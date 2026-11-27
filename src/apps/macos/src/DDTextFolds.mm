#import "DDTextFolds.h"

#import "DDSyntax.h"

#include <algorithm>
#include <string>
#include <vector>

#include "diadraw/dsl/highlight.hpp"

namespace {

constexpr CGFloat kRulerWidth = 16;

/// A foldable block in the view's UTF-16 offsets.
struct Block {
  NSInteger line = 0;     ///< of the `{`, 0-based
  NSInteger last = 0;     ///< the last line a fold hides
  NSUInteger open = 0;    ///< the `{`
  NSRange hidden;         ///< whole lines, line breaks included: the `{` and `}` lines stay
  bool folded = false;
};

/// The blocks of `text`: dd::dsl::fold_ranges in UTF-16 offsets.
std::vector<Block> blocks_of(const std::string& text) {
  auto ranges = dd::dsl::fold_ranges(text);
  std::vector<Block> out;
  if (ranges.empty()) return out;
  auto utf16 = DDUtf16Map(text);
  out.reserve(ranges.size());
  for (const auto& r : ranges)
    out.push_back({static_cast<NSInteger>(r.start_line), static_cast<NSInteger>(r.end_line), utf16[r.open],
                   NSMakeRange(utf16[r.first], utf16[r.last] - utf16[r.first]), false});
  return out;
}

bool inside(NSUInteger i, NSRange r) { return i >= r.location && i < NSMaxRange(r); }

}  // namespace

@implementation DDTextFolds {
  __weak NSTextView* _textView;
  std::vector<Block> _blocks;
  /// The `{` of each folded block, kept in step with edits (see storageEdited:).
  std::vector<NSUInteger> _folded;
  /// The hidden ranges the glyphs were last generated for.
  std::vector<NSRange> _hidden;
  /// The text changed since `_blocks` were found: while typing, the selection moves before
  /// textDidChange, and the caret must not be checked against the old blocks.
  BOOL _stale;
}

- (instancetype)initWithTextView:(NSTextView*)view {
  NSScrollView* scroll = view.enclosingScrollView;
  self = [super initWithScrollView:scroll orientation:NSVerticalRuler];
  if (!self) return nil;
  _textView = view;
  self.clientView = view;
  self.ruleThickness = kRulerWidth;
  self.reservedThicknessForMarkers = 0;
  self.reservedThicknessForAccessoryView = 0;
  self.toolTip = @"Fold or unfold a block";
  scroll.verticalRulerView = self;
  scroll.hasVerticalRuler = YES;
  scroll.rulersVisible = YES;
  view.layoutManager.delegate = self;
  [NSNotificationCenter.defaultCenter addObserver:self
                                         selector:@selector(storageEdited:)
                                             name:NSTextStorageDidProcessEditingNotification
                                           object:view.textStorage];
  [self textDidChange];
  return self;
}

- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
}

/// Moves the folded blocks' `{`s with an edit; one inside the edited text is dropped.
- (void)storageEdited:(NSNotification*)note {
  NSTextStorage* storage = note.object;
  if (!(storage.editedMask & NSTextStorageEditedCharacters)) return;
  _stale = YES;
  NSRange edited = storage.editedRange;
  NSInteger delta = storage.changeInLength;
  NSUInteger oldEnd = NSMaxRange(edited) - delta;  // the end of the replaced text, before the edit
  std::vector<NSUInteger> kept;
  for (NSUInteger o : _folded) {
    if (o < edited.location) kept.push_back(o);
    else if (o >= oldEnd) kept.push_back(o + delta);
  }
  _folded = std::move(kept);
}

- (void)textDidChange {
  NSTextView* view = _textView;
  if (!view) return;
  _blocks = blocks_of(view.string.UTF8String ?: "");
  _stale = NO;
  std::vector<NSUInteger> folded;
  for (auto& b : _blocks)
    if (std::find(_folded.begin(), _folded.end(), b.open) != _folded.end()) {
      b.folded = true;
      folded.push_back(b.open);
    }
  _folded = std::move(folded);  // blocks that went unfold
  [self selectionDidChange];
  [self apply];
}

- (void)selectionDidChange {
  if (_stale) return;  // textDidChange checks again
  NSUInteger caret = _textView.selectedRange.location;
  bool changed = false;
  for (auto& b : _blocks)
    if (b.folded && inside(caret, b.hidden)) {
      b.folded = false;
      std::erase(_folded, b.open);
      changed = true;
    }
  if (changed) [self apply];
}

- (BOOL)toggleLine:(NSInteger)line {
  auto it = std::find_if(_blocks.begin(), _blocks.end(), [&](const Block& b) { return b.line == line; });
  if (it == _blocks.end()) return NO;
  it->folded = !it->folded;
  if (it->folded) {
    _folded.push_back(it->open);
    // A caret in the lines going out of sight moves to the end of the `{` line.
    if (inside(_textView.selectedRange.location, it->hidden)) {
      NSUInteger end = it->hidden.location - 1;  // before the `{` line's break, \r\n or \n
      if (end > 0 && [_textView.string characterAtIndex:end - 1] == '\r') --end;
      _textView.selectedRange = NSMakeRange(end, 0);
    }
  } else {
    std::erase(_folded, it->open);
  }
  [self apply];
  return YES;
}

- (std::vector<DDFoldMarker>)foldMarkers {
  std::vector<DDFoldMarker> out;
  for (const auto& b : _blocks) out.push_back({b.line, b.folded});
  return out;
}

