#pragma once

// Syntax highlighting for DiaDraw source: the tokens a text pane colours. It is a port of the
// stream tokenizer in src/apps/web/src/language.ts (CodeMirror), so every shell colours the same
// words the same way: keywords, names being declared, element kinds, strings, numbers, comments,
// arrows and braces. It does not parse; diagnostics come from dsl::parse.

#include <cstdint>
#include <string_view>
#include <vector>

namespace dd::dsl {

enum class TokenKind {
  Comment,
  String,
  Number,
  Operator,    ///< arrows and `=`
  Bracket,     ///< `{` and `}`
  Keyword,
  Kind,        ///< the element kind after `name =`
  Definition,  ///< a name being declared (`name =`)
  Name,        ///< any other identifier
};

struct Token {
  /// UTF-8 byte offsets into the text.
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  TokenKind kind = TokenKind::Name;
  bool operator==(const Token&) const = default;
};

/// The tokens of `text` in order; text between them (spaces, punctuation) is plain.
std::vector<Token> highlight(std::string_view text);

/// A block a text pane can fold: a `{` … `}` pair (a model, a view, an element with children, a
/// group, a style or theme block, …) whose `}` is at least two lines below its `{`. Folding hides
/// the lines between, so the `{` line and the `}` line stay.
struct FoldRange {
  /// UTF-8 byte offsets of the `{` and of its matching `}`.
  std::uint32_t open = 0;
  std::uint32_t close = 0;
  /// The line of the `{`, where the fold marker goes, and the last line folded away (the one
  /// before the `}`'s line), 0-based. Lines end at `\n`, `\r\n` or `\r`, as in `highlight`
  /// (not at U+2029, which GTK also breaks at; the GTK shell's line numbers assume there is none).
  std::uint32_t start_line = 0;
  std::uint32_t end_line = 0;
  /// Where the `{` line starts, as a byte offset.
  std::uint32_t line = 0;
  /// The hidden text as an editor that joins lines shows it (CodeMirror, VS Code): from the `{`
  /// line's line break to the end of `end_line`'s text (its line break stays).
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  /// The hidden text as whole lines, line breaks included, for text views that hide lines (the
  /// native shells): from the start of the first hidden line to the start of the `}` line.
  std::uint32_t first = 0;
  std::uint32_t last = 0;
  bool operator==(const FoldRange&) const = default;
};

/// The foldable blocks of `text`, by start line, one per line (the outermost where several open
/// on one line). Braces in strings and comments do not count; unmatched ones are skipped, so a
/// half-typed document still folds what it can.
std::vector<FoldRange> fold_ranges(std::string_view text);

/// The CodeMirror tag name of a kind ("comment", "string", "typeName", …), for checks and hosts
/// that map colours by name.
std::string_view token_kind_name(TokenKind kind);

}  // namespace dd::dsl
