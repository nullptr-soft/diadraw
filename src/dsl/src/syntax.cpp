#include "diadraw/dsl/syntax.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace dd::dsl {

std::string_view node_name(NodeType type) {
  static constexpr std::array<std::string_view, static_cast<std::size_t>(NodeType::Rotate) + 1> kNames{
      "Identifier", "String",        "Number",      "Keyword",     "Punct",        "⚠",
      "Document",   "UseDecl",       "ModelBlock",  "ModelBody",   "ElementDecl",  "Relationship",
      "Arrow",      "Group",         "Port",        "PortSide",    "Technology",   "Shape",
      "Tag",        "Description",   "StyleProp",   "Prop",        "Name",         "ElementKind",
      "Label",      "QualifiedName", "ViewBlock",   "ViewBody",    "Include",      "Exclude",
      "Wildcard",   "LayoutStmt",    "Algorithm",   "DirectionOption", "Direction", "SpacingOption",
      "Pin",        "Size",          "Coord",       "Route",       "Exit",         "Entry",
      "Placement",  "Align",         "Axis",        "Order",       "Sequence",     "SeqBody",
      "Message",    "MessageArrow",  "Activate",    "Deactivate",  "SeqNote",      "NoteOver",
      "NoteSide",   "Divider",       "Delay",       "Ref",         "Autonumber",   "Frame",
      "Else",       "FrameKind",     "ThemeDecl",   "StyleBlock",  "StyleSelector", "StyleBody",
      "StyleDecl",  "ThemeDef",      "ThemeBase",   "ThemeBody",   "ThemeNode",
      "ViewNote",   "ViewText",      "ViewBox",     "Around",       "Resize",       "Rotate",
  };
  return kNames[static_cast<std::size_t>(type)];
}

const Node* Node::child(NodeType t) const {
  for (const auto& c : children)
    if (c.type == t) return &c;
  return nullptr;
}

std::vector<const Node*> Node::all(NodeType t) const {
  std::vector<const Node*> out;
  for (const auto& c : children)
    if (c.type == t) out.push_back(&c);
  return out;
}

namespace {

// ── Lexer ──────────────────────────────────────────────────────────────────────────────────────

enum class Tok : std::uint8_t { Ident, String, Number, Punct, Invalid, End };

struct Token {
  Tok kind = Tok::End;
  std::uint32_t from = 0;
  std::uint32_t to = 0;
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

/// Length of the UTF-8 sequence starting with `c` (1 for stray continuation bytes).
std::size_t utf8_length(unsigned char c) {
  if (c >= 0xF0) return 4;
  if (c >= 0xE0) return 3;
  if (c >= 0xC0) return 2;
  return 1;
}

}  // namespace

// Out of line on purpose (also under LTO): inlined into the highlighter's loop, it makes GCC stop
// inlining that loop's per-token checks.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
std::size_t block_string_close(std::string_view text, std::size_t from) {
  const std::size_t n = text.size();
  for (std::size_t i = from; i < n;) {
    if (text.substr(i).starts_with(kBlockQuote)) return i + kBlockQuote.size();
    // Neither a quote nor a backslash is ever a UTF-8 continuation byte, so stepping over two bytes finds the
    // same closing quotes in valid UTF-8 and cannot skip them in invalid UTF-8.
    i += text[i] == '\\' ? 2 : 1;
  }
  return std::string_view::npos;
}

namespace {

void lex(std::string_view s, std::vector<Token>& tokens, std::vector<Comment>& comments,
         std::optional<std::uint32_t>& unclosed_block_string) {
  std::size_t i = 0;
  const std::size_t n = s.size();
  auto push = [&](Tok kind, std::size_t from, std::size_t to) {
    tokens.push_back({kind, static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to)});
  };
  while (i < n) {
    char c = s[i];
    if (is_space(c)) {
      ++i;
      continue;
    }
    // Non-breaking space and the BOM count as whitespace too.
    if (s.compare(i, 2, "\xC2\xA0") == 0) {
      i += 2;
      continue;
    }
    if (s.compare(i, 3, "\xEF\xBB\xBF") == 0) {
      i += 3;
      continue;
    }
    if (s.compare(i, 2, "//") == 0) {
      std::size_t start = i;
      while (i < n && s[i] != '\n') ++i;
      comments.push_back({static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(i), false});
      continue;
    }
    if (s.compare(i, 2, "/*") == 0) {
      std::size_t start = i;
      auto end = s.find("*/", i + 2);
      i = end == std::string_view::npos ? n : end + 2;
      comments.push_back({static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(i), true});
      continue;
    }
    std::size_t start = i;
    if (is_ident_start(c)) {
      while (i < n && is_ident_char(s[i])) ++i;
      push(Tok::Ident, start, i);
      continue;
    }
    if (c == '"' && s.substr(i).starts_with(kBlockQuote)) {
      // A block string runs to the next unescaped `"""`, across lines. Like `/*`, an unclosed
      // one runs to the end.
      std::size_t end = block_string_close(s, i + kBlockQuote.size());
      if (end == std::string_view::npos) unclosed_block_string = static_cast<std::uint32_t>(start);
      i = end == std::string_view::npos ? n : end;
      push(end != std::string_view::npos ? Tok::String : Tok::Invalid, start, i);
      continue;
    }
    if (c == '"') {
      ++i;
      bool closed = false;
      while (i < n) {
        if (s[i] == '"') {
          ++i;
          closed = true;
          break;
        }
        if (s[i] == '\n') break;
        if (s[i] == '\\' && i + 1 < n) {
          i += 1 + utf8_length(static_cast<unsigned char>(s[i + 1]));
          continue;
        }
        ++i;
      }
      push(closed ? Tok::String : Tok::Invalid, start, std::min(i, n));
      continue;
    }
    if (is_digit(c) || (c == '-' && i + 1 < n && is_digit(s[i + 1]))) {
      if (c == '-') ++i;
      while (i < n && is_digit(s[i])) ++i;
      if (i + 1 < n && s[i] == '.' && is_digit(s[i + 1])) {
        ++i;
        while (i < n && is_digit(s[i])) ++i;
      }
      push(Tok::Number, start, i);
      continue;
    }
    bool matched = false;
    for (std::string_view p : {"-->", "->>", "<->", "->", "{", "}", "=", ".", ",", "*"}) {
      if (s.compare(i, p.size(), p) == 0) {
        i += p.size();
        push(Tok::Punct, start, i);
        matched = true;
        break;
      }
    }
    if (matched) continue;
    i += utf8_length(static_cast<unsigned char>(c));
    push(Tok::Invalid, start, std::min(i, n));
  }
  push(Tok::End, n, n);
}

// ── Parser ─────────────────────────────────────────────────────────────────────────────────────

class Parser {
 public:
  explicit Parser(std::string_view source) : s_(source) { lex(source, toks_, tree_.comments, tree_.unclosed_block_string); }

