#include "host/styles.hpp"

#include <algorithm>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/render/svg.hpp"

#include "ascii.hpp"

namespace dd::host {

namespace {

using detail::trim_ascii;

std::vector<std::string_view> lines_of(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    out.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

/// The first `:` outside double quotes, or npos.
std::size_t colon_of(std::string_view line) {
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\\' && quoted) {
      ++i;
    } else if (line[i] == '"') {
      quoted = !quoted;
    } else if (line[i] == ':' && !quoted) {
      return i;
    }
  }
  return std::string_view::npos;
}

bool starts_word(std::string_view s, std::string_view word) {
  if (s.substr(0, word.size()) != word) return false;
  return s.size() == word.size() || s[word.size()] == ' ' || s[word.size()] == '\t';
}

/// `*`, `kind`, `tag "x"`, `kind tag "x"`, `relationship`, `relationship tag "x"`.
Result<StyleRule> parse_selector(std::string_view text) {
  StyleRule rule;
  std::string_view rest = trim_ascii(text);
  auto bad = [&] {
    return Error{"\"" + std::string(trim_ascii(text)) +
                 "\" is not a selector: write *, a kind, tag \"…\", a kind and a tag, or relationship"};
  };
  if (starts_word(rest, "relationship")) {
    rule.target = StyleTarget::Relationships;
    rest = trim_ascii(rest.substr(12));
  } else if (!rest.empty() && rest.front() == '*') {
    rest = trim_ascii(rest.substr(1));
  } else if (!starts_word(rest, "tag")) {
    std::size_t end = 0;
    while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t') ++end;
    if (end == 0) return bad();
    rule.kind = std::string(rest.substr(0, end));
    rest = trim_ascii(rest.substr(end));
  }
  if (!rest.empty()) {
    if (!starts_word(rest, "tag")) return bad();
    rest = trim_ascii(rest.substr(3));
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
      rule.tag = dsl::unquote(rest);
    else
      rule.tag = std::string(rest);
    if (rule.tag->empty()) return bad();
  }
  return rule;
}

/// `after` with the keys `before` has first, in `before`'s order, so a patched block parses back
/// to the same order.
Style ordered(const Style& before, const Style& after) {
  Style out;
  for (const auto& [k, _] : before)
    if (const std::string* v = after.find(k)) out.set(k, *v);
  for (const auto& [k, v] : after)
    if (!before.find(k)) out.set(k, v);
  return out;
}

std::string selector_of(const StyleRule& rule) { return dsl::print_style_selector(rule); }

Operation one(std::vector<Operation> ops) {
  if (ops.size() == 1) return std::move(ops.front());
  return op::Batch{std::move(ops)};
}

/// `key=value` per line (values may hold `;`).
Style parse_key_lines(std::string_view text) {
  Style out;
  for (auto line : lines_of(text)) {
    line = trim_ascii(line);
    if (line.empty()) continue;
    auto eq = line.find('=');
    auto key = trim_ascii(line.substr(0, eq));
    if (key.empty()) continue;
    out.set(std::string(key), eq == std::string_view::npos ? "" : std::string(trim_ascii(line.substr(eq + 1))));
  }
  return out;
}

std::string print_key_lines(const Style& style) {
  std::string out;
  for (const auto& [k, v] : style) out += (out.empty() ? "" : "\n") + k + "=" + v;
  return out;
}

/// Theme names the renderer can draw: built in, declared, or `alias.name` of a used library.
bool known_theme(const Model& model, const std::string& name) {
  if (find_theme(name)) return true;
  if (std::any_of(model.themes.begin(), model.themes.end(), [&](const ThemeDef& t) { return t.name == name; }))
    return true;
  auto dot = name.find('.');
  if (dot == std::string::npos || !model.libraries) return false;
  return std::any_of(model.libraries->begin(), model.libraries->end(),
                     [&](const LibraryUse& u) { return u.alias == name.substr(0, dot); });
}

const ThemeDef* declared_theme(const Model& model, std::string_view name) {
  for (const auto& t : model.themes)
    if (t.name == name) return &t;
  return nullptr;
}

}  // namespace

std::string style_lines(const std::vector<StyleRule>& rules, const std::vector<std::size_t>& shown) {
  std::string out;
  for (std::size_t i : shown) {
    if (i >= rules.size()) continue;
    out += (out.empty() ? "" : "\n") + selector_of(rules[i]) + ": " + dsl::print_style_text(rules[i].style);
  }
  return out;
}

