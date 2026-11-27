#pragma once

// Formatting DiaDraw source as a text editor's "Format Document" does: the text stays the same
// statements in the same order, with its comments, and only the whitespace between tokens changes.
// Unlike print(), which writes a model back in canonical form, nothing is reordered, merged or
// dropped, and text that does not parse is formatted too.

#include <string>
#include <string_view>

namespace dd::dsl {

inline constexpr int kMinIndentSize = 1;
inline constexpr int kMaxIndentSize = 8;
/// The indentation print() writes, and every editor's setting until the person picks another.
inline constexpr int kDefaultIndentSize = 2;

struct FormatOptions {
  /// Spaces per level of indentation, from kMinIndentSize to kMaxIndentSize; others are clamped.
  int indent_size = kDefaultIndentSize;
};

/// Formats DiaDraw source:
///
/// - each line is indented by `indent_size` spaces per open `{`, a line that starts with `}` one
///   level less, and a line that continues a statement from the line above one level more;
/// - tokens on a line are one space apart: none before `,`, one after it, one around `=`,
///   arrows and braces (`{}`, `{{` and `}}` stay as they are written), and one before a `//`
///   comment; around `.`, `*` and characters that are no token, the spacing is kept, one space
///   where there were several;
/// - trailing whitespace goes (an unclosed string keeps its own, to the end of its line), runs of
///   blank lines become one, and blank lines at the start, right
///   after a `{` and right before a `}` go; the text ends with one line break;
/// - the lines inside a block string (`"""`) or a block comment move with the line they start on
///   and are otherwise kept as they are, so a block string reads back the same text.
///
/// Line breaks between tokens are never added or removed, so the text has the same tokens, on
/// the same lines, before and after. Line endings follow the first one after any leading blank
/// lines (`\r\n` or `\n`), except inside block strings, block comments and unclosed strings, which
/// keep theirs; so in mixed text an unclosed string can gain or keep a `\r` before its line break.
/// A byte order mark at the start is kept. Not to be confused with the C ABI's
/// dd_format_source and `diadraw format`, which reprint the model with print().
[[nodiscard]] std::string format_source(std::string_view source, const FormatOptions& options = {});

}  // namespace dd::dsl