  Tree run() {
    Node& doc = tree_.root;
    doc.type = NodeType::Document;
    doc.from = 0;
    while (!at_end()) {
      if (at_kw("use")) {
        statement(doc, {&Parser::use_decl});
      } else if (at_kw("model")) {
        statement(doc, {&Parser::model_block});
      } else if (at_kw("view")) {
        statement(doc, {&Parser::view_block});
      } else if (at_kw("theme")) {
        statement(doc, {theme_defines() ? &Parser::theme_def : &Parser::theme_decl});
      } else if (at_kw("style")) {
        statement(doc, {&Parser::style_block});
      } else {
        // A stray `}` stops error skipping (it closes blocks elsewhere), so take it on its own.
        std::size_t before = pos_;
        skip_error(doc, &Parser::starts_top_level);
        if (pos_ == before) skip_one(doc);
      }
    }
    doc.to = static_cast<std::uint32_t>(s_.size());
    return std::move(tree_);
  }

 private:
  using Production = bool (Parser::*)(Node&);
  using StartTest = bool (Parser::*)();

  // ── Tokens ──

  [[nodiscard]] const Token& tok(std::size_t ahead = 0) const {
    std::size_t i = pos_ + ahead;
    return i < toks_.size() ? toks_[i] : toks_.back();
  }
  [[nodiscard]] std::string_view text(const Token& t) const { return s_.substr(t.from, t.to - t.from); }
  [[nodiscard]] bool at_end() const { return tok().kind == Tok::End; }
  [[nodiscard]] bool at_ident(std::size_t ahead = 0) const { return tok(ahead).kind == Tok::Ident; }
  [[nodiscard]] bool at_string(std::size_t ahead = 0) const { return tok(ahead).kind == Tok::String; }
  [[nodiscard]] bool at_number(std::size_t ahead = 0) const { return tok(ahead).kind == Tok::Number; }
  [[nodiscard]] bool at_kw(std::string_view word, std::size_t ahead = 0) const {
    return at_ident(ahead) && text(tok(ahead)) == word;
  }
  [[nodiscard]] bool at_punct(std::string_view p, std::size_t ahead = 0) const {
    return tok(ahead).kind == Tok::Punct && text(tok(ahead)) == p;
  }
  [[nodiscard]] bool at_any_kw(std::initializer_list<std::string_view> words) const {
    if (!at_ident()) return false;
    auto t = text(tok());
    for (auto w : words)
      if (t == w) return true;
    return false;
  }

  // ── Building nodes ──

  static Node open(NodeType type, std::uint32_t from) {
    Node n;
    n.type = type;
    n.from = from;
    n.to = from;
    return n;
  }
  Node open(NodeType type) const { return open(type, tok().from); }

  static void close(Node& parent, Node&& n) {
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
      if (it->to > it->from || it->type != NodeType::Error) {
        n.to = it->to;
        break;
      }
    }
    if (n.to < n.from) n.to = n.from;
    parent.children.push_back(std::move(n));
  }

