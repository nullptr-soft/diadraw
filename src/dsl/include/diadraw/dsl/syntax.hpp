#pragma once

// The DiaDraw syntax tree: a hand-written replacement for the Lezer grammar in
// packages/dsl/src/diadraw.grammar.
//
// Node types carry the grammar's names, and the tree has the same shape as Lezer's (keywords and
// punctuation are leaf children, `Name` wraps an `Identifier`, …), so lowering reads it the same
// way. Offsets are UTF-8 byte offsets into the source. Comments are kept in `Tree::comments`;
// together with the source text the tree is lossless.
//
// Keywords are contextual: `layout`, `order`, `left`… are names wherever an identifier fits.
// Where both readings fit, the parser tries the keyword reading first, as the grammar's dynamic
// precedence does.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dd::dsl {

enum class NodeType : std::uint8_t {
  // Leaves
  Identifier,
  String,
  Number,
  Keyword,  ///< A contextual keyword; its text says which.
  Punct,    ///< `{ } = . , * -> --> <-> ->>`
  Error,    ///< Skipped or missing input (Lezer's ⚠ node).
  // Structure
  Document,
  UseDecl,
  ModelBlock,
  ModelBody,
  ElementDecl,
  Relationship,
  Arrow,
  Group,
  Port,
  PortSide,
  Technology,
  Shape,
  Tag,
  Description,
  StyleProp,
  Prop,
  Name,
  ElementKind,
  Label,
  QualifiedName,
  ViewBlock,
  ViewBody,
  Include,
  Exclude,
  Wildcard,
  LayoutStmt,
  Algorithm,
  DirectionOption,
  Direction,
  SpacingOption,
  Pin,
  Size,
  Coord,
  Route,
  Exit,
  Entry,
  Placement,
  Align,
  Axis,
  Order,
  Sequence,
  SeqBody,
  Message,
  MessageArrow,
  Activate,
  Deactivate,
  SeqNote,
  NoteOver,
  NoteSide,
  Divider,
  Delay,
  Ref,
  Autonumber,
  Frame,
  Else,
  FrameKind,
  // Styles and themes
  ThemeDecl,      ///< `theme dark` (or `theme alias.name`), top level or in a view
  StyleBlock,     ///< `style selector { … }`
  StyleSelector,  ///< `*`, a kind, `relationship`, each with an optional `tag "…"`
  StyleBody,
  StyleDecl,      ///< `key value`
  ThemeDef,       ///< `theme name extends base { … }`
  ThemeBase,      ///< `extends base`
  ThemeBody,
  ThemeNode,      ///< `node kind { … }` in a theme
  // View decorations
  ViewNote,
  ViewText,
  ViewBox,
  Around,
  // Size and rotation
  Resize,  ///< `size a 160, 80` in a view
  Rotate,  ///< `rotate a 30` in a view
};

/// The grammar's name for a node type, e.g. "ElementDecl".
std::string_view node_name(NodeType type);

struct Node {
  NodeType type = NodeType::Error;
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  std::vector<Node> children;

  /// First direct child of the type, or nullptr (Lezer's `getChild`).
  [[nodiscard]] const Node* child(NodeType t) const;
  /// All direct children of the type (Lezer's `getChildren`).
  [[nodiscard]] std::vector<const Node*> all(NodeType t) const;
  [[nodiscard]] const Node* first_child() const { return children.empty() ? nullptr : &children.front(); }
  [[nodiscard]] const Node* last_child() const { return children.empty() ? nullptr : &children.back(); }
};

struct Comment {
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  bool block = false;
};

struct Tree {
  Node root;
  std::vector<Comment> comments;
  /// Where a block string that is never closed opens. It runs to the end, so there is one at most.
  std::optional<std::uint32_t> unclosed_block_string;
};

/// Parses DiaDraw source. Never fails: problems become `Error` nodes in the tree.
Tree parse_syntax(std::string_view source);

/// One token as the lexer reads it, before parsing.
struct Lexeme {
  enum class Kind : std::uint8_t {
    Identifier,
    String,  ///< a closed string, one-line or block
    Number,
    Punct,    ///< `{ } = . , * -> --> <-> ->>`
    Invalid,  ///< a character that is no token, or a string that is not closed
  };
  Kind kind = Kind::Invalid;
  std::uint32_t from = 0;
  std::uint32_t to = 0;
};

/// The source's tokens and comments, each in source order, exactly as parse_syntax reads them.
struct Lexed {
  std::vector<Lexeme> tokens;
  std::vector<Comment> comments;
  /// As in Tree.
  std::optional<std::uint32_t> unclosed_block_string;
};
Lexed lex_source(std::string_view source);

/// Opens and closes a block string, which spans lines (see docs/dsl.md).
inline constexpr std::string_view kBlockQuote = R"(""")";

/// Where a block string whose text starts at `from` ends: the offset just after its closing `"""`,
/// or `npos` when it is not closed. A `\` escapes the byte after it. The lexer scans a whole
/// source with it, the highlighter one line at a time.
[[nodiscard]] std::size_t block_string_close(std::string_view text, std::size_t from);

/// Calls `f(node)` for every node, parents first; returning false skips a node's children.
template <class F>
void walk(const Node& node, F&& f) {
  if (!f(node)) return;
  for (const auto& c : node.children) walk(c, f);
}

}  // namespace dd::dsl
