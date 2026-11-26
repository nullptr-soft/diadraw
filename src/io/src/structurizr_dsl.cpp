// Port of packages/import/src/structurizr/dsl.ts.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <variant>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "structurizr_ir.hpp"

namespace dd::io::structurizr {

namespace {

struct Token {
  std::string text;
  bool quoted = false;
};

struct Statement {
  std::vector<Token> tokens;
  bool open = false;
  bool close = false;
  int line = 0;
  std::optional<std::string> file;
};

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool ends_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool is_word(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// The code point at `i` is JavaScript whitespace (`\s`); `len` gets its byte length.
bool whitespace_at(std::string_view s, std::size_t i, std::size_t& len) {
  std::size_t j = i;
  bool ws = is_js_whitespace(next_code_point(s, j));
  len = j - i;
  return ws;
}

/// A line terminator for JavaScript's `.`: \n, \r, U+2028, U+2029.
bool line_terminator_at(std::string_view s, std::size_t i) {
  if (s[i] == '\n' || s[i] == '\r') return true;
  return s.compare(i, 3, "\xE2\x80\xA8") == 0 || s.compare(i, 3, "\xE2\x80\xA9") == 0;
}

/// `s` without trailing JavaScript whitespace (`s.replace(/\s*$/, '')`).
std::string_view trim_end(std::string_view s) {
  std::string_view t = js_trim(s);
  return s.substr(0, static_cast<std::size_t>(t.data() - s.data()) + t.size());
}
/// `s` without leading JavaScript whitespace.
std::string_view trim_start(std::string_view s) {
  std::string_view t = js_trim(s);
  return t.empty() ? t : s.substr(static_cast<std::size_t>(t.data() - s.data()));
}

/// `s.split(sep).map(trim).filter(Boolean)` for comma-separated tag lists.
std::vector<std::string> tag_list(std::string_view s) { return split_list(s, ','); }

/// `text.split(/\r?\n/)`.
std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i)
    if (i == text.size() || text[i] == '\n') {
      std::size_t end = i;
      if (i < text.size() && end > start && text[end - 1] == '\r') --end;
      out.emplace_back(text.substr(start, end - start));
      start = i + 1;
    }
  return out;
}

/// `/^\s*"""/.test(line)`.
bool opens_with_triple_quote(std::string_view line) { return starts_with(trim_start(line), "\"\"\""); }

/// Splits DSL text into statements: one per line (with `\` continuations), braces separate.
std::vector<Statement> statements(std::string_view source, const std::optional<std::string>& file) {
  std::vector<Statement> out;
  // Block comments become spaces (keeping line breaks).
  std::string text;
  text.reserve(source.size());
  for (std::size_t i = 0; i < source.size();) {
    if (source.compare(i, 2, "/*") == 0) {
      std::size_t end = source.find("*/", i + 2);
      if (end != std::string_view::npos) {
        for (std::size_t k = i; k < end + 2; ++k) text.push_back(source[k] == '\n' ? '\n' : ' ');
        i = end + 2;
        continue;
      }
    }
    text.push_back(source[i++]);
  }
  std::vector<std::string> lines = split_lines(text);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::string raw = lines[i];
    const int line = static_cast<int>(i) + 1;
    // Line continuations, but not inside comments.
    auto continued = [](std::string_view r) { return ends_with(trim_end(r), "\\"); };
    auto comment = [](std::string_view r) {
      std::string_view t = trim_start(r);
      return starts_with(t, "#") || starts_with(t, "//");
    };
    while (continued(raw) && !comment(raw) && i + 1 < lines.size()) {
      std::string_view kept = trim_end(raw);
      raw = std::string(kept.substr(0, kept.size() - 1)) + " " + lines[++i];
    }
    // Text blocks: """ … """ on following lines.
    if (ends_with(js_trim(raw), "\"\"\"") && !opens_with_triple_quote(raw)) {
      std::string parts;
      bool first = true;
      while (++i < lines.size() && !opens_with_triple_quote(lines[i])) {
        if (!first) parts += "\n";
        first = false;
        parts += js_trim(lines[i]);
      }
      std::string_view kept = trim_end(raw);
      std::string escaped;
      for (char c : parts) {
        if (c == '"') escaped += "\\\"";
        else escaped.push_back(c);
      }
      raw = std::string(kept.substr(0, kept.size() - 3)) + " \"" + escaped + "\"";
    }
    // /"((?:[^"\\]|\\.)*)"|(\{|\})|(->|-\/>|--[\w-]+->)|([^\s{}"]+)/g
    std::vector<Token> tokens;
    std::string_view r = raw;
    for (std::size_t p = 0; p < r.size();) {
      char c = r[p];
      if (c == '"') {
        std::size_t q = p + 1;
        bool closed = false;
        while (q < r.size()) {
          if (r[q] == '"') {
            closed = true;
            break;
          }
          if (r[q] == '\\') {
            if (q + 1 >= r.size() || line_terminator_at(r, q + 1)) break;
            std::size_t k = q + 1;
            next_code_point(r, k);
            q = k;
            continue;
          }
          ++q;
        }
        if (!closed) {
          ++p;  // nothing matches a stray quote
          continue;
        }
        std::string body(r.substr(p + 1, q - p - 1));
        std::string unescaped;
        for (std::size_t k = 0; k < body.size(); ++k) {
          if (body[k] == '\\' && k + 1 < body.size() && (body[k + 1] == '"' || body[k + 1] == '\\')) {
            unescaped.push_back(body[++k]);
          } else {
            unescaped.push_back(body[k]);
          }
        }
        std::string value;
        for (std::size_t k = 0; k < unescaped.size(); ++k) {
          if (unescaped[k] == '\\' && k + 1 < unescaped.size() && unescaped[k + 1] == 'n') {
            value.push_back('\n');
            ++k;
          } else {
            value.push_back(unescaped[k]);
          }
        }
        tokens.push_back({value, true});
        p = q + 1;
        continue;
      }
      if (c == '{' || c == '}') {
        tokens.push_back({std::string(1, c), false});
        ++p;
        continue;
      }
      if (c == '-') {
        if (r.compare(p, 2, "->") == 0) {
          tokens.push_back({"->", false});
          p += 2;
          continue;
        }
        if (r.compare(p, 3, "-/>") == 0) {
          tokens.push_back({"->", false});
          p += 3;
          continue;
        }
        if (p + 1 < r.size() && r[p + 1] == '-') {
          std::size_t q = p + 2;
          while (q < r.size() && (is_word(r[q]) || r[q] == '-')) ++q;
          std::size_t run = q - (p + 2);
          if (run >= 2 && r[q - 1] == '-' && q < r.size() && r[q] == '>') {
            // `a --sync-> b`: a relationship archetype.
            tokens.push_back({"->", false});
            tokens.push_back({"archetype:" + std::string(r.substr(p + 2, run - 1)), false});
            p = q + 1;
            continue;
          }
        }
      }
      std::size_t wlen = 0;
      if (whitespace_at(r, p, wlen)) {
        p += wlen;
        continue;
      }
      std::size_t q = p;
      while (q < r.size() && r[q] != '{' && r[q] != '}' && r[q] != '"') {
        std::size_t l = 0;
        if (whitespace_at(r, q, l)) break;
        q += l;
      }
      std::string word(r.substr(p, q - p));
      // Comments start a line (`# …`, `// …`); a `#` later on is a colour.
      if ((tokens.empty() && starts_with(word, "#")) || starts_with(word, "//")) break;
      tokens.push_back({std::move(word), false});
      p = q;
    }
    std::vector<Token> current;
    for (auto& t : tokens) {
      if (!t.quoted && t.text == "{") {
        out.push_back({std::move(current), true, false, line, file});
        current.clear();
      } else if (!t.quoted && t.text == "}") {
        if (!current.empty()) out.push_back({std::move(current), false, false, line, file});
        out.push_back({{}, false, true, line, file});
        current.clear();
      } else {
        current.push_back(std::move(t));
      }
    }
    if (!current.empty()) out.push_back({std::move(current), false, false, line, file});
  }
  return out;
}

