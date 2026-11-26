// Port of packages/language-server/src/features.ts.

#include "diadraw/lsp/features.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <string_view>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::lsp {

using js::Regex;

namespace {

const std::vector<std::string> kKinds{"person", "system",  "container",  "component", "database", "process", "decision",
                                      "start",  "end",     "note",       "class",     "object",   "event",   "task",
                                      "subprocess", "gateway", "data", "annotation", "pool",     "lane"};
const std::vector<std::string> kShapes{"cylinder", "person", "queue", "note", "decision", "stadium", "circle", "hexagon"};
const std::vector<std::string> kAlgorithms(std::begin(dd::kLayoutAlgorithms), std::end(dd::kLayoutAlgorithms));
const std::vector<std::string> kDirections{"right", "down", "left", "up"};
const std::vector<std::string> kModelKeywords{"group", "port"};
const std::vector<std::string> kPropertyKeywords{"tech", "shape", "tag", "description", "style", "prop"};
const std::vector<std::string> kViewKeywords{"include", "exclude", "layout", "pin",    "align",
                                             "order",   "route",   "sequence", "theme",
                                             "note",    "text",    "box",     "size",  "rotate"};
const std::vector<std::string> kTopKeywords{"use", "theme", "style", "model", "view"};
// Keys of `style` blocks, as for inline `style "…"` (see docs/dsl.md).
const std::vector<std::string> kElementStyleKeys{"fill",     "stroke",    "strokeWidth", "dashed",    "dashPattern",
                                                 "rounded",  "opacity",   "fontColor",   "fontSize",  "fontStyle",
                                                 "bold",     "italic",    "align",       "verticalAlign"};
const std::vector<std::string> kRelationshipStyleKeys{
    "stroke",   "strokeWidth", "dashed",  "dashPattern", "double",    "curved",   "rounded",
    "edgeStyle", "startArrow", "endArrow", "startFill",  "endFill",   "fontColor", "fontSize"};

constexpr auto kEnds = std::to_array<std::string_view>({
    "classic",    "block",     "open",        "async",  "openAsync",   "doubleBlock",  "diamond",
    "oval",       "box",       "circlePlus",  "halfCircle", "baseDash", "dash",        "cross",
    "ERone",      "ERmandOne", "ERzeroToOne", "ERmany", "ERoneToMany", "ERzeroToMany", "none"});
constexpr auto kEdgeStyles = std::to_array<std::string_view>({"orthogonalEdgeStyle"});
constexpr auto kAligns = std::to_array<std::string_view>({"left", "center", "right"});
constexpr auto kVerticalAligns = std::to_array<std::string_view>({"top", "middle", "bottom"});
constexpr auto kBooleans = std::to_array<std::string_view>({"true", "false"});

/// Values a style key takes, when it takes a fixed set; none when it takes any value.
std::span<const std::string_view> style_values(std::string_view key) {
  if (key == "startArrow" || key == "endArrow") return kEnds;
  if (key == "edgeStyle") return kEdgeStyles;
  if (key == "align") return kAligns;
  if (key == "verticalAlign") return kVerticalAligns;
  if (key == "dashed" || key == "rounded" || key == "bold" || key == "italic" || key == "startFill" ||
      key == "endFill" || key == "double" || key == "curved")
    return kBooleans;
  return {};
}

/// Shapes listed one by one only up to this many; past it, `alias.` completes first.
constexpr std::size_t kMaxShapes = 400;

template <class Words>
std::vector<Completion> words(const Words& list, const char* kind) {
  std::vector<Completion> out;
  out.reserve(std::size(list));
  for (const auto& w : list) out.push_back({std::string(w), kind, std::nullopt, std::nullopt});
  return out;
}

std::vector<Completion> words(const std::vector<std::string>& list, const char* kind) {
  return words<std::vector<std::string>>(list, kind);
}

bool is_ref_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

/// Element ids enclosing an offset, innermost last (the scope a reference resolves in).
std::vector<std::string> scope_at(const dsl::ParseResult& result, std::size_t offset) {
  for (const auto& view : result.model.views) {
    auto it = result.source_map.views.find(view.id);
    if (it == result.source_map.views.end()) continue;
    const auto& body = it->second.body;
    if (body.from < offset && offset < body.to) {
      if (view.scope) return {*view.scope};
      break;
    }
  }
  std::vector<std::string> enclosing;
  for (const auto& e : result.model.elements) {
    auto it = result.source_map.elements.find(e.id);
    if (it == result.source_map.elements.end() || !it->second.body) continue;
    if (it->second.body->from < offset && offset < it->second.body->to) enclosing.push_back(e.id);
  }
  std::stable_sort(enclosing.begin(), enclosing.end(),
                   [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
  return enclosing;
}

std::vector<Completion> library_shapes(const LibraryContext& lib, const std::optional<std::string>& alias) {
  std::vector<Completion> out;
  for (const auto& p : lib.set.palette())
    if ((p.type == "shape" || p.type == "composite") && (!alias || p.alias == *alias))
      out.push_back({p.ref, "value", p.label + " — " + p.library, std::nullopt});
  return out;
}

/// The element kinds the libraries add, as `alias.kind`.
std::vector<Completion> library_kinds(const LibraryContext& lib, const std::optional<std::string>& alias) {
  std::vector<Completion> out;
  for (const auto& p : lib.set.palette())
    if (p.type == "kind" && (!alias || p.alias == *alias))
      out.push_back({p.ref, "kind", p.label + " — " + p.library + (p.description ? " — " + *p.description : ""),
                     std::nullopt});
  return out;
}

/// A fragment as an LSP snippet: each parameter becomes a placeholder with its default.
std::string fragment_snippet(const LibraryContext& lib, const std::string& alias, const std::string& id) {
  const Json* f = lib.set.fragment(alias, id);
  std::vector<std::pair<std::string, std::string>> params;  // name, default
  if (const Json* ps = f ? f->get("params") : nullptr; ps && ps->is_array())
    for (const auto& p : ps->as_array()) {
      const Json* name = p.get("name");
      const Json* def = p.get("default");
      std::string n = name && name->is_string() ? name->as_string() : "";
      params.emplace_back(n, def && def->is_string() ? def->as_string() : n);
    }
  const Json* text = f ? f->get("text") : nullptr;
  static const Regex kDollar("\\$", "g");
  static const Regex kPlaceholder("\\\\\\$\\{(\\w+)\\}", "g");
  std::string escaped = kDollar.replace(text && text->is_string() ? text->as_string() : "", "\\$$", true);
  return kPlaceholder.replace(
      escaped,
      [&](const js::Match& m) -> std::string {
        std::string name = m.str(1);
        if (name == "alias") return alias;
        for (std::size_t i = 0; i < params.size(); ++i)
          if (params[i].first == name) return "${" + std::to_string(i + 1) + ":" + params[i].second + "}";
        return m.str(0);
      },
      true);
}

}  // namespace

std::vector<Symbol> document_symbols(const dsl::ParseResult& result) {
  const Model& model = result.model;
  std::vector<Symbol> roots;
  // Elements nest under their parents; parents come first in the model.
  std::map<std::string, std::vector<std::size_t>, std::less<>> path;  // id → indexes into the tree
  auto at = [&](const std::vector<std::size_t>& p) -> Symbol& {
    Symbol* s = &roots[p[0]];
    for (std::size_t i = 1; i < p.size(); ++i) s = &s->children[p[i]];
    return *s;
  };
  for (const auto& el : model.elements) {
    auto it = result.source_map.elements.find(el.id);
    if (it == result.source_map.elements.end()) continue;
    Symbol sym{el.name, el.kind + " " + el.id, "element", it->second.decl, it->second.name, {}};
    std::vector<std::size_t> parent_path;
    if (el.parent)
      if (auto p = path.find(*el.parent); p != path.end()) parent_path = p->second;
    if (!parent_path.empty()) {
      auto& siblings = at(parent_path).children;
      siblings.push_back(std::move(sym));
      parent_path.push_back(siblings.size() - 1);
      path[el.id] = parent_path;
    } else {
      roots.push_back(std::move(sym));
      path[el.id] = {roots.size() - 1};
    }
  }
  std::vector<Symbol> symbols;
  if (result.source_map.model) {
    symbols.push_back({"model", "", "model", *result.source_map.model, *result.source_map.model, std::move(roots)});
  } else {
    symbols = std::move(roots);
  }
  for (const auto& view : model.views) {
    auto it = result.source_map.views.find(view.id);
    if (it == result.source_map.views.end()) continue;
    symbols.push_back({"view " + view.id, view.scope ? "of " + *view.scope : "", "view", it->second.decl,
                       it->second.decl, {}});
  }
  return symbols;
}

std::optional<Reference> reference_at(std::string_view text, std::size_t offset) {
  std::size_t from = std::min(offset, text.size());
  std::size_t to = from;
  while (from > 0 && is_ref_char(text[from - 1])) --from;
  while (to < text.size() && is_ref_char(text[to])) ++to;
  std::string_view ref = text.substr(from, to - from);
  while (!ref.empty() && ref.front() == '.') ref.remove_prefix(1);
  while (!ref.empty() && ref.back() == '.') ref.remove_suffix(1);
  if (ref.empty()) return std::nullopt;
  char c = ref.front();
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) return std::nullopt;
  return Reference{std::string(ref), {static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to)}};
}

