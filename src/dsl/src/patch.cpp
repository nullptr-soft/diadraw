// Port of packages/dsl/src/patch.ts.

#include "diadraw/dsl/patch.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include "diadraw/base/text.hpp"
#include "diadraw/dsl/print.hpp"

namespace dd::dsl {

namespace {

constexpr std::string_view kIndent = "  ";

/// Plans return no edits when the operation cannot be expressed as a local patch.
using Plan = std::optional<std::vector<TextEdit>>;

std::uint32_t u32(std::size_t n) { return static_cast<std::uint32_t>(n); }

std::size_t line_start(std::string_view src, std::size_t pos) {
  // src.lastIndexOf('\n', pos - 1) + 1; a negative fromIndex searches from 0 in JavaScript.
  if (src.empty()) return 0;
  auto i = src.rfind('\n', pos == 0 ? 0 : pos - 1);
  return i == std::string_view::npos ? 0 : i + 1;
}

std::size_t line_end(std::string_view src, std::size_t pos) {
  auto i = src.find('\n', pos);
  return i == std::string_view::npos ? src.size() : i;
}

std::string indent_of(std::string_view src, std::size_t pos) {
  std::size_t start = line_start(src, pos);
  std::size_t end = start;
  while (end < src.size() && (src[end] == ' ' || src[end] == '\t')) ++end;
  return std::string(src.substr(start, end - start));
}

std::vector<std::string> split_lines(std::string_view s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    auto nl = s.find('\n', start);
    out.emplace_back(s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

/// /^\s*(\/\/.*)?$/
bool blank_or_comment(std::string_view s) {
  std::size_t i = 0;
  std::size_t len = 0;
  while (i < s.size() && is_js_space(s, i, &len)) i += len;
  return i == s.size() || s.compare(i, 2, "//") == 0;
}

/// Deleting a statement removes its whole line when nothing else is on it (a trailing `//`
/// comment goes with it); otherwise just the statement and one neighbouring space.
TextEdit deletion(std::string_view src, Span span) {
  std::size_t ls = line_start(src, span.from);
  std::size_t le = line_end(src, span.to);
  auto before = src.substr(ls, span.from - ls);
  auto after = src.substr(span.to, le - span.to);
  if (trim(before).empty() && blank_or_comment(after))
    return {u32(ls), u32(std::min(le + 1, src.size())), ""};
  if (span.to < src.size() && src[span.to] == ' ') return {span.from, span.to + 1, ""};
  if (span.from > 0 && src[span.from - 1] == ' ') return {span.from - 1, span.to, ""};
  return {span.from, span.to, ""};
}

/// Merges overlapping or touching deletions; drops ones inside others.
std::vector<TextEdit> merge_edits(std::vector<TextEdit> edits) {
  std::stable_sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
    return a.from != b.from ? a.from < b.from : a.to > b.to;
  });
  std::vector<TextEdit> out;
  for (auto& e : edits) {
    if (!out.empty() && e.from <= out.back().to && out.back().insert.empty() && e.insert.empty()) {
      out.back().to = std::max(out.back().to, e.to);
    } else {
      out.push_back(std::move(e));
    }
  }
  return out;
}

/// Inserts a statement as the last item of a `{ … }` body. Multi-line bodies get a new line at
/// the body's indentation; one-line bodies get the statement before the closing brace.
TextEdit insert_into_body(std::string_view src, Span body, const std::string& statement) {
  std::size_t close = body.to - 1;
  std::size_t ls = line_start(src, close);
  if (trim(src.substr(ls, close - ls)).empty() && ls > body.from) {
    std::string indent = indent_of(src, close) + std::string(kIndent);
    auto lines = split_lines(statement);
    for (auto& l : lines)
      if (!l.empty()) l = indent + l;
    return {u32(ls), u32(ls), join(lines, "\n") + "\n"};
  }
  if (statement.find('\n') != std::string::npos) {
    // Turn a one-line body into a multi-line one.
    std::string indent = indent_of(src, body.from);
    auto inner = trim(src.substr(body.from + 1, close - body.from - 1));
    std::vector<std::string> lines;
    if (!inner.empty()) lines.emplace_back(inner);
    for (auto& l : split_lines(statement)) lines.push_back(std::move(l));
    for (auto& l : lines)
      if (!l.empty()) l = indent + std::string(kIndent) + l;
    return {body.from, body.to, "{\n" + join(lines, "\n") + "\n" + indent + "}"};
  }
  std::string pad = close > 0 && src[close - 1] == ' ' ? "" : " ";
  return {u32(close), u32(close), pad + statement + " "};
}

std::string_view side_name(PortSide s) {
  switch (s) {
    case PortSide::North: return "north";
    case PortSide::South: return "south";
    case PortSide::East: return "east";
    default: return "west";
  }
}

/// The declaration of a new element, with its ports as a body.
std::string element_text(const Element& element) {
  std::string head = print_element_head(element);
  if (element.ports.empty()) return head;
  std::vector<std::string> ports;
  for (const auto& p : element.ports)
    ports.push_back(std::string(kIndent) + "port " + p.id +
                    (p.side ? " " + std::string(side_name(*p.side)) : ""));
  return head + " {\n" + join(ports, "\n") + "\n}";
}

/// Where top-level model statements go: the first model block, created when there is none.
TextEdit insert_top_level(std::string_view src, const ParseResult& parsed, const std::string& statement) {
  if (parsed.source_map.model) return insert_into_body(src, *parsed.source_map.model, statement);
  auto lines = split_lines(statement);
  for (auto& l : lines)
    if (!l.empty()) l = std::string(kIndent) + l;
  return {0, 0, "model {\n" + join(lines, "\n") + "\n}\n" + (trim(src).empty() ? "" : "\n")};
}

Plan plan_add_element(std::string_view src, const ParseResult& parsed, const Element& element) {
  const auto& map = parsed.source_map;
  if (element.parent && element.id != *element.parent + "." + local_name(element)) return std::nullopt;
  if (!element.parent && element.id.find('.') != std::string::npos) return std::nullopt;
  std::string text = element_text(element);
  if (element.group) {
    auto g = map.groups.find(*element.group);
    if (g == map.groups.end()) return std::nullopt;
    return std::vector<TextEdit>{insert_into_body(src, g->second.body, text)};
  }
  if (element.parent) {
    auto p = map.elements.find(*element.parent);
    if (p == map.elements.end()) return std::nullopt;
    const auto& parent = p->second;
    if (parent.body) return std::vector<TextEdit>{insert_into_body(src, *parent.body, text)};
    std::string indent = indent_of(src, parent.decl.from);
    auto lines = split_lines(text);
    for (auto& l : lines)
      if (!l.empty()) l = indent + std::string(kIndent) + l;
    return std::vector<TextEdit>{
        {parent.decl.to, parent.decl.to, " {\n" + join(lines, "\n") + "\n" + indent + "}"}};
  }
  return std::vector<TextEdit>{insert_top_level(src, parsed, text)};
}

bool hint_names_any(const LayoutHint& hint, const std::vector<ElementId>& ids) {
  auto in = [&](const std::string& id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
  if (const auto* h = std::get_if<PinHint>(&hint)) return in(h->element) || in(h->reference);
  if (const auto* h = std::get_if<PinAtHint>(&hint)) return in(h->element);
  if (const auto* h = std::get_if<SizeHint>(&hint)) return in(h->element);
  if (const auto* h = std::get_if<RotateHint>(&hint)) return in(h->element);
  if (const auto* h = std::get_if<AlignHint>(&hint))
    return std::any_of(h->elements.begin(), h->elements.end(), in);
  if (const auto* h = std::get_if<OrderHint>(&hint))
    return std::any_of(h->elements.begin(), h->elements.end(), in);
  return false;
}

Plan plan_remove_element(std::string_view src, const ParseResult& parsed, const ElementId& id) {
  const auto& model = parsed.model;
  const auto& map = parsed.source_map;
  auto ids = descendants_of(model, id);
  auto has = [&](const std::string& x) { return std::find(ids.begin(), ids.end(), x) != ids.end(); };
  std::vector<Span> spans;
  std::vector<std::string> gone;
  auto decl = map.elements.find(id);
  if (decl == map.elements.end()) return std::nullopt;
  spans.push_back(decl->second.decl);
  for (const auto& r : model.relationships) {
    if (has(r.source.element) || has(r.target.element)) {
      auto source = map.relationships.find(r.id);
      if (source == map.relationships.end()) return std::nullopt;
      spans.push_back(source->second.decl);
      gone.push_back(r.id);
    }
  }
  std::vector<TextEdit> edits;
  for (const auto& view : model.views) {
    auto vs_it = map.views.find(view.id);
    if (vs_it == map.views.end()) return std::nullopt;
    const auto& vs = vs_it->second;
    if (view.scope && has(*view.scope)) {
      spans.push_back(vs.decl);
      continue;
    }
    for (std::size_t i = 0; i < view.hints.size() && i < vs.hints.size(); ++i) {
      const auto& hint = view.hints[i];
      if (const auto* route = std::get_if<RouteHint>(&hint)) {
        if (std::find(gone.begin(), gone.end(), route->relationship) != gone.end())
          spans.push_back(vs.hints[i]);
        continue;
      }
      if (hint_names_any(hint, ids)) spans.push_back(vs.hints[i]);
    }
    // Notes and boxes attached to a removed element go with it.
    for (std::size_t i = 0; i < view.decorations.size() && i < vs.decorations.size(); ++i) {
      const auto& d = view.decorations[i];
      bool named = (d.reference && has(*d.reference)) ||
                   std::any_of(d.around.begin(), d.around.end(), [&](const auto& e) { return has(e); });
      if (named) spans.push_back(vs.decorations[i]);
    }
    // Selectors: drop the token, or the whole statement when it was the last one.
    std::vector<std::pair<std::uint32_t, std::vector<const SelectorSource*>>> statements;
    for (const auto& sel : vs.selectors) {
      auto it = std::find_if(statements.begin(), statements.end(),
                             [&](const auto& s) { return s.first == sel.statement.from; });
      if (it == statements.end())
        statements.push_back({sel.statement.from, {&sel}});
      else
        it->second.push_back(&sel);
    }
    for (const auto& [_, sels] : statements) {
      std::vector<const SelectorSource*> doomed;
      for (const auto* s : sels)
        if (has(s->selector)) doomed.push_back(s);
      if (doomed.empty()) continue;
      if (doomed.size() == sels.size()) {
        // `include x` with x gone would turn into "include everything"; that needs a reprint.
        if (sels[0]->include) return std::nullopt;
        spans.push_back(sels[0]->statement);
      } else {
        for (const auto* s : doomed) edits.push_back(deletion(src, s->span));
      }
    }
  }
  std::vector<TextEdit> all;
  for (const auto& s : spans) all.push_back(deletion(src, s));
  for (auto& e : edits) all.push_back(std::move(e));
  return merge_edits(std::move(all));
}

/// A deletion of one clause of a declaration and the blanks before it (or after it, when it
/// starts a line).
TextEdit clause_deletion(std::string_view src, Span span) {
  std::size_t from = span.from;
  while (from > 0 && (src[from - 1] == ' ' || src[from - 1] == '\t')) --from;
  if (from > 0 && src[from - 1] != '\n') return {u32(from), span.to, ""};
  std::size_t to = span.to;
  while (to < src.size() && (src[to] == ' ' || src[to] == '\t')) ++to;
  return {span.from, u32(to), ""};
}

/// What a declaration's clauses say: the parts of an element or a relationship the clause edits
/// below work on. Relationships have no shape or description.
struct ClauseValues {
  std::optional<std::string> technology;
  std::optional<std::string> shape;
  std::optional<std::string> description;
  std::optional<Style> style;
  std::vector<std::string> tags;
  Properties properties;
};

ClauseValues clause_values(const Element& e) {
  return {e.technology, e.shape, e.description, e.style, e.tags, e.properties};
}

ClauseValues clause_values(const Relationship& r) {
  return {r.technology, std::nullopt, std::nullopt, r.style, r.tags, r.properties};
}

/// Edits that turn a declaration's clauses from `before`'s into `after`'s: changed clauses are
/// rewritten in place, removed ones deleted, and new ones added to `appended` (the caller puts it
/// after the last clause), so the rest of the line (comments, the order the person wrote things
/// in, a body) stays as it was. `indent` is the declaration line's, for a description that spans
/// lines.
void clause_edits(std::string_view src, const std::vector<ClauseSource>& clauses, const ClauseValues& before,
                  const ClauseValues& after, std::vector<TextEdit>& edits, std::string& appended,
                  std::string_view indent) {
  using K = ClauseSource::Kind;
  auto clauses_of = [&](K kind) {
    std::vector<const ClauseSource*> out;
    for (const auto& c : clauses)
      if (c.kind == kind) out.push_back(&c);
    return out;
  };

  // Single-valued clauses: the last one wins, so it is the one rewritten.
  auto single = [&](K kind, const std::optional<std::string>& was, const std::optional<std::string>& now,
                    const std::string& clause) {
    if (was == now) return;
    auto existing = clauses_of(kind);
    if (!now) {
      for (const auto* c : existing) edits.push_back(clause_deletion(src, c->span));
    } else if (!existing.empty()) {
      edits.push_back({existing.back()->span.from, existing.back()->span.to, clause});
    } else {
      appended += " " + clause;
    }
  };
  single(K::Technology, before.technology, after.technology,
         after.technology ? "tech " + quote(*after.technology) : "");
  single(K::Shape, before.shape, after.shape, after.shape ? "shape " + *after.shape : "");
  single(K::Description, before.description, after.description,
         after.description ? "description " + quote_lines(*after.description, indent) : "");
  if (before.style != after.style) {
    std::optional<std::string> was = before.style ? std::optional(print_style_text(*before.style)) : std::nullopt;
    std::optional<std::string> now = after.style ? std::optional(print_style_text(*after.style)) : std::nullopt;
    single(K::Style, was, now, now ? "style " + quote(*now) : "");
  }

  // Tags keep their order: those still there in the same order stay, the rest are deleted, and the
  // new ones go at the end.
  if (before.tags != after.tags) {
    std::size_t next = 0;
    for (const auto* c : clauses_of(K::Tag)) {
      if (next < after.tags.size() && c->key == after.tags[next])
        ++next;
      else
        edits.push_back(clause_deletion(src, c->span));
    }
    for (; next < after.tags.size(); ++next) appended += " tag " + quote(after.tags[next]);
  }

  // Properties by key.
  if (!(before.properties == after.properties)) {
    for (const auto* c : clauses_of(K::Prop)) {
      const std::string* now = after.properties.find(c->key);
      if (!now)
        edits.push_back(clause_deletion(src, c->span));
      else if (const std::string* was = before.properties.find(c->key); !was || *was != *now)
        edits.push_back({c->span.from, c->span.to, "prop " + quote(c->key) + " " + quote(*now)});
    }
    for (const auto& [k, v] : after.properties)
      if (!before.properties.find(k)) appended += " prop " + quote(k) + " " + quote(v);
  }
}

/// Edits that turn an element's declaration into `after`'s, clause by clause (see clause_edits).
Plan plan_update_element(std::string_view src, const ParseResult& parsed, const Element& after) {
  auto found = parsed.source_map.elements.find(after.id);
  const Element* before = parsed.model.elements.find(after.id);
  if (found == parsed.source_map.elements.end() || !before) return std::nullopt;
  const ElementSource& es = found->second;
  std::vector<TextEdit> edits;
  std::string appended;

  // Kind and name. A new label goes right after the kind, in the same edit.
  std::string kind_text = after.kind != before->kind ? after.kind : std::string();
  if (after.name != before->name && es.label) {
    edits.push_back({es.label->from, es.label->to, quote(after.name)});
  } else if (after.name != before->name) {
    if (kind_text.empty()) kind_text = before->kind;
    kind_text += " " + quote(after.name);
  }
  if (!kind_text.empty()) {
    std::string_view written = src.substr(es.kind.from, es.kind.to - es.kind.from);
    if (kind_text != written) edits.push_back({es.kind.from, es.kind.to, kind_text});
  }

  clause_edits(src, es.clauses, clause_values(*before), clause_values(after), edits, appended,
               indent_of(src, es.kind.from));

  if (!appended.empty()) {
    std::uint32_t end = es.label ? std::max(es.kind.to, es.label->to) : es.kind.to;
    for (const auto& c : es.clauses) end = std::max(end, c.span.to);
    edits.push_back({end, end, appended});
  }
  return merge_edits(std::move(edits));
}

// ── Styles and themes ──

/// Deletes a top-level statement with its lines, and one of the blank lines around it when it
/// sat between two, so the statements around it keep one blank line apart.
TextEdit statement_deletion(std::string_view src, Span span) {
  TextEdit e = deletion(src, span);
  bool whole_lines = (e.from == 0 || src[e.from - 1] == '\n') && (e.to == src.size() || src[e.to - 1] == '\n');
  if (!whole_lines || e.from == e.to) return e;
  bool blank_before = e.from == 0 || (e.from >= 2 && src[e.from - 2] == '\n');
  bool blank_after = e.to == src.size() || src[e.to] == '\n';
  if (blank_before && blank_after) {
    if (e.to < src.size())
      ++e.to;
    else if (e.from > 0)
      --e.from;
  }
  return e;
}

/// What a top-level statement is: `use`, `theme` (a theme statement), `themedef` (a declaration),
/// `style`, `model` or `view`.
std::string_view statement_kind(std::string_view src, Span span) {
  auto text = src.substr(span.from, span.to - span.from);
  for (std::string_view kw : {"use", "style", "model", "view"})
    if (text.substr(0, kw.size()) == kw) return kw;
  if (text.substr(0, 5) == "theme") return !text.empty() && text.back() == '}' ? "themedef" : "theme";
  return "";
}

/// Inserts a top-level statement on its own, a blank line apart from its neighbours: after
/// `after`'s line when given, else before the first statement of one of the `before` kinds (and
/// the comment lines right above it), else at the end of the file.
TextEdit insert_statement(std::string_view src, const SourceMap& map, const std::string& text,
                          std::optional<Span> after, std::initializer_list<std::string_view> before) {
  if (after) {
    auto at = u32(line_end(src, after->to));
    return {at, at, "\n\n" + text};
  }
  for (const auto& statement : map.statements) {
    auto kind = statement_kind(src, statement);
    if (std::find(before.begin(), before.end(), kind) == before.end()) continue;
    std::size_t at = line_start(src, statement.from);
    if (!trim(src.substr(at, statement.from - at)).empty()) break;
    while (at > 0) {
      std::size_t prev = line_start(src, at - 1);
      auto line = trim(src.substr(prev, at - 1 - prev));
      if (line.substr(0, 2) != "//") break;
      at = prev;
    }
    return {u32(at), u32(at), text + "\n\n"};
  }
  auto trimmed = trim_end(src);
  if (trimmed.empty()) return {0, u32(src.size()), text + "\n"};
  return {u32(trimmed.size()), u32(src.size()), "\n\n" + text + "\n"};
}

/// Inserts a statement before the one at `next`, on its own line. `blank` separates them by an
/// empty line.
Plan insert_before(std::string_view src, Span next, const std::string& text, bool blank) {
  std::size_t ls = line_start(src, next.from);
  if (!trim(src.substr(ls, next.from - ls)).empty()) return std::nullopt;
  return std::vector<TextEdit>{{u32(ls), u32(ls), text + (blank ? "\n\n" : "\n")}};
}

/// Whether two statements are written with an empty line between them.
bool blank_between(std::string_view src, Span a, Span b) {
  auto gap = src.substr(a.to, b.from > a.to ? b.from - a.to : 0);
  return gap.find("\n\n") != std::string_view::npos || gap.find("\n \n") != std::string_view::npos ||
         std::count(gap.begin(), gap.end(), '\n') >= 2;
}

/// Where a new item of a run of top-level statements (style blocks, theme declarations) goes, at
/// `index` among `existing`; nothing when the run is empty.
std::optional<Plan> insert_in_run(std::string_view src, const std::vector<Span>& existing, std::size_t index,
                                  const std::string& text) {
  if (existing.empty()) return std::nullopt;
  std::size_t at = std::min(index, existing.size());
  bool blank = existing.size() < 2 || blank_between(src, existing[0], existing[1]);
  if (at < existing.size()) return insert_before(src, existing[at], text, blank);
  auto end = u32(line_end(src, existing.back().to));
  return Plan{std::vector<TextEdit>{{end, end, (blank ? "\n\n" : "\n") + text}}};
}

/// Appends `key value` entries (or `node` blocks) to a body: one per line in a multi-line body,
/// side by side in a one-line one.
TextEdit append_entries(std::string_view src, Span body, const std::vector<std::string>& lines) {
  auto inside = src.substr(body.from, body.to - body.from);
  bool multi = inside.find('\n') != std::string_view::npos ||
               std::any_of(lines.begin(), lines.end(), [](const auto& l) { return l.find('\n') != std::string::npos; });
  return insert_into_body(src, body, join(lines, multi ? "\n" : " "));
}

/// Edits that turn a body's `key value` entries from `before` into `after`: a changed value is
/// rewritten where it is written (the last entry of a key, which wins), a dropped key's entries
/// are deleted, and new keys are returned in `added` for the caller to append.
void patch_entries(std::string_view src, const std::vector<EntrySource>& entries, const Style& before,
                   const Style& after, std::vector<TextEdit>& edits, std::vector<std::string>& added) {
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const std::string* now = after.find(entry.key);
    if (!now) {
      edits.push_back(deletion(src, entry.span));
      continue;
    }
    bool last = std::none_of(entries.begin() + static_cast<std::ptrdiff_t>(i) + 1, entries.end(),
                             [&](const EntrySource& e) { return e.key == entry.key; });
    const std::string* was = before.find(entry.key);
    if (last && (!was || *was != *now)) edits.push_back({entry.span.from, entry.span.to, entry.key + " " + print_style_value(*now)});
  }
  for (const auto& [k, v] : after)
    if (std::none_of(entries.begin(), entries.end(), [&](const EntrySource& e) { return e.key == k; }))
      added.push_back(k + " " + print_style_value(v));
}

std::vector<Span> decls(const std::vector<StyleSource>& styles) {
  std::vector<Span> out;
  for (const auto& s : styles) out.push_back(s.decl);
  return out;
}

std::vector<Span> decls(const std::vector<ThemeSource>& themes) {
  std::vector<Span> out;
  for (const auto& t : themes) out.push_back(t.decl);
  return out;
}

/// The last `use` statement, the anchor for statements that come after the uses.
std::optional<Span> last_use(const SourceMap& map) {
  std::optional<Span> out;
  for (const auto& [_, s] : map.libraries)
    if (!out || s.from > out->from) out = s;
  return out;
}

Plan plan_add_style(std::string_view src, const ParseResult& parsed, const StyleRule& rule, std::optional<std::size_t> index) {
  const auto& map = parsed.source_map;
  if (map.styles.size() != parsed.model.styles.size()) return std::nullopt;
  std::string text = print_style_rule(rule);
  if (auto run = insert_in_run(src, decls(map.styles), index.value_or(map.styles.size()), text)) return *run;
  std::optional<Span> after = map.theme;
  if (!after && !map.themes.empty()) after = map.themes.back().decl;
  if (!after) after = last_use(map);
  return std::vector<TextEdit>{insert_statement(src, map, text, after, {"model", "view"})};
}

Plan plan_update_style(std::string_view src, const ParseResult& parsed, std::size_t index, const StyleRule& rule) {
  const auto& map = parsed.source_map;
  if (map.styles.size() != parsed.model.styles.size() || index >= map.styles.size()) return std::nullopt;
  const StyleSource& ss = map.styles[index];
  const StyleRule& before = parsed.model.styles[index];
  std::vector<TextEdit> edits;
  if (before.target != rule.target || before.kind != rule.kind || before.tag != rule.tag)
    edits.push_back({ss.selector.from, ss.selector.to, print_style_selector(rule)});
  std::vector<std::string> added;
  patch_entries(src, ss.entries, before.style, rule.style, edits, added);
  if (!added.empty()) edits.push_back(append_entries(src, ss.body, added));
  return merge_edits(std::move(edits));
}

Plan plan_set_theme(std::string_view src, const ParseResult& parsed, const op::SetTheme& o) {
  const auto& map = parsed.source_map;
  std::optional<Span> written;
  if (o.view) {
    auto v = map.views.find(*o.view);
    if (v == map.views.end()) return std::nullopt;
    written = v->second.theme;
    if (!written) {
      if (!o.theme) return std::vector<TextEdit>{};
      return std::vector<TextEdit>{insert_into_body(src, v->second.body, "theme " + *o.theme)};
    }
  } else {
    written = map.theme;
    if (!written) {
      if (!o.theme) return std::vector<TextEdit>{};
      std::optional<Span> after;
      if (!map.themes.empty()) after = map.themes.back().decl;
      if (!after) after = last_use(map);
      return std::vector<TextEdit>{insert_statement(src, map, "theme " + *o.theme, after, {"style", "model", "view"})};
    }
  }
  if (!o.theme) return std::vector<TextEdit>{o.view ? deletion(src, *written) : statement_deletion(src, *written)};
  return std::vector<TextEdit>{{written->from, written->to, "theme " + *o.theme}};
}

std::optional<std::size_t> theme_index(const ParseResult& parsed, std::string_view name) {
  if (parsed.source_map.themes.size() != parsed.model.themes.size()) return std::nullopt;
  const auto& themes = parsed.model.themes;
  for (std::size_t i = 0; i < themes.size(); ++i)
    if (themes[i].name == name) return i;
  return std::nullopt;
}

Plan plan_add_theme(std::string_view src, const ParseResult& parsed, const ThemeDef& theme, std::optional<std::size_t> index) {
  const auto& map = parsed.source_map;
  if (map.themes.size() != parsed.model.themes.size()) return std::nullopt;
  std::string text = print_theme_def(theme);
  if (auto run = insert_in_run(src, decls(map.themes), index.value_or(map.themes.size()), text)) return *run;
  return std::vector<TextEdit>{insert_statement(src, map, text, last_use(map), {"theme", "style", "model", "view"})};
}

Plan plan_update_theme(std::string_view src, const ParseResult& parsed, const ThemeDef& after) {
  auto index = theme_index(parsed, after.name);
  if (!index) return std::nullopt;
  const ThemeSource& ts = parsed.source_map.themes[*index];
  const ThemeDef& before = parsed.model.themes[*index];
  std::vector<TextEdit> edits;
  if (before.base != after.base) {
    if (!after.base)
      edits.push_back(clause_deletion(src, *ts.base));
    else if (ts.base)
      edits.push_back({ts.base->from, ts.base->to, "extends " + *after.base});
    else
      edits.push_back({ts.name.to, ts.name.to, " extends " + *after.base});
  }
  std::vector<std::string> added;
  patch_entries(src, ts.entries, before.values, after.values, edits, added);

  auto look_of = [](const ThemeDef& t, std::string_view kind) -> const Style* {
    for (const auto& [k, look] : t.nodes)
      if (k == kind) return &look;
    return nullptr;
  };
  for (const auto& [kind, was] : before.nodes) {
    std::vector<const ThemeNodeSource*> blocks;
    for (const auto& n : ts.nodes)
      if (n.kind == kind) blocks.push_back(&n);
    const Style* now = look_of(after, kind);
    if (!now) {
      for (const auto* b : blocks) edits.push_back(deletion(src, b->decl));
      continue;
    }
    if (*now == was) continue;
    if (blocks.size() != 1) return std::nullopt;  // spread over several blocks: reprint
    std::vector<std::string> node_added;
    patch_entries(src, blocks[0]->entries, was, *now, edits, node_added);
    if (!node_added.empty()) edits.push_back(append_entries(src, blocks[0]->body, node_added));
  }
  for (const auto& [kind, look] : after.nodes) {
    if (look_of(before, kind)) continue;
    ThemeDef only;
    only.nodes.emplace_back(kind, look);
    // `theme x {\n  node kind {…}\n}` → the node block, unindented by one level.
    auto printed = print_theme_def(only);
    auto lines = split_lines(printed);
    std::vector<std::string> body(lines.begin() + 1, lines.end() - 1);
    for (auto& l : body) l = l.substr(std::min(l.size(), kIndent.size()));
    added.push_back(join(body, "\n"));
  }
  if (!added.empty()) edits.push_back(append_entries(src, ts.body, added));
  return merge_edits(std::move(edits));
}

/// The text of a hint that replaces `before`, written as `src[span]`. When both pin or route the
/// same thing, the head is kept as written (`pin db` in a scoped view, an escaped route id) and only
/// the coordinates after it are new.
std::string replacement_hint(std::string_view src, Span span, const LayoutHint& before, const LayoutHint& after) {
  std::string text = print_hint(after);
  std::string_view written = src.substr(span.from, span.to - span.from);
  // The element a `pin … at`, `size` or `rotate` is about, and its keyword.
  auto subject = [](const LayoutHint& h) -> std::optional<std::pair<std::string_view, std::string_view>> {
    if (const auto* p = std::get_if<PinAtHint>(&h)) return std::pair{std::string_view("pin"), std::string_view(p->element)};
    if (const auto* p = std::get_if<SizeHint>(&h)) return std::pair{std::string_view("size"), std::string_view(p->element)};
    if (const auto* p = std::get_if<RotateHint>(&h))
      return std::pair{std::string_view("rotate"), std::string_view(p->element)};
    return std::nullopt;
  };
  auto subject_before = subject(before);
  auto subject_after = subject(after);
  const auto* route_before = std::get_if<RouteHint>(&before);
  const auto* route_after = std::get_if<RouteHint>(&after);
  bool route = route_before && route_after && route_before->relationship == route_after->relationship;
  if (!route && !(subject_before && subject_after && *subject_before == *subject_after)) return text;
  std::size_t printed_head =
      route ? std::string_view("route ").size() + quote(route_after->relationship).size()
            : subject_after->first.size() + 1 + subject_after->second.size();
  // The written head: the keyword, blanks, then the subject (a quoted string or a name).
  std::size_t i = written.find_first_of(" \t");
  if (i == std::string_view::npos) return text;
  while (i < written.size() && (written[i] == ' ' || written[i] == '\t')) ++i;
  if (route) {
    if (i >= written.size() || written[i] != '"') return text;
    for (++i; i < written.size() && written[i] != '"'; ++i)
      if (written[i] == '\\') ++i;
    if (i >= written.size()) return text;
    ++i;
  } else {
    while (i < written.size() && written[i] != ' ' && written[i] != '\t' && written[i] != '\n') ++i;
  }
  return std::string(written.substr(0, i)) + text.substr(printed_head);
}

std::string_view arrow_text(const Relationship& r) {
  return r.bidirectional ? "<->" : r.line == LineStyle::Dashed ? "-->" : "->";
}

/// Edits that turn a relationship's declaration into `after`'s: the arrow and the label where
/// they are written, the clauses as for elements (see clause_edits).
Plan plan_update_relationship(std::string_view src, const ParseResult& parsed, const Relationship& after) {
  auto found = parsed.source_map.relationships.find(after.id);
  const Relationship* before = parsed.model.relationships.find(after.id);
  if (found == parsed.source_map.relationships.end() || !before) return std::nullopt;
  const RelationshipSource& rs = found->second;
  std::vector<TextEdit> edits;
  std::string appended;

  if (arrow_text(after) != arrow_text(*before))
    edits.push_back({rs.arrow.from, rs.arrow.to, std::string(arrow_text(after))});

  // A new label goes right after the target, before the clauses.
  std::string new_label;
  if (after.label != before->label) {
    if (!after.label)
      edits.push_back({rs.target.to, rs.label->to, ""});
    else if (rs.label)
      edits.push_back({rs.label->from, rs.label->to, quote(*after.label)});
    else
      new_label = " " + quote(*after.label);
  }

  clause_edits(src, rs.clauses, clause_values(*before), clause_values(after), edits, appended,
               {});  // relationships have no description

  std::uint32_t end = rs.label ? rs.label->to : rs.target.to;
  for (const auto& c : rs.clauses) end = std::max(end, c.span.to);
  if (!new_label.empty() && end == rs.target.to) {
    appended = new_label + appended;
  } else if (!new_label.empty()) {
    edits.push_back({rs.target.to, rs.target.to, new_label});
  }
  if (!appended.empty()) edits.push_back({end, end, appended});
  return merge_edits(std::move(edits));
}

/// The span of the string literal that names the relationship in a `route` statement.
std::optional<Span> route_id_span(std::string_view src, Span hint) {
  std::size_t open = src.find('"', hint.from);
  if (open == std::string_view::npos || open >= hint.to) return std::nullopt;
  for (std::size_t i = open + 1; i < hint.to; ++i) {
    if (src[i] == '\\') {
      ++i;
    } else if (src[i] == '"') {
      return Span{u32(open), u32(i + 1)};
    }
  }
  return std::nullopt;
}

/// Appends an edit for every `route "…"` hint that names a relationship `renamed` gives a new id;
/// false when a hint's id cannot be found where it is written.
template <class Renames>
bool rewrite_routes(std::string_view src, const ParseResult& parsed, const Renames& renamed,
                    std::vector<TextEdit>& edits) {
  if (renamed.empty()) return true;
  for (const auto& [view_id, vs] : parsed.source_map.views) {
    const View* view = parsed.model.views.find(view_id);
    if (!view) continue;
    for (std::size_t i = 0; i < view->hints.size() && i < vs.hints.size(); ++i) {
      const auto* route = std::get_if<RouteHint>(&view->hints[i]);
      if (!route) continue;
      auto it = renamed.find(route->relationship);
      if (it == renamed.end() || it->second == route->relationship) continue;
      auto literal = route_id_span(src, vs.hints[i]);
      if (!literal) return false;
      edits.push_back({literal->from, literal->to, quote(it->second)});
    }
  }
  return true;
}

/// Edits that give relationships new ids: the `name =` in front of each line (written, rewritten
/// or deleted; a derived id that only renumbers needs none) and the `route "…"` hints that name
/// them.
Plan plan_rename_relationships(std::string_view src, const ParseResult& parsed, const op::RenameRelationships& o) {
  const auto& map = parsed.source_map;
  std::map<std::string, std::string, std::less<>> renamed;
  std::vector<TextEdit> edits;
  for (const auto& r : o.renames) {
    auto found = map.relationships.find(r.from);
    if (found == map.relationships.end()) return std::nullopt;
    const RelationshipSource& rs = found->second;
    renamed.emplace(r.from, r.to);
    bool named = is_identifier(r.to);
    if (rs.id && named) {
      edits.push_back({rs.id->from, rs.id->to, r.to});
    } else if (rs.id) {
      edits.push_back({rs.id->from, rs.source.from, ""});  // `name = ` up to the source
    } else if (named) {
      edits.push_back({rs.source.from, rs.source.from, r.to + " = "});
    }
  }
  if (!rewrite_routes(src, parsed, renamed, edits)) return std::nullopt;
  return merge_edits(std::move(edits));
}

/// What a dotted reference written in `scope` names, resolved as the lowering does: from the
/// innermost scope outwards, an element or else a port of the element before the last part.
std::optional<Endpoint> resolve_reference(const Model& model, const std::vector<std::string>& path,
                                          const std::vector<std::string>& scope) {
  for (std::size_t i = scope.size() + 1; i-- > 0;) {
    std::vector<std::string> full(scope.begin(), scope.begin() + static_cast<std::ptrdiff_t>(i));
    full.insert(full.end(), path.begin(), path.end());
    std::string id = join(full, ".");
    if (model.elements.contains(id)) return Endpoint{id, std::nullopt};
    if (full.size() > 1) {
      std::vector<std::string> owner_path(full.begin(), full.end() - 1);
      const Element* owner = model.elements.find(join(owner_path, "."));
      if (owner && std::any_of(owner->ports.begin(), owner->ports.end(),
                               [&](const Port& p) { return p.id == full.back(); }))
        return Endpoint{owner->id, full.back()};
    }
  }
  return std::nullopt;
}

/// How a declaration in `scope` refers to `end`: the shortest tail of its dotted id (and port)
/// that resolves to it from there, so `api` inside `shop { … }` and `shop.api` outside it.
std::optional<std::string> reference_to(const Model& model, const std::vector<std::string>& scope,
                                        const Endpoint& end) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    auto dot = end.element.find('.', start);
    parts.push_back(end.element.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (end.port) parts.push_back(*end.port);
  for (std::size_t k = 1; k <= parts.size(); ++k) {
    std::vector<std::string> path(parts.end() - static_cast<std::ptrdiff_t>(k), parts.end());
    if (resolve_reference(model, path, scope) == end) return join(path, ".");
  }
  return std::nullopt;
}

/// Rewrites the ends that moved, where they are written, and the ids that change: a name in
/// `name = a -> b` in place, and every `route` naming a renamed relationship. Ids derived from
/// endpoints are not written anywhere but in routes; reading the text back derives them anew.
Plan plan_reconnect(std::string_view src, const ParseResult& parsed, const op::ReconnectRelationship& o) {
  const auto& map = parsed.source_map;
  auto found = map.relationships.find(o.id);
  const Relationship* before = parsed.model.relationships.find(o.id);
  if (found == map.relationships.end() || !before) return std::nullopt;
  const RelationshipSource& rs = found->second;
  std::vector<TextEdit> edits;
  for (auto [was, now, span] : {std::tuple{&before->source, &o.source, rs.source},
                                std::tuple{&before->target, &o.target, rs.target}}) {
    if (*was == *now) continue;
    auto ref = reference_to(parsed.model, rs.scope, *now);
    if (!ref) return std::nullopt;
    edits.push_back({span.from, span.to, *ref});
  }
  for (const auto& [from, to] : o.renames) {
    if (from == to) continue;
    auto r = map.relationships.find(from);
    if (r == map.relationships.end()) return std::nullopt;
    if (r->second.id) {
      if (!is_identifier(to)) return std::nullopt;
      edits.push_back({r->second.id->from, r->second.id->to, to});
    }
  }
  if (!rewrite_routes(src, parsed, o.renames, edits)) return std::nullopt;
  return merge_edits(std::move(edits));
}

/// Edits that give an element a new id: its declaration's name, the part of every reference that
/// names it or (in a descendant's dotted reference) its place, and every `route` naming a
/// relationship whose derived id changes with it. Groups are written by their labels, so the
/// group ids that change need no edit.
Plan plan_change_element_id(std::string_view src, const ParseResult& parsed, const op::ChangeElementId& o) {
  const auto& map = parsed.source_map;
  auto decl = map.elements.find(o.id);
  if (decl == map.elements.end()) return std::nullopt;
  auto dot = o.to.rfind('.');
  std::string local = dot == std::string::npos ? o.to : o.to.substr(dot + 1);
  if (!is_identifier(local)) return std::nullopt;
  // The renamed part's index in a dotted id.
  auto depth = static_cast<std::size_t>(std::count(o.id.begin(), o.id.end(), '.'));
  std::vector<TextEdit> edits{{decl->second.name.from, decl->second.name.to, local}};
  std::set<std::uint32_t> seen{decl->second.name.from};
  for (const auto& ref : map.references) {
    if (!is_within(ref.target.element, o.id)) continue;
    if (ref.count > ref.path_length()) return std::nullopt;
    // The renamed part is path part `depth`; written from inside the element, it is not written.
    if (ref.path_index(0) > depth) continue;
    const Span& part = map.parts(ref)[depth - ref.path_index(0)];
    if (seen.insert(part.from).second) edits.push_back({part.from, part.to, local});
  }
  if (!rewrite_routes(src, parsed, o.relationships, edits)) return std::nullopt;
  return merge_edits(std::move(edits));
}

template <class... F>
struct overloaded : F... {
  using F::operator()...;
};
template <class... F>
overloaded(F...) -> overloaded<F...>;

Plan plan(std::string_view src, const ParseResult& parsed, const Operation& operation) {
  const auto& map = parsed.source_map;
  using Edits = std::vector<TextEdit>;
  return std::visit(
      overloaded{
          [&](const op::AddElement& o) -> Plan { return plan_add_element(src, parsed, o.element); },
          [&](const op::RemoveElement& o) -> Plan { return plan_remove_element(src, parsed, o.id); },
          [&](const op::RenameElement& o) -> Plan {
            auto el = map.elements.find(o.id);
            if (el == map.elements.end()) return std::nullopt;
            if (el->second.label) return Edits{{el->second.label->from, el->second.label->to, quote(o.name)}};
            return Edits{{el->second.kind.to, el->second.kind.to, " " + quote(o.name)}};
          },
          [&](const op::ChangeElementId& o) -> Plan { return plan_change_element_id(src, parsed, o); },
          [&](const op::UpdateElement& o) -> Plan { return plan_update_element(src, parsed, o.element); },
          [&](const op::AddRelationship& o) -> Plan {
            return Edits{insert_top_level(src, parsed, print_relationship(o.relationship))};
          },
          [&](const op::RemoveRelationship& o) -> Plan {
            auto r = map.relationships.find(o.id);
            if (r == map.relationships.end()) return std::nullopt;
            std::vector<TextEdit> edits{deletion(src, r->second.decl)};
            for (const auto& [view_id, vs] : map.views) {
              const View* view = parsed.model.views.find(view_id);
              if (!view) continue;
              for (std::size_t i = 0; i < view->hints.size() && i < vs.hints.size(); ++i) {
                const auto* route = std::get_if<RouteHint>(&view->hints[i]);
                if (route && route->relationship == o.id) edits.push_back(deletion(src, vs.hints[i]));
              }
            }
            return merge_edits(std::move(edits));
          },
          [&](const op::ReconnectRelationship& o) -> Plan { return plan_reconnect(src, parsed, o); },
          [&](const op::SetRelationshipLabel& o) -> Plan {
            auto r = map.relationships.find(o.id);
            if (r == map.relationships.end()) return std::nullopt;
            const auto& rs = r->second;
            if (!o.label) {
              if (!rs.label) return Edits{};
              return Edits{{rs.target.to, rs.label->to, ""}};
            }
            if (rs.label) return Edits{{rs.label->from, rs.label->to, quote(*o.label)}};
            return Edits{{rs.target.to, rs.target.to, " " + quote(*o.label)}};
          },
          [&](const op::UpdateRelationship& o) -> Plan {
            return plan_update_relationship(src, parsed, o.relationship);
          },
          [&](const op::RenameRelationships& o) -> Plan { return plan_rename_relationships(src, parsed, o); },
          [&](const op::AddView& o) -> Plan {
            auto trimmed = trim_end(src);
            return Edits{{u32(trimmed.size()), u32(src.size()), "\n\n" + print_view(o.view) + "\n"}};
          },
          [&](const op::RemoveView& o) -> Plan {
            auto v = map.views.find(o.id);
            if (v == map.views.end()) return std::nullopt;
            return Edits{deletion(src, v->second.decl)};
          },
          [&](const op::RenameView& o) -> Plan {
            auto v = map.views.find(o.id);
            if (v == map.views.end()) return std::nullopt;
            return Edits{{v->second.name.from, v->second.name.to, o.to}};
          },
          [&](const op::ReplaceView& o) -> Plan {
            auto v = map.views.find(o.view.id);
            if (v == map.views.end()) return std::nullopt;
            return Edits{{v->second.decl.from, v->second.decl.to, print_view(o.view)}};
          },
          [&](const op::SetLayout& o) -> Plan {
            auto v = map.views.find(o.view);
            const View* view = parsed.model.views.find(o.view);
            if (v == map.views.end() || !view) return std::nullopt;
            View changed = *view;
            changed.layout = o.layout;
            std::string text = print_layout(changed);
            if (v->second.layout) return Edits{{v->second.layout->from, v->second.layout->to, text}};
            return Edits{insert_into_body(src, v->second.body, text)};
          },
          [&](const op::AddHint& o) -> Plan {
            auto v = map.views.find(o.view);
            if (v == map.views.end()) return std::nullopt;
            const auto& vs = v->second;
            std::string text = print_hint(o.hint);
            std::size_t index = o.index.value_or(vs.hints.size());
            if (index >= vs.hints.size()) return Edits{insert_into_body(src, vs.body, text)};
            const Span& next = vs.hints[index];
            std::size_t ls = line_start(src, next.from);
            if (trim(src.substr(ls, next.from - ls)).empty())
              return Edits{{u32(ls), u32(ls), indent_of(src, next.from) + text + "\n"}};
            return Edits{{next.from, next.from, text + " "}};
          },
          [&](const op::RemoveHint& o) -> Plan {
            auto v = map.views.find(o.view);
            if (v == map.views.end() || o.index >= v->second.hints.size()) return std::nullopt;
            return Edits{deletion(src, v->second.hints[o.index])};
          },
          [&](const op::ReplaceHint& o) -> Plan {
            auto v = map.views.find(o.view);
            const View* view = parsed.model.views.find(o.view);
            if (v == map.views.end() || !view || o.index >= v->second.hints.size() || o.index >= view->hints.size())
              return std::nullopt;
            const Span& span = v->second.hints[o.index];
            return Edits{{span.from, span.to, replacement_hint(src, span, view->hints[o.index], o.hint)}};
          },
          [&](const op::UseLibrary& o) -> Plan {
            // After the last `use`, or at the top of the file.
            std::vector<Span> spans;
            for (const auto& [_, s] : map.libraries) spans.push_back(s);
            std::stable_sort(spans.begin(), spans.end(),
                             [](const Span& a, const Span& b) { return a.from < b.from; });
            std::size_t index = o.index.value_or(spans.size());
            std::string line = print_use(o.use);
            if (index < spans.size()) {
              auto at = u32(line_start(src, spans[index].from));
              return Edits{{at, at, line + "\n"}};
            }
            if (!spans.empty()) return Edits{{spans.back().to, spans.back().to, "\n" + line}};
            return Edits{{0, 0, line + "\n\n"}};
          },
          [&](const op::UnuseLibrary& o) -> Plan {
            auto use = map.libraries.find(o.alias);
            if (use == map.libraries.end()) return std::nullopt;
            return Edits{deletion(src, use->second)};
          },
          [&](const op::AddStyleRule& o) -> Plan { return plan_add_style(src, parsed, o.rule, o.index); },
          [&](const op::RemoveStyleRule& o) -> Plan {
            if (map.styles.size() != parsed.model.styles.size() || o.index >= map.styles.size()) return std::nullopt;
            return Edits{statement_deletion(src, map.styles[o.index].decl)};
          },
          [&](const op::UpdateStyleRule& o) -> Plan { return plan_update_style(src, parsed, o.index, o.rule); },
          [&](const op::SetTheme& o) -> Plan { return plan_set_theme(src, parsed, o); },
          [&](const op::AddTheme& o) -> Plan { return plan_add_theme(src, parsed, o.theme, o.index); },
          [&](const op::RemoveTheme& o) -> Plan {
            auto index = theme_index(parsed, o.name);
            if (!index) return std::nullopt;
            return Edits{statement_deletion(src, map.themes[*index].decl)};
          },
          [&](const op::UpdateTheme& o) -> Plan { return plan_update_theme(src, parsed, o.theme); },
          [&](const op::AddGroup&) -> Plan { return std::nullopt; },
          [&](const op::RemoveGroup&) -> Plan { return std::nullopt; },
          [&](const op::Batch&) -> Plan { return std::nullopt; },
      },
      static_cast<const Operation::variant&>(operation));
}

std::size_t error_count(const ParseResult& r) {
  return static_cast<std::size_t>(std::count_if(r.diagnostics.begin(), r.diagnostics.end(), [](const Diagnostic& d) {
    return d.severity == Severity::Error;
  }));
}

/// Where the run of steps from `i` that can be planned against one parse ends (a group move makes
/// such runs): rewrites of different hints (every hint keeps its place), removals of one view's
/// hints in falling order (each index still names the hint it named before the run), and hints
/// appended to one view. Any other step is a run of its own.
std::size_t run_end(const std::vector<Operation>& steps, std::size_t i) {
  std::size_t end = i + 1;
  if (const auto* first = std::get_if<op::ReplaceHint>(&steps[i])) {
    std::set<std::pair<std::string_view, std::size_t>> seen{{first->view, first->index}};
    for (; end < steps.size(); ++end) {
      const auto* next = std::get_if<op::ReplaceHint>(&steps[end]);
      if (!next || !seen.insert({next->view, next->index}).second) break;
    }
  } else if (const auto* removal = std::get_if<op::RemoveHint>(&steps[i])) {
    for (std::size_t last = removal->index; end < steps.size(); ++end) {
      const auto* next = std::get_if<op::RemoveHint>(&steps[end]);
      if (!next || next->view != removal->view || next->index >= last) break;
      last = next->index;
    }
  } else if (const auto* add = std::get_if<op::AddHint>(&steps[i]); add && !add->index) {
    for (; end < steps.size(); ++end) {
      const auto* next = std::get_if<op::AddHint>(&steps[end]);
      if (!next || next->index || next->view != add->view) break;
    }
  }
  return end;
}

/// The edits of steps [i, end), a run `run_end` found, planned against one parse. A run of more
/// than one step is all rewrites, all removals in falling order, or all appends without an index.
Plan plan_run(std::string_view src, const ParseResult& parsed, const std::vector<Operation>& steps, std::size_t i,
              std::size_t end) {
  if (end == i + 1) return plan(src, parsed, steps[i]);
  if (const auto* removal = std::get_if<op::RemoveHint>(&steps[i])) {
    auto v = parsed.source_map.views.find(removal->view);
    if (v == parsed.source_map.views.end() || removal->index >= v->second.hints.size()) return std::nullopt;
    std::vector<Span> spans;
    spans.reserve(end - i);
    for (std::size_t s = i; s < end; ++s) spans.push_back(v->second.hints[std::get<op::RemoveHint>(steps[s]).index]);
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.from < b.from; });
    // Hints with only blanks between them on one line go as one span, so `deletion` sees what the
    // line holds once they are all gone: a line of nothing but removed hints goes whole. (Lines
    // of removed hints each go whole anyway, and `merge_edits` joins those deletions.)
    std::vector<Span> groups;
    for (const Span& span : spans) {
      if (!groups.empty()) {
        auto gap = src.substr(groups.back().to, span.from - groups.back().to);
        if (gap.find('\n') == std::string_view::npos && trim(gap).empty()) {
          groups.back().to = span.to;
          continue;
        }
      }
      groups.push_back(span);
    }
    std::vector<TextEdit> edits;
    edits.reserve(groups.size());
    for (const Span& group : groups) edits.push_back(deletion(src, group));
    return merge_edits(std::move(edits));
  }
  if (const auto* add = std::get_if<op::AddHint>(&steps[i])) {
    auto v = parsed.source_map.views.find(add->view);
    if (v == parsed.source_map.views.end()) return std::nullopt;
    std::vector<std::string> lines;
    lines.reserve(end - i);
    for (std::size_t s = i; s < end; ++s) lines.push_back(print_hint(std::get<op::AddHint>(steps[s]).hint));
    return std::vector<TextEdit>{insert_into_body(src, v->second.body, join(lines, "\n"))};
  }
  std::vector<TextEdit> edits;
  for (std::size_t s = i; s < end; ++s) {
    auto step = plan(src, parsed, steps[s]);
    if (!step) return std::nullopt;
    edits.insert(edits.end(), std::make_move_iterator(step->begin()), std::make_move_iterator(step->end()));
  }
  return edits;
}

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

}  // namespace

