#import "DDDrawing.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace dd::mac {

namespace {

using namespace dd::draw;

CGAffineTransform to_cg(const Matrix& m) { return CGAffineTransformMake(m.a, m.b, m.c, m.d, m.e, m.f); }

void add_path(CGContextRef ctx, const std::vector<Segment>& path) {
  for (const auto& s : path) {
    switch (s.op) {
      case Segment::Move: CGContextMoveToPoint(ctx, s.p[0].x, s.p[0].y); break;
      case Segment::Line: CGContextAddLineToPoint(ctx, s.p[0].x, s.p[0].y); break;
      case Segment::Cubic:
        CGContextAddCurveToPoint(ctx, s.p[0].x, s.p[0].y, s.p[1].x, s.p[1].y, s.p[2].x, s.p[2].y);
        break;
      case Segment::Close: CGContextClosePath(ctx); break;
    }
  }
}

/// The first family of a CSS font-family list that this Mac has, else the system font.
NSFont* upright_font_for(const Text& t, bool bold) {
  NSFontWeight weight = bold ? NSFontWeightSemibold : NSFontWeightRegular;
  NSString* families = [NSString stringWithUTF8String:t.font_family.c_str()];
  for (NSString* raw in [families componentsSeparatedByString:@","]) {
    NSString* family = [[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]
        stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\"'"]];
    if (family.length == 0) continue;
    NSString* lower = family.lowercaseString;
    if ([lower isEqualToString:@"system-ui"] || [lower isEqualToString:@"-apple-system"] ||
        [lower isEqualToString:@"sans-serif"])
      return [NSFont systemFontOfSize:t.font_size weight:weight];
    NSFontDescriptor* d = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
      NSFontFamilyAttribute : family,
      NSFontTraitsAttribute : @{NSFontWeightTrait : @(weight)}
    }];
    NSFont* font = [NSFont fontWithDescriptor:d size:t.font_size];
    if (font && [font.familyName caseInsensitiveCompare:family] == NSOrderedSame) return font;
  }
  return [NSFont systemFontOfSize:t.font_size weight:weight];
}

/// The font of a run of `t`: its family and size, in the run's weight and slant.
NSFont* font_for(const Text& t, bool bold, bool italic) {
  NSFont* font = upright_font_for(t, bold);
  if (!italic) return font;
  // Through the descriptor, which finds the system font's italic and keeps the weight.
  NSFontDescriptor* d = [font.fontDescriptor
      fontDescriptorWithSymbolicTraits:font.fontDescriptor.symbolicTraits | NSFontDescriptorTraitItalic];
  NSFont* slanted = d ? [NSFont fontWithDescriptor:d size:font.pointSize] : nil;
  return slanted ?: font;
}