  void leaf(Node& parent, NodeType type) {
    const Token& t = tok();
    Node n = open(type, t.from);
    n.to = t.to;
    parent.children.push_back(std::move(n));
    ++pos_;
  }

  /// A node wrapping one token, e.g. `Name { Identifier }` or `Direction { kw<"right"> }`.
  void wrapped(Node& parent, NodeType outer, NodeType inner) {
    Node n = open(outer);
    leaf(n, inner);
    close(parent, std::move(n));
  }

  /// Fails the current production: in a speculative parse just reports failure; otherwise marks
  /// the missing piece with a zero-width error node.
  bool missing(Node& n) {
    if (speculative_ == 0) {
      // Lezer reports a missing piece at the start of the token that did not fit.
      Node e = open(NodeType::Error, tok().from);
      n.children.push_back(std::move(e));
    }
    return false;
  }

  bool punct(Node& n, std::string_view p) {
    if (!at_punct(p)) return missing(n);
    leaf(n, NodeType::Punct);
    return true;
  }
  bool keyword(Node& n, std::string_view word) {
    if (!at_kw(word)) return missing(n);
    leaf(n, NodeType::Keyword);
    return true;
  }
  bool string_leaf(Node& n, NodeType wrap = NodeType::String) {
    if (!at_string()) return missing(n);
    if (wrap == NodeType::String)
      leaf(n, NodeType::String);
    else
      wrapped(n, wrap, NodeType::String);
    return true;
  }

  // ── Speculation ──

  /// Runs a production speculatively; on failure rewinds and leaves `parent` untouched.
  bool attempt(Node& parent, Production p) {
    std::size_t saved_pos = pos_;
    std::size_t saved_size = parent.children.size();
    ++speculative_;
    bool ok = (this->*p)(parent);
    --speculative_;
    if (!ok) {
      pos_ = saved_pos;
      parent.children.resize(saved_size);
    }
    return ok;
  }

  /// Whether a production would parse here, without consuming anything.
  bool would_parse(Production p) {
    Node scratch;
    std::size_t saved_pos = pos_;
    ++speculative_;
    bool ok = (this->*p)(scratch);
    --speculative_;
    pos_ = saved_pos;
    return ok;
  }

  /// Tries each reading in order; when none parses cleanly, commits to the first one and lets it
  /// mark what is missing.
  bool statement(Node& parent, std::initializer_list<Production> readings) {
    for (Production p : readings)
      if (attempt(parent, p)) return true;
    if (speculative_ > 0) return false;
    std::size_t before = pos_;
    (this->*(*readings.begin()))(parent);
    if (pos_ == before) skip_one(parent);
    return false;
  }

  void skip_one(Node& parent) {
    if (at_end()) return;
    Node e = open(NodeType::Error);
    e.to = tok().to;
    ++pos_;
    parent.children.push_back(std::move(e));
  }

  /// Skips input up to the end of the block, or the next line that starts a statement here.
  void skip_error(Node& parent, StartTest starts) {
    Node e = open(NodeType::Error);
    std::size_t line_of_prev = 0;
    bool first = true;
    while (!at_end() && !at_punct("}")) {
      if (!first) {
        bool new_line = s_.find('\n', line_of_prev) < tok().from;
        if (new_line && (this->*starts)()) break;
      }
      first = false;
      line_of_prev = tok().to;
      e.to = tok().to;
      ++pos_;
    }
    if (e.to > e.from) parent.children.push_back(std::move(e));
  }

  // ── Top level ──

  bool starts_top_level() {
    if (at_kw("use")) return would_parse(&Parser::use_decl);
    if (at_kw("model")) return would_parse(&Parser::model_block);
    if (at_kw("view")) return would_parse(&Parser::view_block);
    if (at_kw("theme")) return would_parse(theme_defines() ? &Parser::theme_def : &Parser::theme_decl);
    if (at_kw("style")) return would_parse(&Parser::style_block);
    return false;
  }

  bool use_decl(Node& parent) {
    Node n = open(NodeType::UseDecl);
    bool ok = keyword(n, "use") && string_leaf(n);
    if (ok && at_kw("as")) {
      leaf(n, NodeType::Keyword);
      ok = name(n);
    }
    close(parent, std::move(n));
    return ok;
  }

  bool model_block(Node& parent) {
    Node n = open(NodeType::ModelBlock);
    bool ok = keyword(n, "model") && model_body(n);
    close(parent, std::move(n));
    return ok;
  }

  bool view_block(Node& parent) {
    Node n = open(NodeType::ViewBlock);
    bool ok = keyword(n, "view") && name(n);
    if (ok && at_kw("of")) {
      leaf(n, NodeType::Keyword);
      ok = qualified_name(n);
    }
    ok = ok && view_body(n);
    close(parent, std::move(n));
    return ok;
  }