std::string apply_edits(std::string_view source, std::vector<TextEdit> edits) {
  std::string out(source);
  std::stable_sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return b.from < a.from; });
  for (const auto& e : edits) out.replace(e.from, e.to - e.from, e.insert);
  return out;
}

std::vector<TextEdit> diff_edit(std::string_view before, std::string_view after) {
  if (before == after) return {};
  std::size_t start = 0;
  while (start < before.size() && start < after.size() && before[start] == after[start]) ++start;
  // Never split a UTF-8 sequence: editors address characters, not bytes.
  while (start > 0 && start < before.size() && is_continuation(before[start])) --start;
  while (start > 0 && start < after.size() && is_continuation(after[start])) --start;
  std::size_t end_b = before.size();
  std::size_t end_a = after.size();
  while (end_b > start && end_a > start && before[end_b - 1] == after[end_a - 1]) {
    --end_b;
    --end_a;
  }
  while (end_b < before.size() && is_continuation(before[end_b])) {
    ++end_b;
    ++end_a;
  }
  return {{u32(start), u32(end_b), std::string(after.substr(start, end_a - start))}};
}

Result<SourceUpdate> update_source(std::string_view source, const Operation& op,
                                   const ParseResult* parsed_in) {
  ParseResult own;
  if (!parsed_in) own = parse(source);
  const ParseResult& parsed = parsed_in ? *parsed_in : own;

  auto applied = apply_operation(parsed.model, op);
  if (!applied) return applied.error();
  Model expected = std::move(applied->model);

  std::vector<Operation> steps;
  if (const auto* batch = std::get_if<op::Batch>(&op))
    steps = batch->operations;
  else
    steps.push_back(op);

  std::string current(source);
  std::optional<ParseResult> current_parse;
  std::vector<TextEdit> edits;
  bool minimal = true;
  for (std::size_t i = 0; i < steps.size() && minimal;) {
    const ParseResult& base = current_parse ? *current_parse : parsed;
    // A group move's steps come in runs planned against one parse, so the text is parsed once per
    // run, not once per element.
    std::size_t end = run_end(steps, i);
    auto run_edits = plan_run(current, base, steps, i, end);
    if (!run_edits) {
      minimal = false;
      break;
    }
    std::vector<TextEdit> run = std::move(*run_edits);
    current = apply_edits(current, run);
    current_parse = parse(current);
    if (steps.size() == 1) edits = std::move(run);
    i = end;
  }
  if (minimal) {
    if (steps.size() > 1) edits = diff_edit(source, current);
    const ParseResult& final_parse = current_parse ? *current_parse : parsed;
    if (error_count(final_parse) > error_count(parsed) || print(final_parse.model) != print(expected))
      minimal = false;
  }
  if (!minimal) {
    current = print(expected);
    current_parse = parse(current);
    edits = diff_edit(source, current);
  }
  SourceUpdate out;
  out.source = std::move(current);
  out.edits = std::move(edits);
  out.result = current_parse ? std::move(*current_parse) : parse(out.source);
  out.model = std::move(expected);
  out.minimal = minimal;
  return out;
}

}  // namespace dd::dsl