Result<std::vector<StyleRule>> parse_style_lines(std::string_view text) {
  std::vector<StyleRule> out;
  int number = 0;
  for (auto line : lines_of(text)) {
    ++number;
    line = trim_ascii(line);
    if (line.empty()) continue;
    auto colon = colon_of(line);
    if (colon == std::string_view::npos)
      return Error{"Line " + std::to_string(number) + ": write a selector, a colon and the keys: container: fill=#438dd5"};
    auto rule = parse_selector(line.substr(0, colon));
    if (!rule) return Error{"Line " + std::to_string(number) + ": " + rule.error().message};
    rule->style = dsl::parse_style_text(trim_ascii(line.substr(colon + 1)));
    // As adding the block would refuse it, so an inspector can say which line.
    if (auto problem = style_rule_problem(*rule)) return Error{"Line " + std::to_string(number) + ": " + problem->message};
    out.push_back(std::move(*rule));
  }
  return out;
}

std::optional<Operation> style_operation(const std::vector<StyleRule>& rules, const std::vector<std::size_t>& shown,
                                         std::vector<StyleRule> wanted) {
  // Match shown blocks to wanted lines by selector (longest common subsequence), then pair what is
  // left between two matches in order: those are blocks whose selector changed.
  const std::size_t n = shown.size(), m = wanted.size();
  std::vector<std::vector<std::size_t>> lcs(n + 1, std::vector<std::size_t>(m + 1, 0));
  for (std::size_t a = n; a-- > 0;)
    for (std::size_t b = m; b-- > 0;)
      lcs[a][b] = selector_of(rules[shown[a]]) == selector_of(wanted[b]) ? lcs[a + 1][b + 1] + 1
                                                                          : std::max(lcs[a + 1][b], lcs[a][b + 1]);
  std::vector<std::optional<std::size_t>> match(n);  // shown position → wanted line
  {
    std::size_t a = 0, b = 0, gap_a = 0, gap_b = 0;
    auto pair_gap = [&](std::size_t to_a, std::size_t to_b) {
      for (; gap_a < to_a && gap_b < to_b; ++gap_a, ++gap_b) match[gap_a] = gap_b;
    };
    while (a < n && b < m) {
      if (selector_of(rules[shown[a]]) == selector_of(wanted[b])) {
        pair_gap(a, b);
        match[a] = b;
        gap_a = ++a;
        gap_b = ++b;
      } else if (lcs[a + 1][b] >= lcs[a][b + 1]) {
        ++a;
      } else {
        ++b;
      }
    }
    pair_gap(n, m);
  }
  std::vector<bool> placed(m, false);
  for (const auto& w : match)
    if (w) placed[*w] = true;
  for (std::size_t a = 0; a < n; ++a)
    if (match[a]) wanted[*match[a]].style = ordered(rules[shown[a]].style, wanted[*match[a]].style);

  // The blocks after the edit: unshown ones stay, shown ones become their lines, and new lines go
  // before the next matched one (or after the last shown block).
  struct Slot {
    std::optional<std::size_t> old;
    StyleRule rule;
  };
  std::vector<Slot> slots;
  std::size_t next = 0;  // next wanted line to place
  auto flush = [&](std::size_t upto) {
    for (; next < upto; ++next)
      if (!placed[next]) slots.push_back({std::nullopt, wanted[next]});
  };
  std::size_t at = 0;  // position in `shown`
  for (std::size_t i = 0; i < rules.size(); ++i) {
    if (at < n && shown[at] == i) {
      if (match[at]) {
        flush(*match[at]);
        slots.push_back({i, wanted[*match[at]]});
        next = std::max(next, *match[at] + 1);
      }
      if (++at == n) flush(m);
    } else {
      slots.push_back({i, rules[i]});
    }
  }
  flush(m);

  std::vector<Operation> ops;
  for (std::size_t a = n; a-- > 0;)
    if (!match[a]) ops.push_back(op::RemoveStyleRule{shown[a]});
  for (std::size_t p = 0; p < slots.size(); ++p) {
    const Slot& s = slots[p];
    if (!s.old) {
      ops.push_back(op::AddStyleRule{s.rule, p});
    } else {
      const StyleRule& was = rules[*s.old];
      bool same = was.target == s.rule.target && was.kind == s.rule.kind && was.tag == s.rule.tag &&
                  dsl::print_style_text(was.style) == dsl::print_style_text(s.rule.style);
      if (!same) ops.push_back(op::UpdateStyleRule{p, s.rule});
    }
  }
  if (ops.empty()) return std::nullopt;
  return one(std::move(ops));
}

std::vector<std::size_t> styles_for(const Model& model, const Element& element) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < model.styles.size(); ++i)
    if (style_applies(model.styles[i], element)) out.push_back(i);
  return out;
}

std::string shared_styles(const Model& model, const Element& element) {
  return style_lines(model.styles, styles_for(model, element));
}