  // ── Styles and themes ──

  bool theme_decl(Node& parent) {
    Node n = open(NodeType::ThemeDecl);
    bool ok = keyword(n, "theme") && qualified_name(n);
    close(parent, std::move(n));
    return ok;
  }

  /// At `theme`: whether a declaration (`theme name {` or `theme name extends …`) follows.
  [[nodiscard]] bool theme_defines() const {
    if (!at_kw("theme") || !at_ident(1)) return false;
    std::size_t i = 2;
    while (at_punct(".", i) && at_ident(i + 1)) i += 2;
    return at_punct("{", i) || at_kw("extends", i);
  }

  bool theme_def(Node& parent) {
    Node n = open(NodeType::ThemeDef);
    bool ok = keyword(n, "theme") && name(n);
    if (ok && at_kw("extends")) {
      Node base = open(NodeType::ThemeBase);
      leaf(base, NodeType::Keyword);
      ok = qualified_name(base);
      close(n, std::move(base));
    }
    ok = ok && body(n, NodeType::ThemeBody, &Parser::theme_item, &Parser::starts_theme_item);
    close(parent, std::move(n));
    return ok;
  }

  bool starts_theme_item() { return starts_style_decl() || (at_kw("node") && at_ident(1)); }

  bool theme_item(Node& parent) {
    if (at_kw("node") && at_ident(1) && !at_string(2) && !at_number(2) && !at_ident(2)) {
      Node n = open(NodeType::ThemeNode);
      leaf(n, NodeType::Keyword);
      bool ok = name(n) && body(n, NodeType::StyleBody, &Parser::style_decl, &Parser::starts_style_decl);
      close(parent, std::move(n));
      return ok;
    }
    return style_decl(parent);
  }

  bool style_block(Node& parent) {
    Node n = open(NodeType::StyleBlock);
    bool ok = keyword(n, "style") && style_selector(n) &&
              body(n, NodeType::StyleBody, &Parser::style_decl, &Parser::starts_style_decl);
    close(parent, std::move(n));
    return ok;
  }

  /// `*`, `relationship` or an element kind, then an optional `tag "…"`; or just `tag "…"`.
  bool style_selector(Node& parent) {
    Node n = open(NodeType::StyleSelector);
    bool ok = true;
    bool tag_only = at_kw("tag") && at_string(1);
    if (!tag_only) {
      if (at_punct("*"))
        wrapped(n, NodeType::Wildcard, NodeType::Punct);
      else if (at_kw("relationship"))
        leaf(n, NodeType::Keyword);
      else if (at_ident())
        wrapped(n, NodeType::ElementKind, NodeType::Identifier);
      else
        ok = missing(n);
    }
    if (ok && at_kw("tag")) {
      Node tag = open(NodeType::Tag);
      leaf(tag, NodeType::Keyword);
      ok = string_leaf(tag);
      close(n, std::move(tag));
    }
    close(parent, std::move(n));
    return ok;
  }

  bool starts_style_decl() { return at_ident() && (at_ident(1) || at_string(1) || at_number(1)); }

  /// `key value`, the value a string, a number or a word.
  bool style_decl(Node& parent) {
    if (!at_ident()) return false;
    Node n = open(NodeType::StyleDecl);
    wrapped(n, NodeType::Name, NodeType::Identifier);
    bool ok = true;
    if (at_string())
      leaf(n, NodeType::String);
    else if (at_number())
      leaf(n, NodeType::Number);
    else if (at_ident())
      leaf(n, NodeType::Identifier);
    else
      ok = missing(n);
    close(parent, std::move(n));
    return ok;
  }

  // ── Shared pieces ──

  bool name(Node& parent) {
    if (!at_ident()) return missing(parent);
    wrapped(parent, NodeType::Name, NodeType::Identifier);
    return true;
  }

  bool qualified_name(Node& parent) {
    if (!at_ident()) return missing(parent);
    Node n = open(NodeType::QualifiedName);
    leaf(n, NodeType::Identifier);
    bool ok = true;
    while (at_punct(".")) {
      leaf(n, NodeType::Punct);
      if (!at_ident()) {
        ok = missing(n);
        break;
      }
      leaf(n, NodeType::Identifier);
    }
    close(parent, std::move(n));
    return ok;
  }

  bool label_opt(Node& parent) {
    if (at_string()) wrapped(parent, NodeType::Label, NodeType::String);
    return true;
  }

  bool coord(Node& parent) {
    Node n = open(NodeType::Coord);
    bool ok = at_number() || missing(n);
    if (ok) {
      leaf(n, NodeType::Number);
      ok = punct(n, ",") && (at_number() || missing(n));
      if (ok) leaf(n, NodeType::Number);
    }
    close(parent, std::move(n));
    return ok;
  }