const Element* resolve_reference(const dsl::ParseResult& result, const std::string& ref, std::size_t offset) {
  auto scope = scope_at(result, offset);
  std::vector<std::string> parts;
  if (!scope.empty()) {
    std::string s = scope.back();
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
      if (i == s.size() || s[i] == '.') {
        parts.push_back(s.substr(start, i - start));
        start = i + 1;
      }
  }
  // A port reference: resolve the owner. As in the TypeScript (`ref.slice(0, ref.lastIndexOf('.'))`),
  // a name without a dot loses its last character and is tried again (a quirk kept for parity).
  // The TypeScript recurses; this loops, since a long word (an embedded image) would overflow the
  // stack.
  for (std::string current = ref; !current.empty();) {
    for (std::size_t i = parts.size() + 1; i-- > 0;) {
      std::string id;
      for (std::size_t k = 0; k < i; ++k) id += parts[k] + ".";
      id += current;
      if (const Element* el = result.model.elements.find(id)) return el;
    }
    auto dot = current.rfind('.');
    current.resize(dot == std::string::npos ? current.size() - 1 : dot);
  }
  return nullptr;
}

std::optional<dsl::Span> definition(std::string_view text, std::size_t offset, const dsl::ParseResult& result) {
  auto at = reference_at(text, offset);
  if (!at) return std::nullopt;
  // Pointing at a dotted name resolves the prefix up to the cursor's segment.
  std::size_t segment_end = text.find('.', offset);
  std::string ref = segment_end != std::string_view::npos && segment_end < at->span.to
                        ? std::string(text.substr(at->span.from, segment_end - at->span.from))
                        : at->ref;
  const Element* el = resolve_reference(result, ref, offset);
  if (!el) return std::nullopt;
  auto it = result.source_map.elements.find(el->id);
  return it == result.source_map.elements.end() ? std::nullopt : std::optional(it->second.name);
}