std::optional<ElementType> element_type(std::string_view word) {
  static const std::map<std::string, ElementType, std::less<>> kTypes{
      {"person", ElementType::Person},
      {"softwaresystem", ElementType::SoftwareSystem},
      {"container", ElementType::Container},
      {"component", ElementType::Component},
      {"element", ElementType::Element},
      {"deploymentnode", ElementType::DeploymentNode},
      {"infrastructurenode", ElementType::InfrastructureNode},
      {"containerinstance", ElementType::ContainerInstance},
      {"softwaresysteminstance", ElementType::SoftwareSystemInstance}};
  auto it = kTypes.find(word);
  return it == kTypes.end() ? std::nullopt : std::optional<ElementType>(it->second);
}

std::optional<ViewType> view_type(std::string_view word) {
  static const std::map<std::string, ViewType, std::less<>> kTypes{
      {"systemlandscape", ViewType::SystemLandscape}, {"systemcontext", ViewType::SystemContext},
      {"container", ViewType::Container},             {"component", ViewType::Component},
      {"filtered", ViewType::Filtered},               {"dynamic", ViewType::Dynamic},
      {"deployment", ViewType::Deployment},           {"custom", ViewType::Custom}};
  auto it = kTypes.find(word);
  return it == kTypes.end() ? std::nullopt : std::optional<ViewType>(it->second);
}

struct Archetype {
  /// The element type (or `relationship`) this archetype builds on.
  std::string base;
  std::optional<std::string> technology;
  std::optional<std::string> description;
  std::vector<std::string> tags;
};

/// A JavaScript Map: insertion order, setting a key again keeps its place.
template <class V>
struct JsMap {
  std::vector<std::string> order;
  std::map<std::string, V, std::less<>> values;

  void set(const std::string& k, V v) {
    auto it = values.find(k);
    if (it == values.end()) {
      order.push_back(k);
      values.emplace(k, std::move(v));
    } else {
      it->second = std::move(v);
    }
  }
  [[nodiscard]] const V* get(std::string_view k) const {
    auto it = values.find(k);
    return it == values.end() ? nullptr : &it->second;
  }
  [[nodiscard]] bool has(std::string_view k) const { return values.find(k) != values.end(); }
};

struct SkipFrame {};
struct WorkspaceFrame {};
struct ModelFrame {};
struct ViewsFrame {};
struct StylesFrame {};
struct ElementFrame {
  std::string key;
  ElementType type;
  std::optional<std::string> environment;
};
struct GroupFrame {
  std::string path;
};
struct EnvironmentFrame {
  std::string name;
};
struct ViewFrame {
  std::shared_ptr<IrView> view;
};
struct ParallelFrame {
  std::shared_ptr<IrView> view;
  int group;
};
struct StyleFrame {
  std::shared_ptr<TagStyle> style;
};
struct PropertiesFrame {
  std::optional<std::string> key;
  Record* into = nullptr;
};
struct RelationshipFrame {
  std::shared_ptr<IrRelationship> rel;
};
struct ArchetypesFrame {};
struct ArchetypeFrame {
  std::shared_ptr<Archetype> archetype;
};
struct PerspectivesFrame {
  std::optional<std::string> key;
  Record* into = nullptr;
};
using Frame = std::variant<SkipFrame, WorkspaceFrame, ModelFrame, ViewsFrame, StylesFrame, ElementFrame, GroupFrame,
                           EnvironmentFrame, ViewFrame, ParallelFrame, StyleFrame, PropertiesFrame, RelationshipFrame,
                           ArchetypesFrame, ArchetypeFrame, PerspectivesFrame>;

std::string join(const std::vector<Token>& t, std::size_t from, const char* sep = " ") {
  std::string out;
  for (std::size_t i = from; i < t.size(); ++i) {
    if (i > from) out += sep;
    out += t[i].text;
  }
  return out;
}

}  // namespace