  /// `{ item* }` with `item` tried at each position and errors skipped up to the next statement.
  bool body(Node& parent, NodeType type, bool (Parser::*item)(Node&), StartTest starts) {
    Node n = open(type);
    if (!punct(n, "{")) {
      close(parent, std::move(n));
      return false;
    }
    bool ok = true;
    while (!at_end() && !at_punct("}")) {
      std::size_t before = pos_;
      if (!(this->*item)(n)) {
        if (speculative_ > 0) {
          ok = false;
          break;
        }
        if (pos_ == before) skip_error(n, starts);
      }
    }
    ok = ok && punct(n, "}");
    close(parent, std::move(n));
    return ok;
  }

  // ── Model ──

  bool model_body(Node& parent) {
    return body(parent, NodeType::ModelBody, &Parser::model_item, &Parser::starts_model_item);
  }

  bool starts_model_item() {
    Node scratch;
    std::size_t saved = pos_;
    ++speculative_;
    bool ok = model_item(scratch);
    --speculative_;
    pos_ = saved;
    return ok;
  }

  bool model_item(Node& parent) {
    if (!at_ident()) return false;
    if (at_named_relationship()) return statement(parent, {&Parser::relationship});
    if (at_kw("group")) return statement(parent, {&Parser::group, &Parser::element_decl, &Parser::relationship});
    if (at_kw("port")) return statement(parent, {&Parser::port, &Parser::element_decl, &Parser::relationship});
    if (at_punct("=", 1)) return statement(parent, {&Parser::element_decl, &Parser::relationship});
    return statement(parent, {&Parser::relationship, &Parser::element_decl});
  }

  bool element_decl(Node& parent) {
    Node n = open(NodeType::ElementDecl);
    bool ok = name(n) && punct(n, "=");
    if (ok) {
      if (at_ident())
        ok = element_kind(n);
      else
        ok = missing(n);
    }
    if (ok) {
      label_opt(n);
      properties(n);
      if (at_punct("{")) ok = model_body(n);
    }
    close(parent, std::move(n));
    return ok;
  }

  /// A built-in kind (`container`) or a library's (`alias.kind`).
  bool element_kind(Node& parent) {
    Node n = open(NodeType::ElementKind);
    leaf(n, NodeType::Identifier);
    bool ok = true;
    if (at_punct(".")) {
      leaf(n, NodeType::Punct);
      if (at_ident())
        leaf(n, NodeType::Identifier);
      else
        ok = missing(n);
    }
    close(parent, std::move(n));
    return ok;
  }

  /// `name = a.b -> c`: a relationship with an explicit id, not an element whose kind is `a`.
  [[nodiscard]] bool at_named_relationship() const {
    if (!at_ident() || !at_punct("=", 1) || !at_ident(2)) return false;
    std::size_t i = 3;
    while (at_punct(".", i) && at_ident(i + 1)) i += 2;
    return at_punct("->", i) || at_punct("-->", i) || at_punct("<->", i);
  }

  /// `(Name "=")? QName Arrow QName Label? Property*`
  bool relationship(Node& parent) {
    Node n = open(NodeType::Relationship);
    bool ok = true;
    if (at_ident() && at_punct("=", 1)) ok = name(n) && punct(n, "=");
    ok = ok && qualified_name(n);
    if (ok) {
      if (at_punct("->") || at_punct("-->") || at_punct("<->"))
        wrapped(n, NodeType::Arrow, NodeType::Punct);
      else
        ok = missing(n);
    }
    ok = ok && qualified_name(n);
    if (ok) {
      label_opt(n);
      properties(n);
    }
    close(parent, std::move(n));
    return ok;
  }

  void properties(Node& n) {
    while (at_any_kw({"tech", "shape", "tag", "description", "style", "prop"})) {
      if (!attempt(n, &Parser::property)) break;
    }
  }

  bool property(Node& parent) {
    auto word = text(tok());
    NodeType type = word == "tech"          ? NodeType::Technology
                    : word == "shape"       ? NodeType::Shape
                    : word == "tag"         ? NodeType::Tag
                    : word == "description" ? NodeType::Description
                    : word == "style"       ? NodeType::StyleProp
                                            : NodeType::Prop;
    Node n = open(type);
    leaf(n, NodeType::Keyword);
    bool ok;
    if (type == NodeType::Shape)
      ok = qualified_name(n);
    else if (type == NodeType::Prop)
      ok = string_leaf(n) && string_leaf(n);
    else
      ok = string_leaf(n);
    close(parent, std::move(n));
    return ok;
  }

  bool group(Node& parent) {
    Node n = open(NodeType::Group);
    bool ok = keyword(n, "group") && string_leaf(n, NodeType::Label) && model_body(n);
    close(parent, std::move(n));
    return ok;
  }