/// Regenerates the glyphs of the text whose hiding changed, and redraws the markers.
- (void)apply {
  std::vector<NSRange> hidden;
  for (const auto& b : _blocks)
    if (b.folded) hidden.push_back(b.hidden);
  bool same = hidden.size() == _hidden.size() &&
              std::equal(hidden.begin(), hidden.end(), _hidden.begin(),
                         [](NSRange a, NSRange b) { return NSEqualRanges(a, b); });
  NSLayoutManager* layout = _textView.layoutManager;
  NSUInteger length = _textView.textStorage.length;
  // A folded block's `{` line is shaded, so what is hidden shows where the marker is not seen.
  [layout removeTemporaryAttribute:NSBackgroundColorAttributeName forCharacterRange:NSMakeRange(0, length)];
  for (const auto& b : _blocks)
    if (b.folded && b.hidden.location <= length) {
      NSRange line = [_textView.string lineRangeForRange:NSMakeRange(b.open, 0)];
      [layout addTemporaryAttribute:NSBackgroundColorAttributeName
                              value:[NSColor.secondaryLabelColor colorWithAlphaComponent:0.12]
                  forCharacterRange:line];
    }
  if (!same) {
    NSUInteger from = NSUIntegerMax, to = 0;
    for (const auto* list : {&hidden, &_hidden})
      for (NSRange r : *list) {
        from = std::min(from, r.location);
        to = std::max(to, NSMaxRange(r));
      }
    _hidden = std::move(hidden);
    to = std::min(to, length);
    if (from < to) {
      NSRange range = NSMakeRange(from, to - from);
      [layout invalidateGlyphsForCharacterRange:range changeInLength:0 actualCharacterRange:nil];
      [layout invalidateLayoutForCharacterRange:range actualCharacterRange:nil];
    }
  }
  self.needsDisplay = YES;
}

- (BOOL)hidden:(NSUInteger)index {
  for (NSRange r : _hidden)
    if (inside(index, r)) return YES;
  return NO;
}

#pragma mark NSLayoutManagerDelegate

/// Hidden characters get null glyphs.
- (NSUInteger)layoutManager:(NSLayoutManager*)layout
       shouldGenerateGlyphs:(const CGGlyph*)glyphs
                 properties:(const NSGlyphProperty*)props
           characterIndexes:(const NSUInteger*)indexes
                       font:(NSFont*)font
              forGlyphRange:(NSRange)range {
  if (_hidden.empty()) return 0;
  std::vector<NSGlyphProperty> changed(props, props + range.length);
  bool any = false;
  for (NSUInteger i = 0; i < range.length; ++i)
    if ([self hidden:indexes[i]]) {
      changed[i] = NSGlyphPropertyNull;
      any = true;
    }
  if (!any) return 0;
  [layout setGlyphs:glyphs properties:changed.data() characterIndexes:indexes font:font forGlyphRange:range];
  return range.length;
}

/// Hidden line breaks break no lines, so the hidden lines take no height.
- (NSControlCharacterAction)layoutManager:(NSLayoutManager*)layout
                          shouldUseAction:(NSControlCharacterAction)action
               forControlCharacterAtIndex:(NSUInteger)index {
  return [self hidden:index] ? NSControlCharacterActionZeroAdvancement : action;
}

#pragma mark Drawing and clicks

/// Where a block's marker goes, in this ruler's coordinates; empty when its line is hidden.
- (NSRect)markerRect:(const Block&)b {
  NSTextView* view = _textView;
  NSLayoutManager* layout = view.layoutManager;
  if (b.open >= view.textStorage.length || [self hidden:b.open]) return NSZeroRect;
  NSUInteger glyph = [layout glyphIndexForCharacterAtIndex:b.open];
  NSRect line = [layout lineFragmentRectForGlyphAtIndex:glyph effectiveRange:nil];
  line.origin.y += view.textContainerOrigin.y;
  NSPoint top = [self convertPoint:line.origin fromView:view];
  return NSMakeRect(0, top.y, kRulerWidth, NSHeight(line));
}

- (void)drawHashMarksAndLabelsInRect:(NSRect)dirty {
  for (const auto& b : _blocks) {
    NSRect r = [self markerRect:b];
    if (NSIsEmptyRect(r) || !NSIntersectsRect(r, dirty)) continue;
    CGFloat cx = NSMidX(r), cy = NSMidY(r), s = 3.5;
    // Flipped like the text view: y grows downwards.
    NSBezierPath* path = [NSBezierPath bezierPath];
    if (b.folded) {  // ▸
      [path moveToPoint:NSMakePoint(cx - s * 0.6, cy - s)];
      [path lineToPoint:NSMakePoint(cx + s * 0.9, cy)];
      [path lineToPoint:NSMakePoint(cx - s * 0.6, cy + s)];
    } else {  // ▾
      [path moveToPoint:NSMakePoint(cx - s, cy - s * 0.6)];
      [path lineToPoint:NSMakePoint(cx + s, cy - s * 0.6)];
      [path lineToPoint:NSMakePoint(cx, cy + s * 0.9)];
    }
    [path closePath];
    [(b.folded ? NSColor.secondaryLabelColor : NSColor.tertiaryLabelColor) setFill];
    [path fill];
  }
}

- (BOOL)isFlipped {
  return YES;
}

- (void)mouseDown:(NSEvent*)event {
  NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
  for (const auto& b : _blocks) {
    NSRect r = [self markerRect:b];
    if (!NSIsEmptyRect(r) && p.y >= NSMinY(r) && p.y < NSMaxY(r)) {
      [self toggleLine:b.line];
      return;
    }
  }
}

- (void)resetCursorRects {
  [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor];
}

@end
