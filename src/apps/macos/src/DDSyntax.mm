#import "DDSyntax.h"

#include "diadraw/dsl/highlight.hpp"

namespace {

/// UTF-16 offset of every UTF-8 byte offset of `text` (and of its end).
std::vector<NSUInteger> utf16_map(const std::string& text) {
  std::vector<NSUInteger> map(text.size() + 1);
  NSUInteger unit = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    map[i] = unit;
    auto c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0) != 0x80) unit += c >= 0xF0 ? 2 : 1;
  }
  map[text.size()] = unit;
  return map;
}

NSColor* colour(dd::dsl::TokenKind kind) {
  using K = dd::dsl::TokenKind;
  switch (kind) {
    case K::Comment: return NSColor.systemGrayColor;
    case K::String: return NSColor.systemRedColor;
    case K::Number: return NSColor.systemOrangeColor;
    case K::Keyword: return NSColor.systemPurpleColor;
    case K::Kind: return NSColor.systemTealColor;
    case K::Definition: return NSColor.systemBlueColor;
    case K::Operator:
    case K::Bracket: return NSColor.secondaryLabelColor;
    case K::Name: return nil;
  }
  return nil;
}

}  // namespace

void DDColourSyntax(NSTextView* view) {
  NSLayoutManager* layout = view.layoutManager;
  NSString* string = view.string;
  NSRange all = NSMakeRange(0, string.length);
  [layout removeTemporaryAttribute:NSForegroundColorAttributeName forCharacterRange:all];
  std::string text = string.UTF8String;
  auto map = utf16_map(text);
  for (const auto& t : dd::dsl::highlight(text))
    if (NSColor* c = colour(t.kind))
      [layout addTemporaryAttribute:NSForegroundColorAttributeName
                              value:c
                  forCharacterRange:NSMakeRange(map[t.from], map[t.to] - map[t.from])];
}

std::vector<DDMark> DDMarkDiagnostics(NSTextView* view, const std::string& text,
                                      const std::vector<dd::dsl::Diagnostic>& diagnostics) {
  NSLayoutManager* layout = view.layoutManager;
  NSUInteger length = view.string.length;
  NSRange all = NSMakeRange(0, length);
  [layout removeTemporaryAttribute:NSUnderlineStyleAttributeName forCharacterRange:all];
  [layout removeTemporaryAttribute:NSUnderlineColorAttributeName forCharacterRange:all];
  std::vector<DDMark> marks;
  // The diagnostics belong to the text the core parsed; if the view has moved on, wait for the next.
  if (std::string(view.string.UTF8String) != text) return marks;
  auto map = utf16_map(text);
  for (const auto& d : diagnostics) {
    NSUInteger from = map[std::min<std::size_t>(d.span.from, text.size())];
    NSUInteger to = map[std::min<std::size_t>(std::max(d.span.to, d.span.from), text.size())];
    // An empty span still gets a visible mark: the character after it, or before it at the end.
    if (to == from) {
      if (to < length) ++to;
      else if (from > 0) --from;
    }
    DDMark mark{NSMakeRange(from, to - from), d.severity == dd::dsl::Severity::Error,
                [NSString stringWithUTF8String:d.message.c_str()] ?: @""};
    [layout addTemporaryAttributes:@{
      NSUnderlineStyleAttributeName : @(NSUnderlineStyleThick | NSUnderlinePatternDot),
      NSUnderlineColorAttributeName : mark.error ? NSColor.systemRedColor : NSColor.systemOrangeColor,
    }
                 forCharacterRange:mark.range];
    marks.push_back(mark);
  }
  return marks;
}