  bool port(Node& parent) {
    Node n = open(NodeType::Port);
    bool ok = keyword(n, "port") && name(n);
    // `port p north`, unless `north` starts the next statement (`north = …`, `north -> …`).
    if (ok && at_any_kw({"north", "south", "east", "west"}) && !at_punct("=", 1) &&
        !at_punct(".", 1) && !at_punct("->", 1) && !at_punct("-->", 1) && !at_punct("<->", 1)) {
      wrapped(n, NodeType::PortSide, NodeType::Keyword);
    }
    close(parent, std::move(n));
    return ok;
  }

  // ── Views ──

  bool view_body(Node& parent) {
    return body(parent, NodeType::ViewBody, &Parser::view_item, &Parser::starts_view_item);
  }

  static bool is_view_keyword(std::string_view w) {
    return w == "include" || w == "exclude" || w == "layout" || w == "pin" || w == "align" ||
           w == "order" || w == "route" || w == "sequence" || w == "theme" || w == "note" ||
           w == "text" || w == "box" || w == "size" || w == "rotate";
  }

  bool starts_view_item() {
    return at_ident() && is_view_keyword(text(tok())) && view_item_here();
  }

  bool view_item(Node& parent) {
    if (!at_ident()) return false;
    auto w = text(tok());
    Production p = w == "include" || w == "exclude" ? &Parser::selection
                   : w == "layout"                  ? &Parser::layout_stmt
                   : w == "pin"                     ? &Parser::pin
                   : w == "align"                   ? &Parser::align
                   : w == "order"                   ? &Parser::order
                   : w == "route"                   ? &Parser::route
                   : w == "size"                    ? &Parser::resize
                   : w == "rotate"                  ? &Parser::rotate
                   : w == "sequence"                ? &Parser::sequence
                   : w == "theme"                   ? &Parser::theme_decl
                   : w == "note" || w == "text" || w == "box" ? &Parser::decoration
                                                    : nullptr;
    if (!p) return false;
    return statement(parent, {p});
  }

  /// A name in a list ends where a view statement starts: the keyword reading wins.
  bool list_continues(std::initializer_list<std::string_view> stop_words = {}) {
    if (!at_ident()) return false;
    auto w = text(tok());
    for (auto s : stop_words)
      if (w == s) return false;
    if (is_view_keyword(w) && view_item_here()) return false;
    return true;
  }

  /// Whether a view statement parses at the current token. Memoised: lists check it at every
  /// keyword, and without the memo `order order order …` would take exponential time.
  bool view_item_here() {
    auto it = view_item_memo_.find(pos_);
    if (it != view_item_memo_.end()) return it->second;
    bool ok = would_parse(&Parser::view_item);
    view_item_memo_.emplace(pos_, ok);
    return ok;
  }

  bool selection(Node& parent) {
    bool include = at_kw("include");
    Node n = open(include ? NodeType::Include : NodeType::Exclude);
    leaf(n, NodeType::Keyword);
    int count = 0;
    bool ok = true;
    while (true) {
      if (at_punct("*")) {
        wrapped(n, NodeType::Wildcard, NodeType::Punct);
      } else if (list_continues()) {
        if (!qualified_name(n)) {
          ok = false;
          break;
        }
      } else {
        break;
      }
      ++count;
    }
    if (ok && count == 0) ok = missing(n);
    close(parent, std::move(n));
    return ok;
  }

  bool layout_stmt(Node& parent) {
    Node n = open(NodeType::LayoutStmt);
    leaf(n, NodeType::Keyword);
    bool ok = at_ident() || missing(n);
    if (ok) wrapped(n, NodeType::Algorithm, NodeType::Identifier);
    while (ok) {
      if (at_kw("direction") && at_ident(1)) {
        auto d = text(tok(1));
        if (d != "right" && d != "left" && d != "down" && d != "up") break;
        Node opt = open(NodeType::DirectionOption);
        leaf(opt, NodeType::Keyword);
        wrapped(opt, NodeType::Direction, NodeType::Keyword);
        close(n, std::move(opt));
      } else if (at_kw("spacing") && at_number(1)) {
        Node opt = open(NodeType::SpacingOption);
        leaf(opt, NodeType::Keyword);
        leaf(opt, NodeType::Number);
        close(n, std::move(opt));
      } else {
        break;
      }
    }
    close(parent, std::move(n));
    return ok;
  }

  bool pin(Node& parent) {
    Node n = open(NodeType::Pin);
    leaf(n, NodeType::Keyword);
    bool ok = qualified_name(n);
    if (ok) {
      if (at_kw("below") || at_kw("above")) {
        Node p = open(NodeType::Placement);
        leaf(p, NodeType::Keyword);
        close(n, std::move(p));
        ok = qualified_name(n);
      } else if ((at_kw("left") || at_kw("right")) && at_kw("of", 1)) {
        Node p = open(NodeType::Placement);
        leaf(p, NodeType::Keyword);
        leaf(p, NodeType::Keyword);
        close(n, std::move(p));
        ok = qualified_name(n);
      } else if (at_kw("at")) {
        leaf(n, NodeType::Keyword);
        ok = coord(n);
        if (ok && at_kw("size") && at_number(1)) {
          Node size = open(NodeType::Size);
          leaf(size, NodeType::Keyword);
          ok = coord(size);
          close(n, std::move(size));
        }
      } else {
        ok = missing(n);
      }
    }
    close(parent, std::move(n));
    return ok;
  }