Result<std::optional<Operation>> shared_styles_operation(const Model& model, const Element& element,
                                                         std::string_view typed) {
  auto wanted = parse_style_lines(typed);
  if (!wanted) return wanted.error();
  for (const auto& rule : *wanted)
    if (!style_applies(rule, element))
      return Error{"style " + selector_of(rule) + " does not apply to " + element.id +
                   "; edit it with nothing selected"};
  return style_operation(model.styles, styles_for(model, element), std::move(*wanted));
}

std::optional<std::string> view_theme(const Model& model, const std::string& view) {
  if (const View* v = model.views.find(view); v && v->theme) return v->theme;
  return model.theme;
}

DocumentFields document_fields(const Model& model, const std::string& view) {
  DocumentFields f;
  f.theme = model.theme.value_or("");
  if (const View* v = model.views.find(view)) f.view_theme = v->theme.value_or("");
  std::vector<std::size_t> all(model.styles.size());
  for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
  f.styles = style_lines(model.styles, all);
  if (auto name = view_theme(model, view))
    if (const ThemeDef* t = declared_theme(model, *name)) {
      f.declared = t->name;
      f.extends = t->base.value_or("");
      f.theme_keys = print_key_lines(t->values);
      for (const auto& [kind, look] : t->nodes)
        f.theme_nodes += (f.theme_nodes.empty() ? "" : "\n") + kind + ": " + dsl::print_style_text(look);
    }
  return f;
}

Result<std::optional<Operation>> document_operation(const Model& model, const std::string& view,
                                                    const DocumentFields& typed) {
  DocumentFields now = document_fields(model, view);
  std::vector<Operation> ops;
  std::vector<std::string> declaring;

  // A theme name nobody knows starts a new theme from the one it replaces.
  auto pick = [&](std::optional<ViewId> target, std::string_view typed_name, const std::string& was) {
    std::string name(trim_ascii(typed_name));
    if (name == was) return;
    if (!name.empty() && !known_theme(model, name) &&
        std::find(declaring.begin(), declaring.end(), name) == declaring.end()) {
      ThemeDef def;
      def.name = name;
      auto base = target ? view_theme(model, *target) : model.theme;
      if (base && *base != name && *base != "default") def.base = *base;
      ops.push_back(op::AddTheme{std::move(def), std::nullopt});
      declaring.push_back(name);
    }
    ops.push_back(op::SetTheme{std::move(target), name.empty() ? std::nullopt : std::optional(name)});
  };
  pick(std::nullopt, typed.theme, now.theme);
  if (model.views.find(view)) pick(view, typed.view_theme, now.view_theme);

  if (typed.styles != now.styles) {
    auto wanted = parse_style_lines(typed.styles);
    if (!wanted) return wanted.error();
    std::vector<std::size_t> all(model.styles.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    if (auto op = style_operation(model.styles, all, std::move(*wanted))) ops.push_back(std::move(*op));
  }

  const ThemeDef* declared = typed.declared.empty() ? nullptr : declared_theme(model, typed.declared);
  if (declared && (typed.extends != now.extends || typed.theme_keys != now.theme_keys ||
                   typed.theme_nodes != now.theme_nodes)) {
    ThemeDef def = *declared;
    std::string base(trim_ascii(typed.extends));
    def.base = base.empty() ? std::nullopt : std::optional(base);
    def.values = ordered(declared->values, parse_key_lines(typed.theme_keys));
    std::vector<std::pair<std::string, Style>> nodes;
    int number = 0;
    for (auto line : lines_of(typed.theme_nodes)) {
      ++number;
      line = trim_ascii(line);
      if (line.empty()) continue;
      auto colon = line.find(':');
      if (colon == std::string_view::npos)
        return Error{"Node colours, line " + std::to_string(number) + ": write a kind, a colon and the keys: person: fill=#08427b"};
      std::string kind(trim_ascii(line.substr(0, colon)));
      Style look = dsl::parse_style_text(trim_ascii(line.substr(colon + 1)));
      auto same = std::find_if(nodes.begin(), nodes.end(), [&](const auto& p) { return p.first == kind; });
      if (same != nodes.end())
        for (const auto& [k, v] : look) same->second.set(k, v);
      else
        nodes.emplace_back(std::move(kind), std::move(look));
    }
    // Kinds already declared keep their place and key order; new ones follow.
    def.nodes.clear();
    for (const auto& [kind, look] : declared->nodes) {
      auto it = std::find_if(nodes.begin(), nodes.end(), [&](const auto& p) { return p.first == kind; });
      if (it != nodes.end()) {
        def.nodes.emplace_back(kind, ordered(look, it->second));
        nodes.erase(it);
      }
    }
    for (auto& n : nodes) def.nodes.push_back(std::move(n));
    if (!(def == *declared)) ops.push_back(op::UpdateTheme{std::move(def)});
  }
  if (ops.empty()) return std::optional<Operation>{};
  return std::optional(one(std::move(ops)));
}

}  // namespace dd::host
