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

/// The CodeMirror tag name of a kind ("comment", "string", "typeName", …), for checks and hosts
/// that map colours by name.
std::string_view token_kind_name(TokenKind kind);

}  // namespace dd::dsl