  /// `size a 160, 80`
  bool resize(Node& parent) {
    Node n = open(NodeType::Resize);
    leaf(n, NodeType::Keyword);
    bool ok = qualified_name(n) && coord(n);
    close(parent, std::move(n));
    return ok;
  }

  /// `rotate a 30` (degrees, clockwise)
  bool rotate(Node& parent) {
    Node n = open(NodeType::Rotate);
    leaf(n, NodeType::Keyword);
    bool ok = qualified_name(n) && (at_number() || missing(n));
    if (ok) leaf(n, NodeType::Number);
    close(parent, std::move(n));
    return ok;
  }

  bool route(Node& parent) {
    Node n = open(NodeType::Route);
    leaf(n, NodeType::Keyword);
    bool ok = string_leaf(n);
    if (ok && at_kw("via") && at_number(1)) {
      leaf(n, NodeType::Keyword);
      ok = coord(n);
      while (ok && at_number()) ok = coord(n);
    }
    if (ok && at_kw("from") && at_number(1)) {
      Node e = open(NodeType::Exit);
      leaf(e, NodeType::Keyword);
      ok = coord(e);
      close(n, std::move(e));
    }
    if (ok && at_kw("to") && at_number(1)) {
      Node e = open(NodeType::Entry);
      leaf(e, NodeType::Keyword);
      ok = coord(e);
      close(n, std::move(e));
    }
    close(parent, std::move(n));
    return ok;
  }

  /// `align a b c [horizontally|vertically]` and `order a b c`: two names or more.
  bool name_list(Node& n, std::initializer_list<std::string_view> stop_words) {
    bool ok = qualified_name(n);
    int count = ok ? 1 : 0;
    while (ok && list_continues(stop_words)) {
      ok = qualified_name(n);
      ++count;
    }
    if (ok && count < 2) ok = missing(n);
    return ok;
  }

  bool align(Node& parent) {
    Node n = open(NodeType::Align);
    leaf(n, NodeType::Keyword);
    bool ok = name_list(n, {"horizontally", "vertically"});
    if (ok && at_any_kw({"horizontally", "vertically"}))
      wrapped(n, NodeType::Axis, NodeType::Keyword);
    close(parent, std::move(n));
    return ok;
  }

  bool order(Node& parent) {
    Node n = open(NodeType::Order);
    leaf(n, NodeType::Keyword);
    bool ok = name_list(n, {});
    close(parent, std::move(n));
    return ok;
  }

  /// `note [name] "text" [anchor] [style "…"]`, `text …` the same, and
  /// `box [name] ["label"] (around a b … | at x, y size w, h) [style "…"]`, where an anchor is
  /// `below|above|left of|right of <element>` or `at x, y [size w, h]`.
  bool decoration(Node& parent) {
    auto w = text(tok());
    bool is_box = w == "box";
    Node n = open(w == "note" ? NodeType::ViewNote : w == "text" ? NodeType::ViewText : NodeType::ViewBox);
    leaf(n, NodeType::Keyword);
    // A name only when a label or the box's anchor follows it: `box around …` has none.
    if (at_ident() && (at_string(1) || (is_box && (at_kw("around", 1) || at_kw("at", 1)))) &&
        !(is_box && (at_kw("around") || at_kw("at"))))
      name(n);
    bool ok = true;
    if (at_string())
      ok = string_leaf(n, NodeType::Label);
    else if (!is_box)
      ok = missing(n);
    bool anchored = false;
    if (ok && is_box && at_kw("around")) {
      Node a = open(NodeType::Around);
      leaf(a, NodeType::Keyword);
      ok = qualified_name(a);
      while (ok && list_continues({"style"})) ok = qualified_name(a);
      close(n, std::move(a));
      anchored = true;
    } else if (ok && !is_box && (at_kw("below") || at_kw("above"))) {
      Node p = open(NodeType::Placement);
      leaf(p, NodeType::Keyword);
      close(n, std::move(p));
      ok = qualified_name(n);
      anchored = true;
    } else if (ok && !is_box && (at_kw("left") || at_kw("right")) && at_kw("of", 1)) {
      Node p = open(NodeType::Placement);
      leaf(p, NodeType::Keyword);
      leaf(p, NodeType::Keyword);
      close(n, std::move(p));
      ok = qualified_name(n);
      anchored = true;
    } else if (ok && at_kw("at") && at_number(1)) {
      leaf(n, NodeType::Keyword);
      ok = coord(n);
      if (ok && at_kw("size") && at_number(1)) {
        Node size = open(NodeType::Size);
        leaf(size, NodeType::Keyword);
        ok = coord(size);
        close(n, std::move(size));
      } else if (ok && is_box) {
        ok = missing(n);
      }
      anchored = true;
    }
    if (ok && is_box && !anchored) ok = missing(n);
    if (ok && at_kw("style") && at_string(1)) {
      Node st = open(NodeType::StyleProp);
      leaf(st, NodeType::Keyword);
      string_leaf(st);
      close(n, std::move(st));
    }
    close(parent, std::move(n));
    return ok;
  }