void draw_text(CGContextRef ctx, const Text& t) {
  NSColor* color = [NSColor colorWithSRGBRed:t.color.r green:t.color.g blue:t.color.b alpha:t.alpha];
  // Each run in its own font; Core Text does not draw underlines or strike-throughs, so their
  // ranges (in UTF-16 units) are kept to draw after the glyphs.
  struct Lined {
    NSRange range;
    NSFont* font;
    bool underline, strike;
  };
  std::vector<Lined> lined;
  // At most four faces (bold or not, italic or not), each looked up once.
  NSFont* fonts[4] = {nil, nil, nil, nil};
  NSMutableAttributedString* attributed = [[NSMutableAttributedString alloc] init];
  for_each_span(t, [&](std::string_view run, bool bold, bool italic, bool underline, bool strike) {
    NSString* part = [[NSString alloc] initWithBytes:run.data() length:run.size() encoding:NSUTF8StringEncoding];
    if (!part) return;
    NSFont* __strong& font = fonts[(bold ? 1 : 0) + (italic ? 2 : 0)];
    if (!font) font = font_for(t, bold, italic);
    NSRange range = NSMakeRange(attributed.length, part.length);
    [attributed appendAttributedString:[[NSAttributedString alloc]
                                           initWithString:part
                                               attributes:@{NSFontAttributeName : font, NSForegroundColorAttributeName : color}]];
    if (underline || strike) lined.push_back({range, font, underline, strike});
  });
  if (attributed.length == 0) return;
  CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed);
  CGFloat ascent = 0, descent = 0;
  double width = CTLineGetTypographicBounds(line, &ascent, &descent, nullptr);
  double x = t.x, y = t.y;
  if (t.anchor == "middle") x -= width / 2;
  else if (t.anchor == "end") x -= width;
  if (t.baseline == "central" || t.baseline == "middle") y += (ascent - descent) / 2;
  else if (t.baseline == "hanging" || t.baseline == "text-before-edge") y += ascent;
  else if (t.baseline == "text-after-edge" || t.baseline == "ideographic") y -= descent;
  CGContextSaveGState(ctx);
  // The context is y-down; glyphs draw y-up.
  CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));
  CGContextSetTextPosition(ctx, x, y);
  CTLineDraw(line, ctx);
  CGContextSetRGBFillColor(ctx, t.color.r, t.color.g, t.color.b, t.alpha);
  for (const auto& l : lined) {
    const CGFloat from = CTLineGetOffsetForStringIndex(line, static_cast<CFIndex>(l.range.location), nullptr);
    const CGFloat to = CTLineGetOffsetForStringIndex(line, static_cast<CFIndex>(NSMaxRange(l.range)), nullptr);
    const CGFloat thickness = std::max<CGFloat>(1, l.font.underlineThickness);
    // underlinePosition is below the baseline (negative, y-up); y grows down here.
    if (l.underline) CGContextFillRect(ctx, CGRectMake(x + from, y - l.font.underlinePosition, to - from, thickness));
    if (l.strike) CGContextFillRect(ctx, CGRectMake(x + from, y - l.font.xHeight / 2 - thickness / 2, to - from, thickness));
  }
  CGContextRestoreGState(ctx);
  CFRelease(line);
}

void draw_path(CGContextRef ctx, const Item& item) {
  const Paint& p = item.paint;
  CGContextSaveGState(ctx);
  if (p.fill) {
    CGContextSetRGBFillColor(ctx, p.fill->r, p.fill->g, p.fill->b, p.fill_alpha);
    add_path(ctx, item.path);
    CGContextDrawPath(ctx, p.even_odd ? kCGPathEOFill : kCGPathFill);
  }
  if (p.stroke) {
    CGContextSetRGBStrokeColor(ctx, p.stroke->r, p.stroke->g, p.stroke->b, p.stroke_alpha);
    CGContextSetLineWidth(ctx, p.stroke_width);
    CGContextSetLineCap(ctx, p.cap == LineCap::Round ? kCGLineCapRound : p.cap == LineCap::Square ? kCGLineCapSquare
                                                                                                    : kCGLineCapButt);
    CGContextSetLineJoin(ctx, p.join == LineJoin::Round ? kCGLineJoinRound : p.join == LineJoin::Bevel
                                                                                  ? kCGLineJoinBevel
                                                                                  : kCGLineJoinMiter);
    CGContextSetMiterLimit(ctx, p.miter);
    if (!p.dash.empty()) {
      std::vector<CGFloat> dash(p.dash.begin(), p.dash.end());
      CGContextSetLineDash(ctx, 0, dash.data(), dash.size());
    }
    add_path(ctx, item.path);
    CGContextStrokePath(ctx);
  }
  CGContextRestoreGState(ctx);
}

}  // namespace

void paint(CGContextRef ctx, const DisplayList& list) {
  CGContextSaveGState(ctx);
  CGContextConcatCTM(ctx, to_cg(list.root));
  int depth = 0;
  for (const auto& item : list.items) {
    switch (item.kind) {
      case Item::Save:
        CGContextSaveGState(ctx);
        ++depth;
        break;
      case Item::Restore:
        if (depth > 0) {
          CGContextRestoreGState(ctx);
          --depth;
        }
        break;
      case Item::Transform: CGContextConcatCTM(ctx, to_cg(item.matrix)); break;
      case Item::Clip: CGContextClipToRect(ctx, CGRectMake(item.rect[0], item.rect[1], item.rect[2], item.rect[3])); break;
      case Item::Path: draw_path(ctx, item); break;
      case Item::Label: draw_text(ctx, item.text); break;
    }
  }
  while (depth-- > 0) CGContextRestoreGState(ctx);
  CGContextRestoreGState(ctx);
}

}  // namespace dd::mac
