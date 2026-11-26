// Port of packages/dsl/src/lower.ts: the syntax tree lowered into model operations.

#include <algorithm>
#include <span>
#include <unordered_set>

#include "diadraw/base/text.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::dsl {

// ── Text helpers ─────────────────────────────────────────────────────────────────────────────

namespace {

bool is_blank(std::string_view line) { return line.find_first_not_of(" \t") == std::string_view::npos; }

/// Escapes resolved, appended to `out`: `\n` a newline, `\x` → `x`.
void append_unescaped(std::string_view inner, std::string& out) {
  for (std::size_t i = 0; i < inner.size(); ++i) {
    // `.` in /\\(.)/ does not match a newline, so a backslash before one is kept.
    if (inner[i] == '\\' && i + 1 < inner.size() && inner[i + 1] != '\n' && inner[i + 1] != '\r') {
      ++i;
      out.push_back(inner[i] == 'n' ? '\n' : inner[i]);
    } else {
      out.push_back(inner[i]);
    }
  }
}

/// The text of a block string's inside: a blank first line (the rest of the line `"""` opens) and
/// a blank last line (the one `"""` closes) dropped, the indentation the other non-blank lines
/// share removed, blank lines emptied and `\r\n` read as `\n`.
std::string block_text(std::string_view inner) {
  std::vector<std::string_view> lines;
  lines.reserve(static_cast<std::size_t>(std::ranges::count(inner, '\n')) + 1);
  for (std::size_t start = 0;;) {
    std::size_t nl = inner.find('\n', start);
    std::string_view line = inner.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
    if (line.ends_with('\r')) line.remove_suffix(1);
    lines.push_back(line);
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  std::span<const std::string_view> kept(lines);
  if (kept.size() > 1 && is_blank(kept.front())) kept = kept.subspan(1);
  if (kept.size() > 1 && is_blank(kept.back())) kept = kept.first(kept.size() - 1);
  std::size_t common = std::string_view::npos;
  for (auto line : kept)
    if (!is_blank(line)) common = std::min(common, line.find_first_not_of(" \t"));
  // Escapes are resolved per line, after the indentation is gone: a backslash before a line break
  // is kept, as in a one-line string.
  std::string out;
  out.reserve(inner.size());
  for (std::size_t i = 0; i < kept.size(); ++i) {
    if (i) out.push_back('\n');
    if (!is_blank(kept[i])) append_unescaped(kept[i].substr(common), out);
  }
  return out;
}

}  // namespace

std::string unquote(std::string_view literal) {
  if (literal.size() >= 2 * kBlockQuote.size() && literal.starts_with(kBlockQuote) && literal.ends_with(kBlockQuote))
    return block_text(literal.substr(kBlockQuote.size(), literal.size() - 2 * kBlockQuote.size()));
  std::string_view inner = literal.size() >= 2 ? literal.substr(1, literal.size() - 2) : literal;
  std::string out;
  out.reserve(inner.size());
  append_unescaped(inner, out);
  return out;
}

std::string quote_lines(std::string_view text, std::string_view indent) {
  if (text.find('\n') == std::string_view::npos || text.find(kBlockQuote) != std::string_view::npos ||
      text.find('\r') != std::string_view::npos)
    return quote(text);
  std::string out(kBlockQuote);
  out.reserve(text.size() + 2 * kBlockQuote.size() +
              (static_cast<std::size_t>(std::ranges::count(text, '\n')) + 2) * (indent.size() + 3));
  bool flush_left = false;  // a line without leading blanks, so the shared indentation is the block's
  for (std::size_t start = 0;;) {
    std::size_t nl = text.find('\n', start);
    std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
    out.push_back('\n');
    if (!line.empty()) {
      // Blank-only lines read back empty, so they cannot be written in a block.
      if (is_blank(line)) return quote(text);
      flush_left = flush_left || (line[0] != ' ' && line[0] != '\t');
      out.append(indent).append("  ");
      for (char c : line) {
        if (c == '\\') out.push_back('\\');
        out.push_back(c);
      }
    }
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  if (!flush_left) return quote(text);
  out.push_back('\n');
  out.append(indent).append(kBlockQuote);
  return out;
}

std::string quote(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '"')
      out += "\\\"";
    else if (c == '\n')
      out += "\\n";
    else
      out.push_back(c);
  }
  out.push_back('"');
  return out;
}

Style parse_style_text(std::string_view text) {
  Style style;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find(';', start);
    if (end == std::string_view::npos) end = text.size();
    auto part = text.substr(start, end - start);
    if (!part.empty()) {
      auto eq = part.find('=');
      if (eq == std::string_view::npos)
        style.set(std::string(part), "");
      else
        style.set(std::string(part.substr(0, eq)), std::string(part.substr(eq + 1)));
    }
    start = end + 1;
  }
  return style;
}

std::string print_style_text(const Style& style) {
  std::string out;
  bool first = true;
  for (const auto& [k, v] : style) {
    if (!first) out.push_back(';');
    first = false;
    out += v.empty() ? k : k + "=" + v;
  }
  return out;
}