  bool sequence(Node& parent) {
    Node n = open(NodeType::Sequence);
    leaf(n, NodeType::Keyword);
    bool ok = seq_body(n);
    close(parent, std::move(n));
    return ok;
  }

  // ── Sequences ──

  bool seq_body(Node& parent) {
    return body(parent, NodeType::SeqBody, &Parser::seq_item, &Parser::starts_seq_item);
  }

  bool starts_seq_item() {
    Node scratch;
    std::size_t saved = pos_;
    ++speculative_;
    bool ok = seq_item(scratch);
    --speculative_;
    pos_ = saved;
    return ok;
  }

  static bool is_frame_kind(std::string_view w) {
    return w == "alt" || w == "opt" || w == "loop" || w == "par" || w == "critical" ||
           w == "break" || w == "group";
  }

  bool seq_item(Node& parent) {
    if (!at_ident()) return false;
    auto w = text(tok());
    Production p = is_frame_kind(w)                         ? &Parser::frame
                   : w == "activate" || w == "deactivate"   ? &Parser::activation
                   : w == "note"                            ? &Parser::seq_note
                   : w == "divider" || w == "delay"         ? &Parser::marker
                   : w == "ref"                             ? &Parser::ref
                   : w == "autonumber"                      ? &Parser::autonumber
                                                            : nullptr;
    if (p) return statement(parent, {p, &Parser::message});
    return statement(parent, {&Parser::message});
  }

  bool message(Node& parent) {
    Node n = open(NodeType::Message);
    bool ok = qualified_name(n);
    if (ok) {
      if (at_punct("->") || at_punct("->>") || at_punct("-->"))
        wrapped(n, NodeType::MessageArrow, NodeType::Punct);
      else
        ok = missing(n);
    }
    ok = ok && qualified_name(n);
    if (ok) label_opt(n);
    close(parent, std::move(n));
    return ok;
  }

  bool activation(Node& parent) {
    Node n = open(at_kw("activate") ? NodeType::Activate : NodeType::Deactivate);
    leaf(n, NodeType::Keyword);
    bool ok = qualified_name(n);
    close(parent, std::move(n));
    return ok;
  }

  bool seq_note(Node& parent) {
    Node n = open(NodeType::SeqNote);
    leaf(n, NodeType::Keyword);
    bool ok = true;
    if (at_kw("over")) {
      wrapped(n, NodeType::NoteOver, NodeType::Keyword);
      ok = qualified_name(n);
      while (ok && at_ident()) ok = qualified_name(n);
    } else if (at_kw("left") || at_kw("right")) {
      wrapped(n, NodeType::NoteSide, NodeType::Keyword);
      ok = keyword(n, "of") && qualified_name(n);
    } else {
      ok = missing(n);
    }
    ok = ok && string_leaf(n, NodeType::Label);
    close(parent, std::move(n));
    return ok;
  }

  bool marker(Node& parent) {
    Node n = open(at_kw("divider") ? NodeType::Divider : NodeType::Delay);
    leaf(n, NodeType::Keyword);
    label_opt(n);
    close(parent, std::move(n));
    return true;
  }

  bool ref(Node& parent) {
    Node n = open(NodeType::Ref);
    leaf(n, NodeType::Keyword);
    bool ok = keyword(n, "over") && qualified_name(n);
    while (ok && at_ident()) ok = qualified_name(n);
    ok = ok && string_leaf(n, NodeType::Label);
    close(parent, std::move(n));
    return ok;
  }

  bool autonumber(Node& parent) {
    Node n = open(NodeType::Autonumber);
    leaf(n, NodeType::Keyword);
    close(parent, std::move(n));
    return true;
  }

  bool frame(Node& parent) {
    Node n = open(NodeType::Frame);
    wrapped(n, NodeType::FrameKind, NodeType::Keyword);
    label_opt(n);
    bool ok = seq_body(n);
    while (ok && at_kw("else")) {
      wrapped(n, NodeType::Else, NodeType::Keyword);
      label_opt(n);
      ok = seq_body(n);
    }
    close(parent, std::move(n));
    return ok;
  }

  std::string_view s_;
  std::vector<Token> toks_;
  Tree tree_;
  std::size_t pos_ = 0;
  int speculative_ = 0;
  std::unordered_map<std::size_t, bool> view_item_memo_;
};

}  // namespace

Tree parse_syntax(std::string_view source) { return Parser(source).run(); }

}  // namespace dd::dsl