Workspace parse_dsl(std::string_view source, ModelBuilder& b, const StructurizrOptions& options) {
  Workspace ws;
  JsMap<std::string> constants;
  JsMap<std::string> alias;
  JsMap<std::shared_ptr<Archetype>> archetypes;
  JsMap<std::shared_ptr<IrElement>> elements;
  std::vector<Frame> stack;
  int anonymous = 0;
  int parallel_groups = 0;
  std::map<std::string, int, std::less<>> view_counts;

  auto drop = [&](const Statement& st, const std::string& message, const std::string& construct) {
    b.drop(message, st.line, construct);
  };
  auto element_at = [&](std::string_view key) -> IrElement* {
    const auto* e = elements.get(key);
    return e ? e->get() : nullptr;
  };
  auto sub = [&](const std::string& text) {
    // text.replace(/\$\{([\w.-]+)\}/g, (all, n) => constants.get(n) ?? all)
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
      if (text.compare(i, 2, "${") == 0) {
        std::size_t j = i + 2;
        while (j < text.size() && (is_word(text[j]) || text[j] == '.' || text[j] == '-')) ++j;
        if (j > i + 2 && j < text.size() && text[j] == '}') {
          const std::string* v = constants.get(std::string_view(text).substr(i + 2, j - i - 2));
          out += v ? *v : text.substr(i, j + 1 - i);
          i = j + 1;
          continue;
        }
      }
      out.push_back(text[i++]);
    }
    return out;
  };
  auto current_element = [&]() -> std::optional<std::string> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (const auto* f = std::get_if<ElementFrame>(&*it)) return f->key;
    return std::nullopt;
  };
  auto current_environment = [&]() -> std::optional<std::string> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if (const auto* f = std::get_if<EnvironmentFrame>(&*it)) return f->name;
      if (const auto* f = std::get_if<ElementFrame>(&*it); f && f->environment && !f->environment->empty())
        return f->environment;
    }
    return std::nullopt;
  };
  auto current_group = [&]() -> std::optional<std::string> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if (const auto* f = std::get_if<GroupFrame>(&*it)) return f->path;
      if (std::holds_alternative<ElementFrame>(*it)) return std::nullopt;
    }
    return std::nullopt;
  };
  auto resolve = [&](const std::string& ref) -> std::optional<std::string> {
    std::string r = sub(ref);
    if (r == "this") return current_element();
    // Identifiers are case-insensitive.
    std::optional<std::string> known;
    if (elements.has(r)) known = r;
    else if (const std::string* a = alias.get(r)) known = *a;
    else {
      std::string lr = ascii_lower(r);
      for (const auto& id : alias.order)
        if (ascii_lower(id) == lr) {
          known = *alias.get(id);
          break;
        }
    }
    if (known && !known->empty() && !starts_with(*known, "group:") && !starts_with(*known, "env:")) return known;
    // Hierarchical identifiers relative to the enclosing elements: `b.c` inside `a`.
    for (auto el = current_element(); el && !el->empty();) {
      IrElement* e = element_at(*el);
      std::string prefix = e ? e->local : *el;
      const std::string* a = alias.get(prefix + "." + r);
      std::string k = a ? *a : *el + "." + r;
      if (elements.has(k)) return k;
      el = e ? e->parent : std::nullopt;
    }
    // Canonical names: `SoftwareSystem://Name`, `Container://System.Container`,
    // `DeploymentNode://Environment/Node/Child`.
    std::size_t colon = r.find("://");
    if (colon != std::string::npos && colon > 0 && colon + 3 < r.size()) {
      bool word = true;
      for (std::size_t i = 0; i < colon; ++i) word = word && is_word(r[i]);
      bool single_line = true;
      for (std::size_t i = colon + 3; i < r.size(); ++i) single_line = single_line && !line_terminator_at(r, i);
      if (word && single_line) {
        std::string type = ascii_lower(r.substr(0, colon));
        std::string path = r.substr(colon + 3);
        std::size_t cut = path.find_last_of("/.");
        std::string name = cut == std::string::npos ? path : path.substr(cut + 1);
        for (const auto& k : elements.order) {
          const IrElement& el = **elements.get(k);
          if (ascii_lower(type_name(el.type)) == type && el.name == name) return el.key;
        }
      }
    }
    return std::nullopt;
  };

  // `!elements "element.parent==x && element.tag==y"`: the elements an expression selects.
  std::function<std::vector<IrElement*>(const std::string&)> select = [&](const std::string& expression) {
    std::vector<std::function<bool(const IrElement&)>> tests;
    std::size_t start = 0;
    while (true) {
      std::size_t amp = expression.find("&&", start);
      std::string part = expression.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
      start = amp == std::string::npos ? std::string::npos : amp + 2;
      // /^\s*element\.(\w+)\s*==\s*(.*?)\s*$/
      std::optional<std::pair<std::string, std::string>> m;
      {
        std::string_view s = trim_start(part);
        if (starts_with(s, "element.")) {
          s.remove_prefix(8);
          std::size_t f = 0;
          while (f < s.size() && is_word(s[f])) ++f;
          std::string field(s.substr(0, f));
          std::string_view rest = trim_start(s.substr(f));
          if (!field.empty() && starts_with(rest, "==")) {
            std::string_view value = trim_end(trim_start(rest.substr(2)));
            bool single_line = true;
            for (std::size_t i = 0; i < value.size(); ++i) single_line = single_line && !line_terminator_at(value, i);
            if (single_line) m = std::make_pair(field, std::string(value));
          }
        }
      }
      std::string trimmed(js_trim(part));
      const std::string* group_alias = alias.get(trimmed);
      if (!m && group_alias && starts_with(*group_alias, "group:")) {
        auto members = select("element.group==" + trimmed);
        std::set<const IrElement*> set(members.begin(), members.end());
        tests.emplace_back([set](const IrElement& el) { return set.count(&el) > 0; });
      } else if (!m) {
        bool star = trimmed == "*";
        tests.emplace_back([star](const IrElement&) { return star; });
      } else {
        const auto& [field, value] = *m;
        if (field == "group") {
          const std::string* a = alias.get(value);
          std::string path = a ? (starts_with(*a, "group:") ? a->substr(6) : *a) : value;
          tests.emplace_back([path](const IrElement& el) {
            return el.group && (*el.group == path || starts_with(*el.group, path + "/"));
          });
        } else if (field == "parent") {
          auto parent = resolve(value);
          tests.emplace_back([parent](const IrElement& el) { return el.parent == parent; });
        } else if (field == "type") {
          std::string lv = ascii_lower(value);
          tests.emplace_back([lv](const IrElement& el) { return ascii_lower(type_name(el.type)) == lv; });
        } else if (field == "tag") {
          std::vector<std::string> wanted;
          std::size_t s0 = 0;
          for (std::size_t i = 0; i <= value.size(); ++i)
            if (i == value.size() || value[i] == ',') {
              wanted.emplace_back(js_trim(std::string_view(value).substr(s0, i - s0)));
              s0 = i + 1;
            }
          tests.emplace_back([wanted](const IrElement& el) {
            for (const auto& w : wanted)
              if (std::find(el.tags.begin(), el.tags.end(), w) == el.tags.end()) return false;
            return true;
          });
        } else if (field == "technology") {
          tests.emplace_back([value](const IrElement& el) { return el.technology == value; });
        } else {
          tests.emplace_back([](const IrElement&) { return false; });
        }
      }
      if (start == std::string::npos) break;
    }
    std::vector<IrElement*> out;
    for (const auto& k : elements.order) {
      IrElement* el = elements.get(k)->get();
      bool all = true;
      for (const auto& test : tests) all = all && test(*el);
      if (all) out.push_back(el);
    }
    return out;
  };

  auto element_frame = [](const IrElement& el) {
    return ElementFrame{el.key, el.type,
                        el.environment && !el.environment->empty() ? el.environment : std::nullopt};
  };

  std::function<void(const std::vector<Statement>&)> run = [&](const std::vector<Statement>& list) {
    for (std::size_t index = 0; index < list.size(); ++index) {
      const Statement& st = list[index];
      if (st.close) {
        if (stack.empty()) drop(st, "Unmatched \"}\"", "unmatched }");
        else stack.pop_back();
        continue;
      }
      Frame* frame = stack.empty() ? nullptr : &stack.back();
      std::vector<Token> t;
      for (const auto& x : st.tokens) t.push_back({sub(x.text), x.quoted});
      auto word = [&](std::size_t i) -> std::optional<std::string> {
        return i < t.size() && !t[i].quoted ? std::optional<std::string>(t[i].text) : std::nullopt;
      };
      auto text_at = [&](std::size_t i) -> std::optional<std::string> {
        return i < t.size() ? std::optional<std::string>(t[i].text) : std::nullopt;
      };
      std::optional<std::string> lower = word(0) ? std::optional<std::string>(ascii_lower(*word(0))) : std::nullopt;
      const bool directive = lower && starts_with(*lower, "!");
      auto lower_or = [&](const char* fallback) { return lower ? *lower : std::string(fallback); };
      auto skip_if_open = [&] {
        if (st.open) stack.emplace_back(SkipFrame{});
      };
      auto not_imported = [&] { return "Not imported: " + utf16_prefix(join(t, 0), 60); };

      if (frame && std::holds_alternative<SkipFrame>(*frame)) {
        skip_if_open();
        continue;
      }
      if (auto* f = frame ? std::get_if<PropertiesFrame>(frame) : nullptr) {
        if (f->into && t.size() >= 2) f->into->set(t[0].text, t[1].text);
        else if (f->key && t.size() >= 2) {
          if (IrElement* el = element_at(*f->key)) el->properties.set(t[0].text, t[1].text);
          else
            for (const auto& r : ws.relationships)
              if (r->key == f->key) {
                r->properties.set(t[0].text, t[1].text);
                break;
              }
        }
        skip_if_open();
        continue;
      }
      if (auto* f = frame ? std::get_if<RelationshipFrame>(frame) : nullptr; f && !directive) {
        // A relationship's body: `rel = a -> b { … }`.
        IrRelationship& rel = *f->rel;
        if (lower == "tags" || lower == "tag") {
          for (std::size_t i = 1; i < t.size(); ++i)
            for (auto& x : tag_list(t[i].text)) rel.tags.push_back(x);
        } else if (lower == "description" && t.size() > 1) rel.description = t[1].text;
        else if (lower == "technology" && t.size() > 1) rel.technology = t[1].text;
        else if (lower == "url" && t.size() > 1) rel.properties.set("url", t[1].text);
        else if (lower == "properties" && st.open) stack.emplace_back(PropertiesFrame{std::nullopt, &rel.properties});
        else if (lower == "perspectives" && st.open)
          stack.emplace_back(PerspectivesFrame{std::nullopt, &rel.properties});
        else {
          drop(st, "Not imported: " + utf16_prefix(join(t, 0), 60), "relationship " + lower_or("undefined"));
          skip_if_open();
        }
        continue;
      }
      if (frame && std::holds_alternative<ArchetypesFrame>(*frame) && !directive) {
        // `name = base { … }`, `name = otherArchetype { … }`, or `name = -> { … }` for relationships.
        if (t.size() > 1 && t[1].text == "=") {
          std::string base_name = text_at(2).value_or("");
          std::string parent_name = base_name == "->" && t.size() > 3 && starts_with(t[3].text, "archetype:")
                                        ? t[3].text.substr(10)
                                        : base_name;
          const auto* found = archetypes.get(parent_name);
          if (!found) found = archetypes.get("default:" + ascii_lower(base_name));
          const Archetype* parent = found ? found->get() : nullptr;
          auto a = std::make_shared<Archetype>();
          a->base = parent ? parent->base : base_name == "->" ? "relationship" : base_name;
          if (parent) a->tags = parent->tags;
          if (parent && parent->technology && !parent->technology->empty()) a->technology = parent->technology;
          if (parent && parent->description && !parent->description->empty()) a->description = parent->description;
          archetypes.set(t[0].text, a);
          if (st.open) stack.emplace_back(ArchetypeFrame{a});
        } else if (t.size() == 1 && st.open) {
          // `softwareSystem { … }` or `-> { … }`: defaults for every element of a type.
          auto a = std::make_shared<Archetype>();
          a->base = t[0].text == "->" ? "relationship" : t[0].text;
          archetypes.set("default:" + ascii_lower(t[0].text), a);
          stack.emplace_back(ArchetypeFrame{a});
        } else {
          skip_if_open();
        }
        continue;
      }
      if (auto* f = frame ? std::get_if<ArchetypeFrame>(frame) : nullptr; f && !directive) {
        auto a = f->archetype;
        if (lower == "technology" && t.size() > 1) a->technology = t[1].text;
        else if (lower == "description" && t.size() > 1) a->description = t[1].text;
        else if (lower == "tag" || lower == "tags")
          for (std::size_t i = 1; i < t.size(); ++i)
            for (auto& x : tag_list(t[i].text)) a->tags.push_back(x);
        skip_if_open();
        continue;
      }
      if (auto* f = frame ? std::get_if<PerspectivesFrame>(frame) : nullptr) {
        Record* into = f->into;
        if (!into && f->key)
          if (IrElement* el = element_at(*f->key)) into = &el->properties;
        if (into && !t.empty()) into->set("perspective." + t[0].text, join(t, 1));
        skip_if_open();
        continue;
      }
      if (auto* f = frame ? std::get_if<StyleFrame>(frame) : nullptr) {
        if (t.size() >= 2) f->style->props.set(t[0].text, join(t, 1));
        else if (!t.empty()) f->style->props.set(t[0].text, "true");
        skip_if_open();
        continue;
      }

      // Directives, anywhere.
      if (lower == "!constant" || lower == "!const" || lower == "!var") {
        if (t.size() > 1) constants.set(t[1].text, join(t, 2));
        continue;
      }
      if (lower == "!identifiers" || lower == "!impliedrelationships") {
        if (lower == "!impliedrelationships")
          ws.implied_relationships = ascii_lower(text_at(1).value_or("true")) != "false";
        continue;
      }
      if (lower == "!include") {
        std::string target = text_at(1).value_or("");
        std::string lt = ascii_lower(target);
        if (starts_with(lt, "http://") || starts_with(lt, "https://")) {
          drop(st, "Remote include not fetched: " + target, "!include url");
          continue;
        }
        std::optional<std::vector<IncludedFile>> found;
        if (options.resolve) found = options.resolve(target, st.file ? st.file : options.file_name);
        if (!found) {
          drop(st, "Include not found: " + target, "!include missing");
          continue;
        }
        for (const auto& f : *found) run(statements(f.text, f.path.empty() ? std::nullopt : std::optional(f.path)));
        continue;
      }
      if (lower == "!docs" || lower == "!adrs" || lower == "!decisions") {
        std::string what = lower == "!docs" ? "docs" : "adrs";
        auto el = current_element();
        if (el && !el->empty()) element_at(*el)->properties.set(what, text_at(1).value_or(""));
        else b.note("Workspace " + what + ": " + text_at(1).value_or(""));
        skip_if_open();
        continue;
      }
      if (lower == "!script" || lower == "!plugin") {
        drop(st, *lower + " is not run", *lower);
        skip_if_open();
        continue;
      }
      if (lower == "!ref" || lower == "!element" || lower == "!extend") {
        const std::string* env = alias.get(text_at(1).value_or(""));
        if (env && starts_with(*env, "env:")) {
          if (st.open) stack.emplace_back(EnvironmentFrame{env->substr(4)});
          continue;
        }
        auto key = resolve(text_at(1).value_or(""));
        if (!key || key->empty()) drop(st, "Unknown element " + text_at(1).value_or("undefined"), *lower);
        if (st.open) {
          IrElement* el = key && !key->empty() ? element_at(*key) : nullptr;
          if (el) stack.emplace_back(element_frame(*el));
          else stack.emplace_back(SkipFrame{});
        }
        continue;
      }
      if (lower == "!elements" && st.open) {
        // The body runs once per selected element, as if written inside it.
        std::vector<Statement> body;
        for (int depth = 1; ++index < list.size();) {
          const Statement& inner = list[index];
          if (inner.close && --depth == 0) break;
          if (inner.open) ++depth;
          body.push_back(inner);
        }
        auto selected = select(text_at(1).value_or(""));
        if (selected.empty()) drop(st, "No elements match " + text_at(1).value_or(""), "!elements");
        for (IrElement* el : selected) {
          std::size_t depth = stack.size();
          stack.emplace_back(element_frame(*el));
          run(body);
          stack.resize(depth, SkipFrame{});
        }
        continue;
      }
      if (lower == "!elements" || lower == "!relationships" || lower == "!relationship") {
        drop(st, *lower + " blocks are not applied", *lower);
        skip_if_open();
        continue;
      }
      if (directive) {
        drop(st, "Unknown directive " + *lower, *lower);
        skip_if_open();
        continue;
      }

      if (!frame) {
        if (lower == "workspace") {
          if (word(1) == "extends") {
            std::string target = text_at(2).value_or("");
            std::optional<IncludedFile> found;
            if (!starts_with(target, "http:") && !starts_with(target, "https:") && options.resolve)
              if (auto files = options.resolve(target, options.file_name); files && !files->empty())
                found = (*files)[0];
            if (found && starts_with(trim_start(found->text), "{")) {
              // Extending a JSON workspace: its elements keep their DSL identifiers if it has them.
              Workspace base = parse_json(found->text, b);
              for (const auto& el : base.elements) {
                ws.elements.push_back(el);
                elements.set(el->key, el);
                if (el->identifier && !el->identifier->empty()) alias.set(*el->identifier, el->key);
              }
              for (const auto& r : base.relationships) ws.relationships.push_back(r);
              for (const auto& v : base.views) ws.views.push_back(v);
              for (const auto& s : base.element_styles) ws.element_styles.push_back(s);
              for (const auto& s : base.relationship_styles) ws.relationship_styles.push_back(s);
              if (base.name && !base.name->empty() && !(ws.name && !ws.name->empty())) ws.name = base.name;
            } else if (found) {
              std::size_t before = stack.size();
              run(statements(found->text, found->path.empty() ? std::nullopt : std::optional(found->path)));
              stack.resize(before, SkipFrame{});
            } else {
              drop(st, "workspace extends " + target + ": not resolved", "workspace extends");
            }
          } else if (t.size() > 1 && t[1].quoted) {
            ws.name = t[1].text;
          }
          if (st.open) stack.emplace_back(WorkspaceFrame{});
        } else {
          drop(st, "Expected \"workspace\"", "outside workspace");
          skip_if_open();
        }
        continue;
      }

      if (std::holds_alternative<WorkspaceFrame>(*frame)) {
        if (lower == "model" && st.open) stack.emplace_back(ModelFrame{});
        else if (lower == "views" && st.open) stack.emplace_back(ViewsFrame{});
        else if (lower == "name" || lower == "description") {
          if (lower == "name" && t.size() > 1) ws.name = t[1].text;
        } else if (lower == "properties" && st.open) stack.emplace_back(PropertiesFrame{});
        else if (lower == "configuration" && st.open) stack.emplace_back(SkipFrame{});
        else {
          drop(st, "Not imported: " + lower_or("undefined"), lower_or("statement"));
          skip_if_open();
        }
        continue;
      }

      const bool modelish = std::holds_alternative<ModelFrame>(*frame) || std::holds_alternative<ElementFrame>(*frame) ||
                            std::holds_alternative<GroupFrame>(*frame) ||
                            std::holds_alternative<EnvironmentFrame>(*frame);
      if (modelish) {
        // Relationship: `[id =] a -> b …`, `-> b …`, `this -> b …`
        std::size_t arrow = t.size();
        for (std::size_t i = 0; i < t.size(); ++i)
          if (!t[i].quoted && t[i].text == "->") {
            arrow = i;
            break;
          }
        if (arrow != t.size()) {
          const bool has_id = t.size() > 1 && t[1].text == "=" && arrow == 3;
          std::string from_ref = arrow == 0 ? "this" : t[has_id ? 2 : 0].text;
          auto from = from_ref == "this" ? current_element() : resolve(from_ref);
          const Archetype* rel_archetype = nullptr;
          if (arrow + 1 < t.size() && starts_with(t[arrow + 1].text, "archetype:")) {
            std::string name = t[arrow + 1].text.substr(10);
            t.erase(t.begin() + static_cast<std::ptrdiff_t>(arrow + 1));
            if (const auto* a = archetypes.get(name)) rel_archetype = a->get();
          } else if (const auto* a = archetypes.get("default:->")) {
            rel_archetype = a->get();
          }
          auto to = resolve(text_at(arrow + 1).value_or(""));
          std::vector<std::string> rest;
          for (std::size_t i = arrow + 2; i < t.size(); ++i) rest.push_back(t[i].text);
          if (!from || from->empty() || !to || to->empty()) {
            drop(st, "Relationship to an unknown element: " + join(t, 0), "relationship");
            skip_if_open();
          } else {
            auto rel = std::make_shared<IrRelationship>();
            if (has_id && !t[0].text.empty()) rel->key = t[0].text;
            rel->source = *from;
            rel->destination = *to;
            auto pick = [&](std::size_t i, const std::optional<std::string>& fallback) -> std::optional<std::string> {
              if (i < rest.size() && !rest[i].empty()) return rest[i];
              if (fallback && !fallback->empty()) return fallback;
              return std::nullopt;
            };
            rel->description = pick(0, rel_archetype ? rel_archetype->description : std::nullopt);
            rel->technology = pick(1, rel_archetype ? rel_archetype->technology : std::nullopt);
            if (rel_archetype) rel->tags = rel_archetype->tags;
            if (rest.size() > 2)
              for (auto& x : tag_list(rest[2])) rel->tags.push_back(x);
            rel->line = st.line;
            ws.relationships.push_back(rel);
            if (st.open) stack.emplace_back(RelationshipFrame{rel});
          }
          continue;
        }

        if (lower == "archetypes" && st.open) {
          stack.emplace_back(ArchetypesFrame{});
          continue;
        }
        // `instanceOf x` inside a deployment node: an instance of a container or system.
        const std::size_t instance_at = t.size() > 1 && t[1].text == "=" ? 2 : 0;
        if (auto w = word(instance_at); w && ascii_lower(*w) == "instanceof" && instance_at + 1 < t.size()) {
          auto key = resolve(t[instance_at + 1].text);
          if (IrElement* target = element_at(key.value_or("")))
            t[instance_at] = {target->type == ElementType::Container ? "containerInstance" : "softwareSystemInstance", false};
        }
        const bool has_id = t.size() > 1 && !t[1].quoted && t[1].text == "=";
        std::string raw_type = (has_id ? word(2) : !t.empty() && !t[0].quoted ? std::optional(t[0].text) : std::nullopt)
                                   .value_or("");
        const auto* found_archetype = archetypes.get(raw_type);
        if (!found_archetype) found_archetype = archetypes.get("default:" + ascii_lower(raw_type));
        const Archetype* archetype = found_archetype ? found_archetype->get() : nullptr;
        std::string type_word = ascii_lower(archetype ? archetype->base : raw_type);
        if (auto type = element_type(type_word); !type_word.empty() && type) {
          std::vector<std::string> args;
          for (std::size_t i = has_id ? 3 : 1; i < t.size(); ++i) args.push_back(t[i].text);
          auto arg = [&](std::size_t i) -> std::optional<std::string> {
            return i < args.size() ? std::optional(args[i]) : std::nullopt;
          };
          std::optional<std::string> identifier = has_id ? std::optional(t[0].text) : std::nullopt;
          auto parent = current_element();
          auto environment = current_environment();
          const bool deployment = type == ElementType::DeploymentNode || type == ElementType::InfrastructureNode ||
                                  type == ElementType::ContainerInstance || type == ElementType::SoftwareSystemInstance;
          std::string local = identifier ? *identifier : type_name(*type) + std::to_string(++anonymous);
          std::string prefix = deployment ? "deployment:" + environment.value_or("") + ":" : "";
          std::string key = parent && !parent->empty() ? *parent + "." + local : prefix + local;
          auto el = std::make_shared<IrElement>();
          if (type == ElementType::ContainerInstance || type == ElementType::SoftwareSystemInstance) {
            auto of = resolve(arg(0).value_or(""));
            IrElement* target = of && !of->empty() ? element_at(*of) : nullptr;
            if (!target) {
              drop(st, type_word + " of an unknown element: " + arg(0).value_or("undefined"), type_word);
              skip_if_open();
              continue;
            }
            el->key = key;
            el->local = identifier ? *identifier : target->local;
            el->type = *type;
            el->name = target->name;
            if (target->description && !target->description->empty()) el->description = target->description;
            if (target->technology && !target->technology->empty()) el->technology = target->technology;
            el->tags = target->tags;
            for (auto& x : tag_list(arg(2).value_or(""))) el->tags.push_back(x);
            if (arg(1) && !arg(1)->empty()) el->properties.set("deploymentGroups", *arg(1));
            if (parent && !parent->empty()) el->parent = parent;
            if (environment && !environment->empty()) el->environment = environment;
            el->instance_of = target->key;
            el->line = st.line;
          } else {
            const bool with_tech = type == ElementType::Container || type == ElementType::Component ||
                                   type == ElementType::DeploymentNode || type == ElementType::InfrastructureNode;
            const bool is_element = type == ElementType::Element;
            auto description = is_element ? arg(2) : arg(1);
            auto technology = with_tech ? arg(2) : std::nullopt;
            std::string tags = (is_element ? arg(3) : with_tech ? arg(3) : arg(2)).value_or("");
            el->key = key;
            el->local = local;
            el->type = *type;
            el->name = arg(0).value_or(local);
            if (description && !description->empty()) el->description = description;
            if (technology && !technology->empty()) el->technology = technology;
            el->tags = tag_list(tags);
            if (is_element && arg(1) && !arg(1)->empty()) el->properties.set("metadata", *arg(1));
            if (type == ElementType::DeploymentNode && arg(4) && !arg(4)->empty())
              el->properties.set("instances", *arg(4));
            if (parent && !parent->empty()) el->parent = parent;
            el->group = current_group();
            if (environment && !environment->empty() && deployment) el->environment = environment;
            el->line = st.line;
          }
          if (archetype) {
            for (const auto& x : archetype->tags) el->tags.push_back(x);
            if (archetype->technology && !archetype->technology->empty() && !(el->technology && !el->technology->empty()))
              el->technology = archetype->technology;
            if (archetype->description && !archetype->description->empty() &&
                !(el->description && !el->description->empty()))
              el->description = archetype->description;
          }
          ws.elements.push_back(el);
          elements.set(key, el);
          if (identifier && !identifier->empty()) {
            alias.set(*identifier, key);
            alias.set(key, key);
          }
          if (st.open)
            stack.emplace_back(ElementFrame{
                key, *type, environment && !environment->empty() && deployment ? environment : std::nullopt});
          continue;
        }
        auto word2 = word(2);
        const bool group_id = t.size() > 1 && t[1].text == "=" && word2 && ascii_lower(*word2) == "group";
        if ((lower == "group" || lower == "enterprise" || group_id) && st.open) {
          std::string name = text_at(group_id ? 3 : 1).value_or("Group");
          auto parent_path = current_group();
          std::string path = parent_path && !parent_path->empty() ? *parent_path + "/" + name : name;
          if (group_id) alias.set(t[0].text, "group:" + path);
          stack.emplace_back(GroupFrame{path});
          continue;
        }
        if (lower == "deploymentenvironment") {
          if (st.open) stack.emplace_back(EnvironmentFrame{text_at(1).value_or("Default")});
          continue;
        }
        if (t.size() > 1 && t[1].text == "=" && word2 && ascii_lower(*word2) == "deploymentenvironment") {
          std::string name = text_at(3).value_or(t[0].text);
          alias.set(t[0].text, "env:" + name);
          if (st.open) stack.emplace_back(EnvironmentFrame{name});
          continue;
        }
        if (lower == "deploymentgroup" ||
            (t.size() > 1 && t[1].text == "=" && word2 && ascii_lower(*word2) == "deploymentgroup"))
          continue;
        if (lower == "group" && !st.open) {
          // `group "Name"` without a body: inside an element it names the element's group.
          if (const auto* f = std::get_if<ElementFrame>(frame); f && t.size() > 1) {
            IrElement* el = element_at(f->key);
            auto parent_path = current_group();
            el->group = parent_path && !parent_path->empty() ? *parent_path + "/" + t[1].text : t[1].text;
          }
          continue;
        }
        if (const auto* f = std::get_if<ElementFrame>(frame)) {
          IrElement* el = element_at(f->key);
          if (lower == "description" && t.size() > 1) el->description = t[1].text;
          else if (lower == "technology" && t.size() > 1) el->technology = t[1].text;
          else if (lower == "tags" || lower == "tag") {
            for (std::size_t i = 1; i < t.size(); ++i)
              for (auto& x : tag_list(t[i].text)) el->tags.push_back(x);
          } else if (lower == "url" && t.size() > 1) el->properties.set("url", t[1].text);
          else if (lower == "instances" && t.size() > 1) el->properties.set("instances", t[1].text);
          else if (lower == "properties" && st.open) stack.emplace_back(PropertiesFrame{el->key, nullptr});
          else if (lower == "perspectives" && st.open) stack.emplace_back(PerspectivesFrame{el->key, nullptr});
          else if (lower == "healthcheck")
            el->properties.set("healthCheck " + text_at(1).value_or(""), text_at(2).value_or(""));
          else {
            drop(st, not_imported(), "element " + lower_or("undefined"));
            skip_if_open();
          }
          continue;
        }
        if (lower == "properties" && st.open) {
          stack.emplace_back(PropertiesFrame{});
          continue;
        }
        drop(st, not_imported(), "model " + lower_or("statement"));
        skip_if_open();
        continue;
      }

      if (std::holds_alternative<ViewsFrame>(*frame)) {
        auto type = lower ? view_type(*lower) : std::nullopt;
        if (type && (st.open || type == ViewType::Filtered)) {
          std::vector<std::string> rest;
          for (std::size_t i = 1; i < t.size(); ++i) rest.push_back(t[i].text);
          auto at = [&](std::size_t i) -> std::optional<std::string> {
            return i < rest.size() ? std::optional(rest[i]) : std::nullopt;
          };
          auto automatic = [&](const std::string& prefix) {
            return prefix + "-" + std::to_string(++view_counts[prefix]);
          };
          auto view = std::make_shared<IrView>();
          view->type = *type;
          view->line = st.line;
          if (type == ViewType::SystemLandscape || type == ViewType::Custom) {
            view->key = at(0) ? *at(0) : automatic(type == ViewType::Custom ? "Custom" : "SystemLandscape");
          } else if (type == ViewType::Filtered) {
            view->key = at(3) ? *at(3) : automatic("Filtered");
            view->base_key = at(0).value_or("");
            view->filter_exclude = ascii_lower(at(1).value_or("")).find("exclude") != std::string::npos;
            std::string tags = at(2).value_or("");
            std::size_t s0 = 0;
            for (std::size_t i = 0; i <= tags.size(); ++i)
              if (i == tags.size() || tags[i] == ',') {
                view->filter_tags.emplace_back(js_trim(std::string_view(tags).substr(s0, i - s0)));
                s0 = i + 1;
              }
          } else if (type == ViewType::Deployment) {
            auto subject = at(0) == "*" ? std::nullopt : resolve(at(0).value_or(""));
            view->key = at(2) ? *at(2) : automatic("Deployment");
            if (subject && !subject->empty()) view->subject = subject;
            const std::string* env = alias.get(at(1).value_or(""));
            view->environment = env ? (starts_with(*env, "env:") ? env->substr(4) : *env) : at(1).value_or("");
          } else {
            auto subject = at(0) == "*" ? std::nullopt : resolve(at(0).value_or(""));
            if (at(0) != "*" && !(subject && !subject->empty()))
              drop(st, "View subject not found: " + at(0).value_or("undefined"), "view subject");
            static const std::map<ViewType, const char*> kPrefix{{ViewType::SystemContext, "SystemContext"},
                                                                 {ViewType::Container, "Container"},
                                                                 {ViewType::Component, "Component"},
                                                                 {ViewType::Dynamic, "Dynamic"}};
            view->key = at(1) ? *at(1) : automatic(kPrefix.at(*type));
            if (subject && !subject->empty()) view->subject = subject;
            if (type == ViewType::Dynamic) view->steps.emplace();
          }
          ws.views.push_back(view);
          if (st.open) stack.emplace_back(ViewFrame{view});
          continue;
        }
        if (lower == "styles" && st.open) {
          stack.emplace_back(StylesFrame{});
          continue;
        }
        if (lower == "theme" || lower == "themes") {
          drop(st, "Themes are not fetched: " + join(t, 1), "theme");
          continue;
        }
        if ((lower == "branding" || lower == "terminology" || lower == "properties" || lower == "configuration") &&
            st.open) {
          stack.emplace_back(SkipFrame{});
          continue;
        }
        if (lower == "image") {
          drop(st, "Image views are not imported", "image view");
          skip_if_open();
          continue;
        }
        drop(st, "View not imported: " + lower_or("undefined"), "views " + lower_or("statement"));
        skip_if_open();
        continue;
      }

      if (std::holds_alternative<ViewFrame>(*frame) || std::holds_alternative<ParallelFrame>(*frame)) {
        const auto* pf = std::get_if<ParallelFrame>(frame);
        std::shared_ptr<IrView> view = pf ? pf->view : std::get<ViewFrame>(*frame).view;
        if (lower == "include" || lower == "exclude") {
          // A group identifier stands for the elements in it.
          auto& list_ref = lower == "include" ? view->include : view->exclude;
          for (std::size_t i = 1; i < t.size(); ++i) {
            const std::string* a = alias.get(t[i].text);
            if (a && starts_with(*a, "group:")) {
              for (IrElement* el : select("element.group==" + t[i].text)) list_ref.push_back(el->key);
            } else {
              list_ref.push_back(t[i].text);
            }
          }
        } else if (lower == "autolayout") {
          std::string d = ascii_lower(word(1).value_or("tb"));
          AutoLayout a;
          a.direction = d == "bt" ? Direction::Up : d == "lr" ? Direction::Right : d == "rl" ? Direction::Left : Direction::Down;
          auto number = [&](std::size_t i) -> std::optional<double> {
            if (i >= t.size()) return std::nullopt;
            double v = js_to_number(t[i].text);
            return std::isnan(v) || v == 0 ? std::nullopt : std::optional(v);
          };
          a.rank_separation = number(2);
          a.node_separation = number(3);
          view->auto_layout = a;
        } else if (st.open && t.empty() && view->type == ViewType::Dynamic) {
          // `{ … }` in a dynamic view: steps in parallel.
          stack.emplace_back(ParallelFrame{view, ++parallel_groups});
        } else if (view->type == ViewType::Dynamic &&
                   std::any_of(t.begin(), t.end(), [](const Token& x) { return x.text == "->"; })) {
          std::size_t a = 0;
          while (t[a].text != "->") ++a;
          IrStep step;
          step.source = a > 0 ? t[a - 1].text : "";
          step.destination = text_at(a + 1).value_or("");
          step.description = text_at(a + 2);
          step.technology = text_at(a + 3);
          if (pf) step.parallel = pf->group;
          view->steps->push_back(std::move(step));
          // A step's body (url, properties) is not imported.
          skip_if_open();
        } else if (view->type == ViewType::Dynamic && !t.empty() &&
                   std::any_of(ws.relationships.begin(), ws.relationships.end(),
                               [&](const auto& r) { return r->key == t[0].text; })) {
          const IrRelationship* r = nullptr;
          for (const auto& x : ws.relationships)
            if (x->key == t[0].text) {
              r = x.get();
              break;
            }
          IrStep step;
          step.source = r->source;
          step.destination = r->destination;
          std::optional<std::string> d = t.size() > 1 ? std::optional(t[1].text) : r->description;
          if (d && !d->empty()) step.description = d;
          view->steps->push_back(std::move(step));
          skip_if_open();
        } else if (lower == "animation" && st.open) {
          drop(st, "Animation steps are not imported", "animation");
          stack.emplace_back(SkipFrame{});
        } else if (lower == "title" || lower == "description" || lower == "default") {
          // Kept out of the model; the key names the view.
        } else if (lower == "properties" && st.open) {
          stack.emplace_back(SkipFrame{});
        } else {
          drop(st, "View statement not imported: " + utf16_prefix(join(t, 0), 60), "view " + lower_or("statement"));
          skip_if_open();
        }
        continue;
      }

      if (std::holds_alternative<StylesFrame>(*frame)) {
        if ((lower == "element" || lower == "relationship") && st.open) {
          auto style = std::make_shared<TagStyle>();
          style->tag = text_at(1).value_or("");
          (lower == "element" ? ws.element_styles : ws.relationship_styles).push_back(style);
          stack.emplace_back(StyleFrame{style});
        } else if ((lower == "light" || lower == "dark") && st.open) {
          // Colour-scheme specific styles: kept with the default ones.
          stack.emplace_back(StylesFrame{});
        } else if (lower == "theme" || lower == "themes") {
          drop(st, "Themes are not fetched: " + join(t, 1), "theme");
        } else {
          drop(st, "Style not imported: " + lower_or("undefined"), "styles " + lower_or("undefined"));
          skip_if_open();
        }
        continue;
      }
      drop(st, not_imported(), "statement");
      skip_if_open();
    }
  };
  run(statements(source, options.file_name));
  return ws;
}

}  // namespace dd::io::structurizr