std::string print_style_value(std::string_view value) {
  auto word = [](std::string_view v) {
    if (v.empty() || !((v[0] >= 'a' && v[0] <= 'z') || (v[0] >= 'A' && v[0] <= 'Z') || v[0] == '_')) return false;
    return std::all_of(v.begin(), v.end(), [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
  };
  auto number = [](std::string_view v) {
    std::size_t i = !v.empty() && v[0] == '-' ? 1 : 0;
    std::size_t digits = 0;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') ++i, ++digits;
    if (digits == 0) return false;
    if (i == v.size()) return true;
    if (v[i] != '.' || i + 1 == v.size()) return false;
    ++i;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') ++i;
    return i == v.size();
  };
  return word(value) || number(value) ? std::string(value) : quote(value);
}

std::string relationship_id(const Endpoint& source, const Endpoint& target, int index) {
  auto end = [](const Endpoint& e) { return e.port ? e.element + "." + *e.port : e.element; };
  std::string base = end(source) + "->" + end(target);
  return index == 1 ? base : base + "#" + std::to_string(index);
}

namespace {

const std::unordered_set<std::string_view> kBuiltinKinds{
    "person", "system",  "container", "component",  "database", "process",  "decision",
    "start",  "end",     "note",      "class",      "object",   "event",    "task",
    "subprocess", "gateway", "data",  "annotation", "group",    "pool",     "lane",
    "conversation"};

bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
bool is_alnum(char c) { return is_word_char(c) && c != '_'; }

/// /^(@[a-z0-9][\w.-]*\/)?[a-z0-9][\w.-]*$/i
bool is_library_name(std::string_view name) {
  auto segment = [](std::string_view s) {
    if (s.empty() || !is_alnum(s.front())) return false;
    return std::all_of(s.begin() + 1, s.end(),
                       [](char c) { return is_word_char(c) || c == '.' || c == '-'; });
  };
  if (!name.empty() && name.front() == '@') {
    auto slash = name.find('/');
    if (slash == std::string_view::npos) return false;
    return segment(name.substr(1, slash - 1)) && segment(name.substr(slash + 1));
  }
  return segment(name);
}

double to_number(std::string_view s) {
  double d = 0;
  (void)from_chars_double(s.data(), s.data() + s.size(), d);
  return d;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::vector<std::string> with(std::vector<std::string> scope, const std::string& more) {
  scope.push_back(more);
  return scope;
}

Span span(const Node& n) { return {n.from, n.to}; }

class Lowering {
 public:
  explicit Lowering(std::string_view source) : src_(source) {
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < source.size(); ++i)
      if (source[i] == '\n') line_starts_.push_back(static_cast<std::uint32_t>(i + 1));
    result_.tree = parse_syntax(source);
  }

  ParseResult run() {
    syntax_errors();
    const Node& top = result_.tree.root;
    for (const Node* use : top.all(NodeType::UseDecl)) use_decl(*use);
    for (const Node* def : top.all(NodeType::ThemeDef)) theme_def(*def);
    theme_cycles();
    for (const auto& child : top.children) {
      switch (child.type) {
        case NodeType::UseDecl:
        case NodeType::ThemeDecl:
        case NodeType::ThemeDef:
        case NodeType::StyleBlock:
        case NodeType::ModelBlock:
        case NodeType::ViewBlock: result_.source_map.statements.push_back(span(child)); break;
        default: break;
      }
      if (child.type == NodeType::ThemeDecl) theme_decl(child);
      if (child.type == NodeType::StyleBlock) style_block(child);
    }
    for (const Node* block : top.all(NodeType::ModelBlock)) {
      const Node* body = block->child(NodeType::ModelBody);
      if (!body) continue;
      if (!result_.source_map.model) result_.source_map.model = span(*body);
      model_body(*body, {}, std::nullopt);
    }
    for (std::size_t i = 0; i < pending_.size(); ++i) relationship(*pending_[i].node, pending_[i].scope);
    for (const Node* view : top.all(NodeType::ViewBlock)) view_block(*view);
    std::stable_sort(result_.diagnostics.begin(), result_.diagnostics.end(),
                     [](const Diagnostic& a, const Diagnostic& b) { return a.span.from < b.span.from; });
    return std::move(result_);
  }

 private:
  struct Pending {
    const Node* node;
    std::vector<std::string> scope;
  };

  // ── Helpers ──

  [[nodiscard]] std::string_view text(const Node& n) const { return src_.substr(n.from, n.to - n.from); }
  [[nodiscard]] Model& model() { return result_.model; }

  void report(Severity severity, Span at, std::string message) {
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), at.from);
    std::size_t line = static_cast<std::size_t>(it - line_starts_.begin()) - 1;
    std::uint32_t start = line_starts_[line];
    Diagnostic d;
    d.span = {at.from, std::max(at.to, at.from)};
    d.severity = severity;
    d.message = std::move(message);
    d.line = static_cast<int>(line + 1);
    d.column = static_cast<int>(utf16_length(src_.substr(start, at.from - start))) + 1;
    result_.diagnostics.push_back(std::move(d));
  }

  bool apply(Operation op, Span at) {
    auto inverse = apply_in_place(model(), op);
    if (!inverse) {
      report(Severity::Error, at, inverse.error().message);
      return false;
    }
    result_.operations.push_back(std::move(op));
    return true;
  }

  void syntax_errors() {
    const auto& open = result_.tree.unclosed_block_string;
    // An unclosed block string runs to the end of the text, so its error marks only its opening.
    if (open)
      report(Severity::Error, {*open, static_cast<std::uint32_t>(*open + kBlockQuote.size())},
             "Unclosed block string: end it with \"\"\"");
    walk(result_.tree.root, [&](const Node& node) {
      if (node.type != NodeType::Error) return true;
      std::string_view t = trim(text(node));
      std::uint32_t to = node.to > node.from
                             ? node.to
                             : std::min<std::uint32_t>(node.from + 1, static_cast<std::uint32_t>(src_.size()));
      // An error that runs into the unclosed string stops where it opens; the string has its own.
      if (open && node.from < *open && to > *open) to = *open;
      // The error nodes at its opening (the value missing just before it, and the string itself)
      // need no error of their own.
      std::size_t first = t.empty() ? node.from : static_cast<std::size_t>(t.data() - src_.data());
      if (open && first == *open) return false;
      if (t.empty()) {
        report(Severity::Error, {node.from, to}, "Syntax error: incomplete statement");
      } else {
        auto end = t.find_first_of(" \t\n\r\f\v");
        report(Severity::Error, {node.from, to},
               "Unexpected \"" + std::string(t.substr(0, end)) + "\"");
      }
      return false;
    });
  }

  std::optional<std::vector<std::string>> qualified(const Node* node) const {
    if (!node) return std::nullopt;
    std::vector<std::string> out;
    for (const Node* id : node->all(NodeType::Identifier)) out.emplace_back(text(*id));
    return out;
  }

  /// Resolves a dotted reference from the innermost scope outwards.
  std::optional<Endpoint> resolve(const std::vector<std::string>& path,
                                  const std::vector<std::string>& scope) const {
    for (std::size_t i = scope.size() + 1; i-- > 0;) {
      std::vector<std::string> full(scope.begin(), scope.begin() + static_cast<std::ptrdiff_t>(i));
      full.insert(full.end(), path.begin(), path.end());
      std::string id = join(full, ".");
      if (result_.model.elements.contains(id)) return Endpoint{id, std::nullopt};
      if (full.size() > 1) {
        std::vector<std::string> owner_path(full.begin(), full.end() - 1);
        const Element* owner = result_.model.elements.find(join(owner_path, "."));
        const std::string& port = full.back();
        if (owner && std::any_of(owner->ports.begin(), owner->ports.end(),
                                 [&](const Port& p) { return p.id == port; }))
          return Endpoint{owner->id, port};
      }
    }
    return std::nullopt;
  }

  std::optional<ElementId> resolve_element(const Node& node, const std::vector<std::string>& scope) {
    auto path = qualified(&node);
    if (!path || path->empty()) return std::nullopt;
    auto found = resolve(*path, scope);
    if (!found || found->port) {
      report(Severity::Error, span(node), "Unknown element \"" + join(*path, ".") + "\"");
      return std::nullopt;
    }
    referenced(node, *found);
    return found->element;
  }

  /// Notes where a resolved reference is written, for renames.
  void referenced(const Node& node, const Endpoint& target) {
    auto& map = result_.source_map;
    std::size_t first = map.reference_parts.size();
    for (const Node& c : node.children)
      if (c.type == NodeType::Identifier) map.reference_parts.push_back(span(c));
    map.references.push_back({static_cast<std::uint32_t>(first),
                              static_cast<std::uint32_t>(map.reference_parts.size() - first), target});
  }

  void prop(const Node& node, Properties& into) {
    auto strings = node.all(NodeType::String);
    if (strings.size() >= 2) into.set(unquote(text(*strings[0])), unquote(text(*strings[1])));
  }

  // ── Libraries ──

  void use_decl(const Node& node) {
    const Node* spec = node.child(NodeType::String);
    if (!spec) return;
    auto [name, range] = parse_library_spec(unquote(text(*spec)));
    if (!is_library_name(name)) {
      report(Severity::Error, span(*spec), "\"" + name + "\" is not a library name");
      return;
    }
    const Node* alias_node = node.child(NodeType::Name);
    std::string alias = alias_node ? std::string(text(*alias_node)) : default_library_alias(name);
    if (result_.source_map.libraries.count(alias)) {
      report(Severity::Error, span(alias_node ? *alias_node : *spec),
             "Library alias \"" + alias + "\" is already used");
      return;
    }
    result_.source_map.libraries[alias] = span(node);
    if (!model().libraries) model().libraries.emplace();
    model().libraries->push_back({name, range, alias});
  }

  // ── Styles and themes ──

  [[nodiscard]] bool declared_theme(std::string_view name) const {
    return std::any_of(result_.model.themes.begin(), result_.model.themes.end(),
                       [&](const ThemeDef& t) { return t.name == name; });
  }

  /// Warns about a theme reference that is neither built in, declared, nor `alias.name` of a
  /// library the document uses (whether the library has it is checked where libraries resolve).
  void check_theme_ref(const std::string& value, Span at) {
    if (find_theme(value) || declared_theme(value)) return;
    auto dot = value.find('.');
    if (dot != std::string::npos && result_.source_map.libraries.count(value.substr(0, dot))) return;
    std::string known;
    for (const auto& n : theme_names()) known += (known.empty() ? "" : ", ") + n;
    for (const auto& t : result_.model.themes) known += ", " + t.name;
    report(Severity::Warning, at, "Unknown theme \"" + value + "\" (themes: " + known + ")");
  }

  /// The theme a `theme …` statement names, warning about names the renderer does not know.
  std::optional<std::string> theme_name(const Node& node) {
    auto path = qualified(node.child(NodeType::QualifiedName));
    if (!path || path->empty()) return std::nullopt;
    std::string value = join(*path, ".");
    check_theme_ref(value, span(*node.child(NodeType::QualifiedName)));
    return value;
  }

  /// `key value` lines of a block, checked against the keys it takes.
  Style block_values(const Node& body, const std::vector<std::string>& keys, std::string_view what) {
    Style out;
    for (const Node* decl : body.all(NodeType::StyleDecl)) {
      const Node* key = decl->child(NodeType::Name);
      auto value = decl_value(*decl);
      if (!key || !value) continue;
      std::string k(text(*key));
      if (std::find(keys.begin(), keys.end(), k) == keys.end())
        report(Severity::Warning, span(*key), "Unknown " + std::string(what) + " key \"" + k + "\"");
      out.set(std::move(k), std::move(*value));
    }
    return out;
  }

  /// The complete `key value` entries directly in a body.
  std::vector<EntrySource> entries(const Node& body) {
    std::vector<EntrySource> out;
    for (const Node* decl : body.all(NodeType::StyleDecl)) {
      const Node* key = decl->child(NodeType::Name);
      if (key && decl_value(*decl)) out.push_back({std::string(text(*key)), span(*decl)});
    }
    return out;
  }

  std::optional<std::string> decl_value(const Node& decl) {
    const Node* str = decl.child(NodeType::String);
    const Node* number = decl.child(NodeType::Number);
    const Node* word = decl.child(NodeType::Identifier);
    if (str) return unquote(text(*str));
    if (number) return std::string(text(*number));
    if (word) return text(*word) == "true" ? "1" : text(*word) == "false" ? "0" : std::string(text(*word));
    return std::nullopt;
  }

  void theme_def(const Node& node) {
    const Node* name_node = node.child(NodeType::Name);
    const Node* body = node.child(NodeType::ThemeBody);
    if (!name_node || !body) return;
    ThemeDef def;
    def.name = std::string(text(*name_node));
    if (find_theme(def.name)) {
      report(Severity::Error, span(*name_node), "\"" + def.name + "\" is a built-in theme; give yours another name");
      return;
    }
    if (declared_theme(def.name)) {
      report(Severity::Error, span(*name_node), "Theme \"" + def.name + "\" is already declared");
      return;
    }
    ThemeSource src;
    src.decl = span(node);
    src.name = span(*name_node);
    src.body = span(*body);
    src.entries = entries(*body);
    if (const Node* base = node.child(NodeType::ThemeBase)) {
      src.base = span(*base);
      if (auto path = qualified(base->child(NodeType::QualifiedName)); path && !path->empty())
        def.base = join(*path, ".");
    }
    def.values = block_values(*body, theme_keys(), "theme");
    for (const Node* n : body->all(NodeType::ThemeNode)) {
      const Node* kind = n->child(NodeType::Name);
      const Node* block = n->child(NodeType::StyleBody);
      if (!kind || !block) continue;
      Style look = block_values(*block, theme_node_keys(), "theme node");
      std::string k(text(*kind));
      src.nodes.push_back({k, span(*n), span(*block), entries(*block)});
      auto same = std::find_if(def.nodes.begin(), def.nodes.end(), [&](const auto& p) { return p.first == k; });
      if (same != def.nodes.end())
        for (const auto& [key, v] : look) same->second.set(key, v);
      else
        def.nodes.emplace_back(std::move(k), std::move(look));
    }
    model().themes.push_back(std::move(def));
    result_.source_map.themes.push_back(std::move(src));
  }

  /// Checks every declared theme's `extends` once all are known: unknown bases and cycles.
  void theme_cycles() {
    const auto& themes = result_.model.themes;
    for (std::size_t i = 0; i < themes.size(); ++i) {
      const ThemeDef& t = themes[i];
      if (!t.base) continue;
      const Node* base_node = nullptr;
      walk(result_.tree.root, [&](const Node& n) {
        if (base_node) return false;
        if (n.type == NodeType::ThemeBase && n.from >= result_.source_map.themes[i].decl.from &&
            n.to <= result_.source_map.themes[i].decl.to)
          base_node = &n;
        return true;
      });
      Span at = base_node ? span(*base_node) : result_.source_map.themes[i].name;
      check_theme_ref(*t.base, at);
      std::string current = *t.base;
      for (int step = 0; step <= static_cast<int>(themes.size()); ++step) {
        if (current == t.name) {
          report(Severity::Error, at, "Theme \"" + t.name + "\" extends itself");
          break;
        }
        auto next = std::find_if(themes.begin(), themes.end(), [&](const ThemeDef& d) { return d.name == current; });
        if (next == themes.end() || !next->base) break;
        current = *next->base;
      }
    }
  }

  void theme_decl(const Node& node) {
    auto name = theme_name(node);
    if (!name) return;
    if (result_.source_map.theme)
      report(Severity::Warning, span(node), "The theme is already set; the last `theme` wins");
    model().theme = std::move(name);
    result_.source_map.theme = span(node);
  }

  void style_block(const Node& node) {
    const Node* selector = node.child(NodeType::StyleSelector);
    const Node* body = node.child(NodeType::StyleBody);
    if (!selector || !body) return;
    StyleRule rule;
    if (selector->child(NodeType::Keyword)) rule.target = StyleTarget::Relationships;
    if (const Node* kind = selector->child(NodeType::ElementKind)) rule.kind = std::string(text(*kind));
    if (const Node* tag = selector->child(NodeType::Tag))
      if (const Node* value = tag->child(NodeType::String)) rule.tag = unquote(text(*value));
    for (const Node* decl : body->all(NodeType::StyleDecl)) {
      const Node* key = decl->child(NodeType::Name);
      if (!key) continue;
      std::string value;
      const Node* str = decl->child(NodeType::String);
      const Node* number = decl->child(NodeType::Number);
      const Node* word = decl->child(NodeType::Identifier);
      if (str)
        value = unquote(text(*str));
      else if (number)
        value = std::string(text(*number));
      else if (word)
        value = text(*word) == "true" ? "1" : text(*word) == "false" ? "0" : std::string(text(*word));
      else
        continue;
      rule.style.set(std::string(text(*key)), std::move(value));
    }
    model().styles.push_back(std::move(rule));
    result_.source_map.styles.push_back({span(node), span(*selector), span(*body), entries(*body)});
  }

  // ── Model ──

  void model_body(const Node& body, const std::vector<std::string>& scope,
                  const std::optional<GroupId>& group) {
    for (const auto& child : body.children) {
      switch (child.type) {
        case NodeType::ElementDecl: element(child, scope, group); break;
        case NodeType::Relationship: pending_.push_back({&child, scope}); break;
        case NodeType::Group: group_decl(child, scope, group); break;
        case NodeType::Port: port(child, scope); break;
        default: break;
      }
    }
  }

  /// The complete `tech`, `shape`, `tag`, `description`, `style` and `prop` clauses of a
  /// declaration, in source order.
  std::vector<ClauseSource> clauses(const Node& node) {
    std::vector<ClauseSource> out;
    for (const auto& c : node.children) {
      const Node* value = c.last_child();
      if (!value || value == c.first_child()) continue;
      using K = ClauseSource::Kind;
      switch (c.type) {
        case NodeType::Technology: out.push_back({K::Technology, span(c), {}}); break;
        case NodeType::Shape: out.push_back({K::Shape, span(c), {}}); break;
        case NodeType::Description: out.push_back({K::Description, span(c), {}}); break;
        case NodeType::StyleProp: out.push_back({K::Style, span(c), {}}); break;
        case NodeType::Tag: out.push_back({K::Tag, span(c), unquote(text(*value))}); break;
        case NodeType::Prop: {
          auto strings = c.all(NodeType::String);
          if (strings.size() >= 2) out.push_back({K::Prop, span(c), unquote(text(*strings[0]))});
          break;
        }
        default: break;
      }
    }
    return out;
  }

  void element(const Node& node, const std::vector<std::string>& scope,
               const std::optional<GroupId>& group) {
    const Node* name_node = node.child(NodeType::Name);
    const Node* kind_node = node.child(NodeType::ElementKind);
    if (!name_node || !kind_node) return;
    std::string local(text(*name_node));
    std::string id = join(with(scope, local), ".");
    std::string kind;
    for (const Node* part : kind_node->all(NodeType::Identifier)) {
      if (!kind.empty()) kind += ".";
      kind += text(*part);
    }
    if (auto dot = kind.find('.'); dot != std::string::npos) {
      // A library's kind: whether the library has it is the host's to check, once it is resolved.
      std::string alias = kind.substr(0, dot);
      if (!result_.source_map.libraries.count(alias))
        report(Severity::Warning, span(*kind_node), "No library is used as \"" + alias + "\"; add `use` for it");
    } else if (!kBuiltinKinds.count(kind)) {
      report(Severity::Warning, span(*kind_node), "Unknown element kind \"" + kind + "\"");
    }
    const Node* label_node = node.child(NodeType::Label);
    const Node* body = node.child(NodeType::ModelBody);

    Element e;
    e.id = id;
    e.kind = kind;
    e.name = label_node ? unquote(text(*label_node)) : local;
    if (!scope.empty()) e.parent = join(scope, ".");
    e.group = group;
    for (const auto& c : node.children) {
      const Node* value = c.last_child();
      if (!value || value == c.first_child()) continue;
      switch (c.type) {
        case NodeType::Technology: e.technology = unquote(text(*value)); break;
        case NodeType::Description: e.description = unquote(text(*value)); break;
        case NodeType::Shape: e.shape = std::string(text(*value)); break;
        case NodeType::Tag: e.tags.push_back(unquote(text(*value))); break;
        case NodeType::StyleProp: e.style = parse_style_text(unquote(text(*value))); break;
        case NodeType::Prop: prop(c, e.properties); break;
        default: break;
      }
    }

    // Ports live in the body; collect them first so the element is added complete.
    ports_.emplace_back();
    if (body)
      for (const Node* p : body->all(NodeType::Port)) port(*p, with(scope, local));
    e.ports = std::move(ports_.back());
    ports_.pop_back();

    if (!apply(op::AddElement{std::move(e)}, span(*name_node))) return;
    ElementSource src{span(node), span(*name_node), span(*kind_node), std::nullopt, std::nullopt, {}};
    if (label_node) src.label = span(*label_node);
    if (body) src.body = span(*body);
    src.clauses = clauses(node);
    result_.source_map.elements[id] = std::move(src);
    if (body) {
      // Groups do not cross element boundaries: children start outside any group.
      auto inner = with(scope, local);
      for (const auto& child : body->children) {
        if (child.type == NodeType::ElementDecl)
          element(child, inner, std::nullopt);
        else if (child.type == NodeType::Relationship)
          pending_.push_back({&child, inner});
        else if (child.type == NodeType::Group)
          group_decl(child, inner, std::nullopt);
      }
    }
  }

  void port(const Node& node, const std::vector<std::string>& scope) {
    const Node* name_node = node.child(NodeType::Name);
    if (!name_node) return;
    if (ports_.empty() || scope.empty()) {
      report(Severity::Error, span(node), "Ports must be declared inside an element");
      return;
    }
    auto& current = ports_.back();
    std::string id(text(*name_node));
    if (std::any_of(current.begin(), current.end(), [&](const Port& p) { return p.id == id; })) {
      report(Severity::Error, span(*name_node), "Port \"" + id + "\" already exists");
      return;
    }
    Port p{id, std::nullopt};
    if (const Node* side = node.child(NodeType::PortSide)) {
      auto s = text(*side);
      p.side = s == "north" ? PortSide::North
               : s == "south" ? PortSide::South
               : s == "east"  ? PortSide::East
                              : PortSide::West;
    }
    current.push_back(std::move(p));
  }

  void group_decl(const Node& node, const std::vector<std::string>& scope,
                  const std::optional<GroupId>& parent) {
    const Node* label_node = node.child(NodeType::Label);
    const Node* body = node.child(NodeType::ModelBody);
    if (!label_node || !body) return;
    std::string name = unquote(text(*label_node));
    std::string base = parent ? *parent + "/" + name : join(with(scope, name), "/");
    std::string id = base;
    for (int n = 2; model().groups.contains(id); ++n) id = base + "#" + std::to_string(n);
    if (!apply(op::AddGroup{{id, name, parent}}, span(*label_node))) return;
    result_.source_map.groups[id] = {span(node), span(*body)};
    model_body(*body, scope, id);
  }

  void relationship(const Node& node, const std::vector<std::string>& scope) {
    auto names = node.all(NodeType::QualifiedName);
    const Node* arrow_node = node.child(NodeType::Arrow);
    if (names.size() < 2 || !arrow_node) return;
    std::vector<Endpoint> ends;
    for (const Node* end : {names[0], names[1]}) {
      auto path = *qualified(end);
      auto found = resolve(path, scope);
      if (!found) {
        report(Severity::Error, span(*end), "Unknown element \"" + join(path, ".") + "\"");
        return;
      }
      referenced(*end, *found);
      ends.push_back(*found);
    }
    auto arrow = text(*arrow_node);
    const Node* label_node = node.child(NodeType::Label);
    Relationship r;
    r.source = ends[0];
    r.target = ends[1];
    for (const auto& c : node.children) {
      const Node* value = c.last_child();
      if (!value || value == c.first_child()) continue;
      switch (c.type) {
        case NodeType::Technology: r.technology = unquote(text(*value)); break;
        case NodeType::Tag: r.tags.push_back(unquote(text(*value))); break;
        case NodeType::StyleProp: r.style = parse_style_text(unquote(text(*value))); break;
        case NodeType::Prop: prop(c, r.properties); break;
        case NodeType::Shape:
        case NodeType::Description:
          report(Severity::Warning, span(c),
                 "\"" + std::string(text(*c.first_child())) + "\" is ignored on relationships");
          break;
        default: break;
      }
    }
    // `name = a -> b` names the relationship; otherwise the id comes from its endpoints.
    const Node* id_node = node.child(NodeType::Name);
    if (id_node) {
      r.id = std::string(text(*id_node));
    } else {
      int index = 1;
      while (model().relationships.contains(relationship_id(r.source, r.target, index))) ++index;
      r.id = relationship_id(r.source, r.target, index);
    }
    r.line = arrow == "-->" ? LineStyle::Dashed : LineStyle::Solid;
    r.bidirectional = arrow == "<->";
    if (label_node) r.label = unquote(text(*label_node));
    std::string id = r.id;
    if (apply(op::AddRelationship{std::move(r)}, id_node ? span(*id_node) : span(node))) {
      RelationshipSource src;
      src.decl = span(node);
      src.target = span(*names[1]);
      src.source = span(*names[0]);
      src.arrow = span(*arrow_node);
      src.scope = scope;
      if (label_node) src.label = span(*label_node);
      if (id_node) src.id = span(*id_node);
      src.clauses = clauses(node);
      result_.source_map.relationships[id] = std::move(src);
    }
  }

  // ── Views ──

  void view_block(const Node& node) {
    const Node* name_node = node.child(NodeType::Name);
    const Node* body = node.child(NodeType::ViewBody);
    if (!name_node || !body) return;
    std::string id(text(*name_node));
    std::optional<ElementId> scope_id;
    if (const Node* scope_node = node.child(NodeType::QualifiedName)) {
      scope_id = resolve_element(*scope_node, {});
      if (!scope_id) return;
    }
    std::vector<std::string> scope;
    if (scope_id) {
      std::size_t start = 0;
      while (true) {
        auto dot = scope_id->find('.', start);
        scope.push_back(scope_id->substr(start, dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
      }
    }

    View view;
    view.id = id;
    view.scope = scope_id;
    view.include.clear();
    ViewSource src;
    bool saw_include = false;
    bool autonumber = false;

    for (const auto& item : body->children) {
      switch (item.type) {
        case NodeType::Include:
        case NodeType::Exclude: {
          bool include = item.type == NodeType::Include;
          if (include) saw_include = true;
          auto& into = include ? view.include : view.exclude;
          for (const auto& s : item.children) {
            std::optional<std::string> selector;
            if (s.type == NodeType::Wildcard)
              selector = "*";
            else if (s.type == NodeType::QualifiedName)
              selector = resolve_element(s, scope);
            if (!selector) continue;
            into.push_back(*selector);
            src.selectors.push_back({include, *selector, span(s), span(item)});
          }
          break;
        }
        case NodeType::LayoutStmt: {
          LayoutOptions layout;
          if (const Node* algorithm = item.child(NodeType::Algorithm)) {
            layout.algorithm = std::string(text(*algorithm));
            if (!is_layout_algorithm(layout.algorithm)) {
              std::string known;
              for (std::string_view a : kLayoutAlgorithms) known += (known.empty() ? "" : ", ") + std::string(a);
              report(Severity::Warning, span(*algorithm),
                     "Unknown layout \"" + layout.algorithm + "\"; using layered (known: " + known + ")");
            }
          }
          if (const Node* opt = item.child(NodeType::DirectionOption))
            if (const Node* dir = opt->child(NodeType::Direction)) {
              auto d = text(*dir);
              layout.direction = d == "right" ? Direction::Right
                                 : d == "left" ? Direction::Left
                                 : d == "up"   ? Direction::Up
                                               : Direction::Down;
            }
          if (const Node* opt = item.child(NodeType::SpacingOption))
            if (const Node* n = opt->child(NodeType::Number)) layout.spacing = to_number(text(*n));
          view.layout = std::move(layout);
          src.layout = span(item);
          break;
        }
        case NodeType::ThemeDecl: {
          if (auto name = theme_name(item)) {
            if (src.theme) report(Severity::Warning, span(item), "The view's theme is already set; the last `theme` wins");
            view.theme = std::move(name);
            src.theme = span(item);
          }
          break;
        }
        case NodeType::Sequence: {
          const Node* seq = item.child(NodeType::SeqBody);
          view.sequence = seq ? sequence_items(*seq, scope, autonumber) : std::vector<SequenceItem>{};
          break;
        }
        case NodeType::ViewNote:
        case NodeType::ViewText:
        case NodeType::ViewBox: {
          if (auto d = decoration(item, scope)) {
            if (d->name) {
              bool taken = false;
              for (const auto& other : view.decorations) taken = taken || other.name == d->name;
              if (taken) {
                report(Severity::Error, span(*item.child(NodeType::Name)),
                       "Duplicate decoration \"" + *d->name + "\" in view \"" + id + "\"");
                break;
              }
            }
            view.decorations.push_back(std::move(*d));
            src.decorations.push_back(span(item));
          }
          break;
        }
        case NodeType::Pin:
        case NodeType::Align:
        case NodeType::Order:
        case NodeType::Route:
        case NodeType::Resize:
        case NodeType::Rotate: {
          if (auto h = hint(item, scope)) {
            view.hints.push_back(std::move(*h));
            src.hints.push_back(span(item));
          }
          break;
        }
        default: break;
      }
    }
    if (!saw_include) view.include = {"*"};
    if (autonumber) view.autonumber = true;
    if (apply(op::AddView{std::move(view)}, span(*name_node))) {
      src.decl = span(node);
      src.name = span(*name_node);
      src.body = span(*body);
      result_.source_map.views[id] = std::move(src);
    }
  }

  std::vector<SequenceItem> sequence_items(const Node& body, const std::vector<std::string>& scope,
                                           bool& autonumber) {
    std::vector<SequenceItem> items;
    auto refs = [&](const Node& n) -> std::optional<std::vector<ElementId>> {
      std::vector<ElementId> ids;
      for (const Node* q : n.all(NodeType::QualifiedName)) {
        auto id = resolve_element(*q, scope);
        if (!id) return std::nullopt;
        ids.push_back(*id);
      }
      return ids;
    };
    auto label = [&](const Node& n) -> std::optional<std::string> {
      const Node* l = n.child(NodeType::Label);
      if (!l) return std::nullopt;
      return unquote(text(*l));
    };
    for (const auto& item : body.children) {
      switch (item.type) {
        case NodeType::Message: {
          auto ids = refs(item);
          const Node* arrow = item.child(NodeType::MessageArrow);
          if (!ids || ids->size() < 2 || !arrow) break;
          auto a = text(*arrow);
          items.emplace_back(SequenceMessage{(*ids)[0], (*ids)[1], label(item),
                                             a == "->>"   ? MessageStyle::Async
                                             : a == "-->" ? MessageStyle::Reply
                                                          : MessageStyle::Sync});
          break;
        }
        case NodeType::Activate:
        case NodeType::Deactivate: {
          auto ids = refs(item);
          if (ids && !ids->empty())
            items.emplace_back(SequenceActivation{item.type == NodeType::Activate, (*ids)[0]});
          break;
        }
        case NodeType::SeqNote: {
          auto ids = refs(item);
          const Node* side = item.child(NodeType::NoteSide);
          if (!ids || ids->empty()) break;
          NotePosition position = !side                     ? NotePosition::Over
                                  : text(*side) == "left"   ? NotePosition::Left
                                                            : NotePosition::Right;
          items.emplace_back(SequenceNote{position, *ids, label(item).value_or("")});
          break;
        }
        case NodeType::Divider: items.emplace_back(SequenceDivider{label(item)}); break;
        case NodeType::Delay: items.emplace_back(SequenceDelay{label(item)}); break;
        case NodeType::Ref: {
          auto ids = refs(item);
          if (ids && !ids->empty()) items.emplace_back(SequenceRef{*ids, label(item).value_or("")});
          break;
        }
        case NodeType::Autonumber: autonumber = true; break;
        case NodeType::Frame: {
          const Node* kind = item.child(NodeType::FrameKind);
          if (!kind) break;
          SequenceFrame frame;
          auto k = text(*kind);
          frame.frame = k == "alt"        ? FrameKind::Alt
                        : k == "opt"      ? FrameKind::Opt
                        : k == "loop"     ? FrameKind::Loop
                        : k == "par"      ? FrameKind::Par
                        : k == "critical" ? FrameKind::Critical
                        : k == "break"    ? FrameKind::Break
                                          : FrameKind::Group;
          std::optional<std::string> pending;
          for (const auto& c : item.children) {
            if (c.type == NodeType::Label)
              pending = unquote(text(c));
            else if (c.type == NodeType::Else)
              pending.reset();
            else if (c.type == NodeType::SeqBody) {
              frame.sections.push_back({pending, sequence_items(c, scope, autonumber)});
              pending.reset();
            }
          }
          items.emplace_back(std::move(frame));
          break;
        }
        default: break;
      }
    }
    return items;
  }

  std::pair<std::optional<double>, std::optional<double>> coord(const Node* node) const {
    if (!node) return {};
    auto numbers = node->all(NodeType::Number);
    std::optional<double> a, b;
    if (numbers.size() > 0) a = to_number(text(*numbers[0]));
    if (numbers.size() > 1) b = to_number(text(*numbers[1]));
    return {a, b};
  }

  std::optional<Decoration> decoration(const Node& item, const std::vector<std::string>& scope) {
    Decoration d;
    d.kind = item.type == NodeType::ViewNote   ? DecorationKind::Note
             : item.type == NodeType::ViewText ? DecorationKind::Text
                                               : DecorationKind::Box;
    if (const Node* n = item.child(NodeType::Name)) d.name = std::string(text(*n));
    if (const Node* l = item.child(NodeType::Label)) d.text = unquote(text(*l));
    if (d.kind != DecorationKind::Box && !d.text) return std::nullopt;
    if (const Node* placement = item.child(NodeType::Placement)) {
      const Node* ref = item.child(NodeType::QualifiedName);
      if (!ref) return std::nullopt;
      auto id = resolve_element(*ref, scope);
      if (!id) return std::nullopt;
      auto word = text(*placement->first_child());
      d.placement = word == "below"   ? Placement::Below
                    : word == "above" ? Placement::Above
                    : word == "left"  ? Placement::LeftOf
                                      : Placement::RightOf;
      d.reference = *id;
    }
    if (const Node* around = item.child(NodeType::Around)) {
      for (const Node* q : around->all(NodeType::QualifiedName)) {
        auto id = resolve_element(*q, scope);
        if (!id) return std::nullopt;
        d.around.push_back(*id);
      }
      if (d.around.empty()) return std::nullopt;
    }
    if (const Node* at = item.child(NodeType::Coord)) {
      auto [x, y] = coord(at);
      if (!x || !y) return std::nullopt;
      d.at = Point2{*x, *y};
      const Node* size = item.child(NodeType::Size);
      auto [w, h] = coord(size ? size->child(NodeType::Coord) : nullptr);
      if (w && h) {
        d.width = w;
        d.height = h;
      }
    }
    if (d.kind == DecorationKind::Box && d.around.empty() && !(d.at && d.width)) return std::nullopt;
    if (const Node* st = item.child(NodeType::StyleProp))
      if (const Node* value = st->child(NodeType::String)) d.style = parse_style_text(unquote(text(*value)));
    return d;
  }

  std::optional<LayoutHint> hint(const Node& item, const std::vector<std::string>& scope) {
    std::vector<ElementId> ids;
    for (const Node* ref : item.all(NodeType::QualifiedName)) {
      auto id = resolve_element(*ref, scope);
      if (!id) return std::nullopt;
      ids.push_back(*id);
    }
    if (item.type == NodeType::Pin) {
      if (const Node* placement = item.child(NodeType::Placement)) {
        auto word = text(*placement->first_child());
        if (ids.size() < 2) return std::nullopt;
        Placement p = word == "below"   ? Placement::Below
                      : word == "above" ? Placement::Above
                      : word == "left"  ? Placement::LeftOf
                                        : Placement::RightOf;
        return PinHint{ids[0], p, ids[1]};
      }
      auto [x, y] = coord(item.child(NodeType::Coord));
      if (ids.empty() || !x || !y) return std::nullopt;
      PinAtHint h{ids[0], *x, *y, std::nullopt, std::nullopt};
      const Node* size = item.child(NodeType::Size);
      auto [w, hgt] = coord(size ? size->child(NodeType::Coord) : nullptr);
      if (w && hgt) {
        h.width = w;
        h.height = hgt;
      }
      return h;
    }
    if (item.type == NodeType::Resize) {
      auto [w, h] = coord(item.child(NodeType::Coord));
      if (ids.empty() || !w || !h) return std::nullopt;
      if (*w <= 0 || *h <= 0) {
        report(Severity::Error, span(*item.child(NodeType::Coord)), "A size must be wider and taller than 0");
        return std::nullopt;
      }
      return SizeHint{ids[0], *w, *h};
    }
    if (item.type == NodeType::Rotate) {
      const Node* degrees = item.child(NodeType::Number);
      if (ids.empty() || !degrees) return std::nullopt;
      return RotateHint{ids[0], to_number(text(*degrees))};
    }
    if (item.type == NodeType::Route) {
      const Node* id_node = item.child(NodeType::String);
      if (!id_node) return std::nullopt;
      std::string rel = unquote(text(*id_node));
      if (!model().relationships.contains(rel)) {
        report(Severity::Error, span(*id_node), "Unknown relationship \"" + rel + "\"");
        return std::nullopt;
      }
      auto point = [&](const Node* n) -> std::optional<Point2> {
        auto [px, py] = coord(n);
        if (!px || !py) return std::nullopt;
        return Point2{*px, *py};
      };
      RouteHint h{rel, {}, std::nullopt, std::nullopt};
      for (const Node* c : item.all(NodeType::Coord))
        if (auto p = point(c)) h.points.push_back(*p);
      const Node* exit = item.child(NodeType::Exit);
      const Node* entry = item.child(NodeType::Entry);
      h.exit = point(exit ? exit->child(NodeType::Coord) : nullptr);
      h.entry = point(entry ? entry->child(NodeType::Coord) : nullptr);
      return h;
    }
    if (item.type == NodeType::Align) {
      const Node* axis = item.child(NodeType::Axis);
      return AlignHint{ids, axis && text(*axis) == "vertically" ? Axis::Vertical : Axis::Horizontal};
    }
    return OrderHint{ids};
  }

  std::string_view src_;
  std::vector<std::uint32_t> line_starts_;
  ParseResult result_;
  std::vector<Pending> pending_;
  /// Ports declared inside an element body, gathered before the element is added.
  std::vector<std::vector<Port>> ports_;
};

}  // namespace

ParseResult parse(std::string_view source) { return Lowering(source).run(); }

}  // namespace dd::dsl