std::optional<Hover> hover(std::string_view text, std::size_t offset, const dsl::ParseResult& result) {
  auto at = reference_at(text, offset);
  if (!at) return std::nullopt;
  const Element* el = resolve_reference(result, at->ref, offset);
  if (!el) return std::nullopt;
  std::vector<std::string> lines{"**" + el->name + "** `" + el->kind + "`", "", "id: `" + el->id + "`"};
  if (el->technology && !el->technology->empty()) lines.push_back("technology: " + *el->technology);
  if (el->description && !el->description->empty()) {
    lines.emplace_back("");
    lines.push_back(*el->description);
  }
  std::size_t rels = 0;
  for (const auto& r : result.model.relationships)
    if (r.source.element == el->id || r.target.element == el->id) ++rels;
  if (rels) {
    lines.emplace_back("");
    lines.push_back(std::to_string(rels) + " relationship" + (rels > 1 ? "s" : ""));
  }
  std::string markdown;
  for (std::size_t i = 0; i < lines.size(); ++i) markdown += (i ? "\n" : "") + lines[i];
  return Hover{markdown, at->span};
}

std::vector<Completion> completions(std::string_view text, std::size_t offset, const dsl::ParseResult& result,
                                    const LibraryContext* libraries) {
  offset = std::min(offset, text.size());
  std::size_t line_start = 0;
  if (offset > 0) {
    auto nl = text.rfind('\n', offset - 1);
    line_start = nl == std::string_view::npos ? 0 : nl + 1;
  }
  const std::string before(text.substr(line_start, offset - line_start));
  auto elements = [&] {
    std::vector<Completion> out;
    for (const auto& e : result.model.elements) out.push_back({e.id, "element", e.kind + " — " + e.name, std::nullopt});
    return out;
  };
  static const Regex kUse("^\\s*use\\s+\"[^\"]*$");
  static const Regex kKindRef("=\\s*(\\w+)\\.\\w*$");
  static const Regex kKind("=\\s*\\w*$");
  static const Regex kShapeRef("\\bshape\\s+(\\w+)\\.\\w*$");
  static const Regex kShape("\\bshape\\s+\\w*$");
  static const Regex kLayout("\\blayout\\s+\\w*$");
  static const Regex kDirection("\\bdirection\\s+\\w*$");
  static const Regex kArrow("(->|-->|<->)\\s*[\\w.]*$");
  static const Regex kPin("\\bpin\\s+[\\w.]+\\s+[\\w.]*$");
  static const Regex kSelectors("\\b(include|exclude|pin|align|order|of|around|below|above)\\b[^\"]*$");
  static const Regex kNoteAnchor("^\\s*(note|text)(\\s+\\w+)?\\s+\"[^\"]*\"\\s+\\w*$");
  static const Regex kBoxAnchor("^\\s*box(\\s+\\w+)?(\\s+\"[^\"]*\")?\\s+\\w*$");
  static const Regex kProperty("^\\s*[\\w.]+\\s+\\S.*$");
  static const Regex kTheme("(^\\s*theme|\\bextends)\\s+[\\w.]*$");
  static const Regex kThemeNode("^\\s*node\\s+\\w*$");
  static const Regex kStyleSelector("^\\s*style\\s+\\w*$");
  static const Regex kStyleValue("^\\s*(\\w+)\\s+\\w*$");
  static const Regex kStyleKey("^\\s*\\w*$");

  if (kUse.test(before)) {
    std::vector<Completion> out;
    if (libraries)
      for (const auto& l : libraries->available) {
        std::string major = l.version.substr(0, l.version.find('.'));
        out.push_back({l.name, "value", l.version + (l.description ? " — " + *l.description : ""),
                       l.name + "@^" + major});
      }
    return out;
  }
  if (kTheme.test(before)) {
    std::vector<Completion> out;
    for (const auto& name : theme_names()) out.push_back({name, "value", "built-in theme", std::nullopt});
    for (const auto& t : result.model.themes) out.push_back({t.name, "value", "theme", std::nullopt});
    if (libraries)
      for (const auto& ref : libraries->set.theme_refs()) out.push_back({ref, "value", "library theme", std::nullopt});
    return out;
  }
  // Inside a theme declaration: its keys and `node` blocks, then fill/stroke/text in a block.
  for (const auto& theme : result.source_map.themes) {
    if (!(theme.body.from < offset && offset <= theme.body.to)) continue;
    if (offset == theme.body.to && text[theme.body.to - 1] == '}') break;
    for (const auto& n : theme.nodes)
      if (const auto& node = n.body;
          node.from < offset && offset <= node.to && !(offset == node.to && text[node.to - 1] == '}'))
        return kStyleKey.test(before) ? words(theme_node_keys(), "property") : std::vector<Completion>{};
    if (kThemeNode.test(before)) {
      auto out = words({"default", "external"}, "keyword");
      auto kinds = words(kKinds, "kind");
      out.insert(out.end(), kinds.begin(), kinds.end());
      return out;
    }
    if (kStyleKey.test(before)) {
      auto out = words(theme_keys(), "property");
      out.push_back({"node", "keyword", std::nullopt, std::nullopt});
      return out;
    }
    return {};
  }
  // Inside a `style` block: keys at the start of a line, then the key's values.
  for (std::size_t i = 0; i < result.source_map.styles.size() && i < result.model.styles.size(); ++i) {
    const auto& body = result.source_map.styles[i].body;
    if (!(body.from < offset && offset <= body.to)) continue;
    if (offset == body.to && body.to > body.from && text[body.to - 1] == '}') break;
    if (auto m = kStyleValue.exec(before)) {
      return words(style_values(m->str(1)), "value");
    }
    if (kStyleKey.test(before))
      return words(result.model.styles[i].target == StyleTarget::Relationships ? kRelationshipStyleKeys
                                                                                : kElementStyleKeys,
                   "property");
    return {};
  }
  bool in_model_body =
      result.source_map.model && result.source_map.model->from < offset && offset <= result.source_map.model->to;
  if (kStyleSelector.test(before) && !in_model_body) {
    auto out = words({"*", "relationship", "tag"}, "keyword");
    auto kinds = words(kKinds, "kind");
    out.insert(out.end(), kinds.begin(), kinds.end());
    return out;
  }
  if (auto m = kKindRef.exec(before); m && libraries) return library_kinds(*libraries, m->str(1));
  if (kKind.test(before)) {
    auto out = words(kKinds, "kind");
    if (libraries) {
      auto added = library_kinds(*libraries, std::nullopt);
      out.insert(out.end(), added.begin(), added.end());
    }
    return out;
  }
  if (auto m = kShapeRef.exec(before); m && libraries) return library_shapes(*libraries, m->str(1));
  if (kShape.test(before)) {
    auto out = words(kShapes, "value");
    if (libraries) {
      auto all = library_shapes(*libraries, std::nullopt);
      if (all.size() <= kMaxShapes) out.insert(out.end(), all.begin(), all.end());
      else
        for (const auto& a : libraries->set.aliases()) out.push_back({a + ".", "value", std::nullopt, std::nullopt});
    }
    return out;
  }
  if (kLayout.test(before)) return words(kAlgorithms, "value");
  if (kDirection.test(before)) return words(kDirections, "value");
  if (kArrow.test(before)) return elements();
  if (kPin.test(before)) return words({"below", "above", "left of", "right of", "at"}, "keyword");
  bool in_view = false;
  for (const auto& [_, s] : result.source_map.views) in_view = in_view || (s.body.from < offset && offset <= s.body.to);
  if (in_view && kNoteAnchor.test(before)) return words({"below", "above", "left of", "right of", "at", "style"}, "keyword");
  if (in_view && kBoxAnchor.test(before)) return words({"around", "at"}, "keyword");
  if (kSelectors.test(before)) return elements();

  bool in_model = result.source_map.model && result.source_map.model->from < offset && offset <= result.source_map.model->to;
  if (in_view) return words(kViewKeywords, "keyword");
  if (kProperty.test(before)) return words(kPropertyKeywords, "keyword");
  if (in_model) {
    auto out = words(kModelKeywords, "keyword");
    auto els = elements();
    out.insert(out.end(), els.begin(), els.end());
    if (libraries)
      for (const auto& p : libraries->set.palette())
        if (p.type == "fragment")
          out.push_back({p.ref, "snippet", p.label + (p.description ? " — " + *p.description : ""),
                         fragment_snippet(*libraries, p.alias, p.id)});
    return out;
  }
  return words(kTopKeywords, "keyword");
}

}  // namespace dd::lsp
