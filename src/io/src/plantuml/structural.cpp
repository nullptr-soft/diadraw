// Port of packages/import/src/plantuml/structural.ts: class, object, component, deployment, use
// case and state diagrams.

#include <algorithm>
#include <map>
#include <variant>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

namespace {

struct ClassKeyword {
  const char* name;
  const char* kind;
  const char* stereotype;  // nullptr: none
};
const std::vector<ClassKeyword>& class_keywords() {
  static const std::vector<ClassKeyword> kKeywords{
      {"class", "class", nullptr},          {"abstract class", "class", "abstract"},
      {"abstract", "class", "abstract"},    {"interface", "class", "interface"},
      {"enum", "class", "enumeration"},     {"annotation", "class", "annotation"},
      {"entity", "class", "entity"},        {"struct", "class", "struct"},
      {"protocol", "class", "protocol"},    {"exception", "class", "exception"},
      {"metaclass", "class", "metaclass"},  {"stereotype", "class", "stereotype"},
      {"record", "class", "record"},        {"dataclass", "class", "dataclass"},
      {"object", "object", nullptr},        {"map", "object", "map"},
      {"json", "object", "json"}};
  return kKeywords;
}
const ClassKeyword* class_keyword(std::string_view k) {
  for (const auto& c : class_keywords())
    if (k == c.name) return &c;
  return nullptr;
}

struct StateShape {
  const char* shape;
  const char* kind;
};
const StateShape* state_shape(std::string_view s) {
  static const std::map<std::string, StateShape, std::less<>> kShapes{
      {"fork", {"rect", "process"}},           {"join", {"rect", "process"}},
      {"choice", {"rhombus", "decision"}},     {"end", {"circle", "end"}},
      {"history", {"circle", "process"}},      {"history*", {"circle", "process"}},
      {"entrypoint", {"circle", "process"}},   {"exitpoint", {"circle", "process"}},
      {"inputpin", {"rect", "process"}},       {"outputpin", {"rect", "process"}},
      {"expansioninput", {"rect", "process"}}, {"expansionoutput", {"rect", "process"}},
      {"sdlreceive", {"step", "process"}}};
  auto it = kShapes.find(s);
  return it == kShapes.end() ? nullptr : &it->second;
}

const std::string kName =
    R"re("[^"]*"|\[[^\]]*\]|\(\)\s*"[^"]*"|\(\)\s*[\w.]+|\([^)]*\)|:[^:]+:|[\p{L}\p{N}_.$:@-]+(?:<[^>]*>)?)re";
const std::string kEndpoint =
    R"re(\[\*\]|\[H\*?\]|"[^"]*"|\[[^\]]+\]|\(\)\s*"[^"]*"|\(\)\s*[\w.]+|\([^)*]+\)|:[^:]+:|[\p{L}\p{N}_.$@-]+(?:::[\p{L}\p{N}_.-]+)?(?:<[^>]*>)?)re";

const Regex& decl_regex() {
  static const Regex kDecl = [] {
    std::vector<std::string> keys;
    for (const auto& c : class_keywords()) keys.emplace_back(c.name);
    keys.emplace_back("state");
    for (const auto& [k, _] : element_keywords()) keys.push_back(k);
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return b.size() < a.size(); });
    std::string alternatives;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      std::string k = keys[i];
      if (auto sp = k.find(' '); sp != std::string::npos) k.replace(sp, 1, "\\s+");
      alternatives += (i ? "|" : "") + k;
    }
    return Regex("^(" + alternatives + ")\\s+(" + kName + ")(?:\\s+as\\s+(" + kName +
                     R"re())?((?:\s*<<[^>]*>>)*)((?:\s*(?:extends|implements)\s+[\p{L}\p{N}_.,\s<>]+?)*)(?:\s*(?:#|\$)[\w#;.:-]+)*(?:\s*\[\[[^\]]*\]\])?\s*(\{\s*\}?|:\s*(.*))?\s*$)re",
                 "iu");
  }();
  return kDecl;
}

const Regex& relation_regex() {
  static const Regex kRelation("^(" + kEndpoint + R"re()\s*(?:"([^"]*)"\s*)?(\S*?[-.=~]\S*?)\s*(?:"([^"]*)"\s*)?()re" +
                                   kEndpoint + R"re()\s*(?::\s*(.*))?$)re",
                               "u");
  return kRelation;
}

struct ReadName {
  std::string name;
  std::optional<std::string> kind;
  std::optional<std::string> shape;
};

/// Reads a name form: `"x"`, `[x]`, `(x)`, `() x`, `:x:` or a plain word.
ReadName read_name(std::string_view raw) {
  std::string t = trim(raw);
  auto inner = [&] { return std::string_view(t).substr(1, t.size() - 2); };
  if (!t.empty() && t.front() == '[' && t.back() == ']') return {plain_label(inner()), "component", std::nullopt};
  if (t.rfind("()", 0) == 0) return {plain_label(unquote(std::string_view(t).substr(2))), "component", "interface"};
  if (!t.empty() && t.front() == '(' && t.back() == ')') return {plain_label(inner()), "process", "ellipse"};
  if (t.size() > 2 && t.front() == ':' && t.back() == ':') return {plain_label(inner()), "person", "actor"};
  return {plain_label(unquote(t)), std::nullopt, std::nullopt};
}

struct ElementFrame {
  std::string key;
  std::string name;
};
struct GroupFrame {
  GroupId id;
};
struct SkipFrame {};
using Frame = std::variant<ElementFrame, GroupFrame, SkipFrame>;

struct Declaration {
  std::string kind;
  std::optional<std::string> label;
  std::optional<std::string> shape;
  std::optional<std::string> stereotype;
  std::optional<std::string> description;
};

}  // namespace

void parse_structural(Ctx& ctx, const std::string& type) {
  ModelBuilder& b = ctx.b;
  std::map<std::string, std::string, std::less<>> names;  // PlantUML name or alias → builder key
  std::vector<Frame> stack;
  struct Members {
    std::vector<std::string> attributes, methods;
  };
  std::vector<std::pair<std::string, Members>> members;
  int note_count = 0;

  auto name_of = [&](std::string_view n) -> const std::string* {
    auto it = names.find(n);
    return it == names.end() ? nullptr : &it->second;
  };
  auto scope_key = [&]() -> std::optional<std::string> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (const auto* f = std::get_if<ElementFrame>(&*it)) return f->key;
    return std::nullopt;
  };
  auto scope_name = [&]() -> std::string {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (const auto* f = std::get_if<ElementFrame>(&*it)) return f->name;
    return "";
  };
  auto group = [&]() -> std::optional<GroupId> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if (const auto* f = std::get_if<GroupFrame>(&*it)) return f->id;
      if (std::holds_alternative<ElementFrame>(*it)) return std::nullopt;
    }
    return std::nullopt;
  };
  auto description_of = [&](const std::string& key) -> std::optional<std::string> {
    auto id = b.id(key);
    const Element* el = id ? b.model().elements.find(*id) : nullptr;
    return el ? el->description : std::nullopt;
  };
  auto append_description = [&](const std::string& key, const std::string& more) {
    std::vector<std::string> parts;
    if (auto d = description_of(key); d && !d->empty()) parts.push_back(*d);
    if (!more.empty()) parts.push_back(more);
    std::string joined = join(parts, "\n");
    b.patch(key, [&](Element& e) { e.description = joined; });
  };

  auto declare = [&](const std::string& name, const Declaration& init) -> std::string {
    if (const std::string* known = name_of(name); known && !known->empty()) {
      std::string k = *known;
      // A later declaration says what an implicitly created element really is.
      if (init.stereotype && !init.stereotype->empty()) b.set_property(k, "stereotype", *init.stereotype);
      if (init.label && !init.label->empty() && *init.label != name) {
        std::string label = *init.label;
        b.patch(k, [&](Element& e) { e.name = label; });
      }
      if (init.description && !init.description->empty()) append_description(k, *init.description);
      return k;
    }
    std::string key = ctx.key(scope_name() + "/" + name);
    auto parent = scope_key();
    auto g = group();
    Properties properties;
    if (init.stereotype && !init.stereotype->empty()) properties.set("stereotype", *init.stereotype);
    // `Shape<T>` is referred to as `Shape` elsewhere.
    static const Regex kGeneric("<.*>$");
    static const Regex kGenericTail("<.*$");
    static const Regex kQualifier("^.*[.:]");
    std::string base = kGeneric.replace(name, "", false);
    if (base != name && !names.count(base)) names[base] = key;
    ElementInit e;
    e.kind = init.kind;
    e.name = init.label ? *init.label : name;
    e.local = kQualifier.replace(kGenericTail.replace(name, "", false), "", false);
    if (init.shape && !init.shape->empty()) e.shape = init.shape;
    if (init.description && !init.description->empty()) e.description = init.description;
    e.parent = parent;
    e.group = g;
    e.properties = std::move(properties);
    b.add_element(key, std::move(e));
    names[name] = key;
    ctx.include.push_back(key);
    return key;
  };
  auto declare_as = [&](const std::string& id, const std::string& kind, const std::string& label,
                        const std::string& shape) { return declare(id, {kind, label, shape, std::nullopt, std::nullopt}); };

  /// The element an endpoint names, declaring it with a sensible kind when it is new.
  auto endpoint = [&](std::string_view raw, bool source) -> std::string {
    std::string t = trim(raw);
    if (t == "[*]") {
      std::string id = scope_name() + "#" + (source ? "start" : "end");
      if (const std::string* k = name_of(id)) return *k;
      return declare_as(id, source ? "start" : "end", "", "circle");
    }
    static const Regex kHistory("^\\[H\\*?\\]$");
    if (kHistory.test(t)) {
      std::string id = scope_name() + "#" + t;
      if (const std::string* k = name_of(id)) return *k;
      return declare_as(id, "process", t.substr(1, t.size() - 2), "circle");
    }
    static const Regex kPort("^([\\w.]+)::([\\w.-]+)$");
    auto port = kPort.exec(t);
    ReadName r = read_name(port ? port->view(1) : std::string_view(t));
    if (const std::string* k = name_of(r.name)) return *k;
    static const Regex kGeneric("<.*>$");
    std::string generic = kGeneric.replace(r.name, "", false);
    if (const std::string* k = name_of(generic)) return *k;
    std::string kind = r.kind ? *r.kind
                       : type == "class"   ? "class"
                       : type == "object"  ? "object"
                       : type == "state"   ? "process"
                       : type == "usecase" ? "process"
                                           : "component";
    Declaration d{kind, std::nullopt, r.shape, std::nullopt, std::nullopt};
    if (type == "state" && !r.kind) d.shape = "rounded";
    if (type == "usecase" && !r.kind) d.shape = "ellipse";
    return declare(r.name, d);
  };

  auto add_member = [&](const std::string& key, std::string_view raw) {
    static const Regex kModifiers("\\{(static|abstract|classifier|field|method)\\}\\s*", "g");
    static const Regex kSeparator("^(--|\\.\\.|==|__)");
    std::string text = trim(kModifiers.replace(raw, "", true));
    if (text.empty() || kSeparator.test(text)) return;
    Members* m = nullptr;
    for (auto& [k, v] : members)
      if (k == key) m = &v;
    if (!m) m = &members.emplace_back(key, Members{}).second;
    (text.find('(') != std::string::npos ? m->methods : m->attributes).push_back(text);
  };

  static const Regex kEnd("^end\\s*(package|namespace|frame|folder|node|rectangle|cloud|component)?$", "i");
  static const Regex kRegion("^(--+|\\|\\|+)$");
  static const Regex kPackage(R"re(^(package|namespace|together)\b\s*(.*?)\s*(<<[^>]*>>)?\s*(#[\w#]+)?\s*\{$)re", "i");
  static const Regex kAsAlias("\\s+as\\s+\\w+$");
  static const Regex kNote(R"re(^note\s+(left|right|top|bottom|over)?\s*(?:of\s+)?([^:]*?)\s*(?::\s*(.*))?$)re", "i");
  static const Regex kNoteStart("^note\\b", "i");
  static const Regex kEndNote("^end\\s*note$", "i");
  static const Regex kFloatingQuoted(R"re(^"(.*)"\s+as\s+(\w+)$)re");
  static const Regex kFloatingAs("^as\\s+(\\w+)$");
  static const Regex kOnLink("^on\\s+link", "i");
  static const Regex kMember(R"re(^([\w.$"]+[\w"]?)\s*:\s*(.*)$)re");
  static const Regex kPortDecl("^(port|portin|portout)\\s+([\\p{L}\\p{N}_.]+)", "iu");
  static const Regex kOrder("\\s+order\\s+-?\\d+$", "i");
  static const Regex kMulti("^(\\w+)\\s+([\\p{L}\\p{N}_.]+)\\s*\\[\\s*$", "u");
  static const Regex kWhitespace("\\s+", "g");
  static const Regex kStereotypes("<<([^>]*)>>", "g");
  static const Regex kQuotedStart(R"re(^["[(:])re");
  static const Regex kInherit(R"re((extends|implements)\s+([\p{L}\p{N}_.,\s<>]+?)(?=\s+(?:extends|implements)\b|$))re", "gu");
  static const Regex kGenericTail("<.*$");
  static const Regex kArrowOnly(R"re(=>)re");
  static const Regex kLeftOpen("^<\\|?$");
  static const Regex kLabelArrows(R"re(^\s*<\s*|\s*>\s*$)re", "g");
  static const Regex kShorthand(
      R"re(^(\[[^\]]+\]|\([^)*]+\)|:[^:]+:|\(\)\s*(?:"[^"]*"|[\w.]+))(?:\s+as\s+([\w.]+))?(?:\s*<<[^>]*>>)?(?:\s*#\w+)?\s*$)re");
  static const Regex kIconAlias(R"re(^\$?(\w+)\s*\(([^)]*)\)\s+as\s+([\w.]+))re");
  static const Regex kIconMacro(R"re(^(\$?[A-Za-z_]\w*)\s*\(\s*([\w.]+)\s*(?:,\s*([\s\S]*))?\)\s*(\{)?\s*$)re");
  static const Regex kIconArgs(R"re("[^"]*"|[^,]+)re", "g");

  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }
    const int line = lines[i].line;
    std::string t = trim(lines[i].text);

    if (t == "}" || kEnd.test(t)) {
      if (stack.empty()) ctx.drop(line, "unmatched }");
      else stack.pop_back();
      continue;
    }
    // Concurrent regions inside a composite state.
    if (type == "state" && kRegion.test(t)) continue;

    // Packages and namespaces are groups; `together { … }` too.
    if (auto m = kPackage.exec(t)) {
      std::string name = plain_label(unquote(kAsAlias.replace(m->view(2), "", false)));
      if (name.empty()) name = js::to_lower(m->view(1));
      if (js::to_lower(m->view(1)) == "together") {
        stack.emplace_back(SkipFrame{});
        continue;
      }
      stack.emplace_back(GroupFrame{b.add_group(name, group())});
      continue;
    }

    // Notes become note elements, linked to what they are about.
    if (auto m = kNote.exec(t); m && kNoteStart.test(t)) {
      std::optional<std::string> body = m->get(3);
      std::string target = trim(m->view(2));
      if (!body) {
        std::vector<std::string> rest;
        while (++i < lines.size() && !kEndNote.test(trim(lines[i].text))) rest.push_back(trim(lines[i].text));
        body = join(rest, "\n");
      }
      auto floating = kFloatingQuoted.exec(target);
      if (!floating) floating = kFloatingAs.exec(target);
      if (kOnLink.test(target)) {
        ctx.drop(line, "note on link");
        continue;
      }
      std::string note_name = floating ? (floating->has(2) ? floating->str(2) : floating->str(1))
                                       : "note" + std::to_string(++note_count);
      std::string label = plain_label(floating && floating->has(2) ? floating->view(1) : std::string_view(*body));
      std::string key = declare(note_name, {"note", label, "note", std::nullopt, std::nullopt});
      if (!floating && !target.empty())
        if (const std::string* about = name_of(read_name(target).name)) {
          RelationshipInit init;
          init.line = LineStyle::Dashed;
          init.style = Style{{"endArrow", "none"}, {"dashed", "1"}};
          b.add_relationship(key, *about, std::move(init));
        }
      continue;
    }

    // `Name : text` adds a member (class) or a description line (state).
    if (auto m = kMember.exec(t); m && !relation_regex().test(t) && names.count(unquote(m->view(1)))) {
      std::string key = names[unquote(m->view(1))];
      if (type == "class" || type == "object") add_member(key, m->view(2));
      else append_description(key, plain_label(m->view(2)));
      continue;
    }

    // Ports belong to the component they are declared in; links to them attach there.
    if (auto m = kPortDecl.exec(t)) {
      auto owner = scope_key();
      if (owner) {
        b.add_port(*owner, m->str(2));
        names[m->str(2)] = *owner;
      } else {
        ctx.drop(line, "port outside a component");
      }
      continue;
    }
    // Declarations.
    t = kOrder.replace(t, "", false);
    // `component api [` … `]`: a multi-line label.
    if (auto multi = kMulti.exec(t)) {
      std::vector<std::string> body;
      while (++i < lines.size() && trim(lines[i].text) != "]") body.push_back(trim(lines[i].text));
      std::string joined = join(body, "\\n");
      std::replace(joined.begin(), joined.end(), '"', '\'');
      t = multi->str(1) + " \"" + joined + "\" as " + multi->str(2);
    }
    if (auto m = decl_regex().exec(t)) {
      std::string keyword = kWhitespace.replace(js::to_lower(m->view(1)), " ", true);
      ReadName a = read_name(m->view(2));
      std::optional<ReadName> b2;
      if (m->has(3)) b2 = read_name(m->view(3));
      // `Name as alias`, `"Long name" as alias`, or `alias as "Long name"`.
      bool quoted_second =
          m->has(3) && kQuotedStart.test(trim(m->view(3))) && !kQuotedStart.test(trim(m->view(2)));
      std::string alias = b2 ? (quoted_second ? a.name : b2->name) : a.name;
      std::string label = b2 ? (quoted_second ? b2->name : a.name) : a.name;
      std::vector<std::string> stereotypes;
      for (const auto& s : kStereotypes.match_all(m->view(4))) stereotypes.push_back(trim(s.view(1)));
      std::string inherits = m->str(5);
      std::optional<std::string> body;
      if (m->has(6)) body = trim(m->view(6));
      std::optional<std::string> description;
      if (m->has(7) && !m->view(7).empty()) description = plain_label(m->view(7));
      std::string kind;
      std::optional<std::string> shape;
      std::string stereotype = join(stereotypes, ", ");
      if (keyword == "state") {
        const StateShape* special = nullptr;
        for (const auto& s : stereotypes)
          if ((special = state_shape(js::to_lower(s)))) break;
        kind = special ? special->kind : "process";
        shape = special ? special->shape : "rounded";
      } else if (const ClassKeyword* ck = class_keyword(keyword)) {
        kind = ck->kind;
        std::vector<std::string> parts;
        if (ck->stereotype) parts.emplace_back(ck->stereotype);
        for (const auto& s : stereotypes)
          if (!s.empty()) parts.push_back(s);
        stereotype = join(parts, ", ");
      } else {
        const KeywordSpec* spec = element_keyword(keyword);
        kind = spec ? spec->kind : "component";
        if (spec) shape = spec->shape;
        if (keyword == "usecase") shape = "ellipse";
      }
      Declaration d{kind, label, shape, std::nullopt, description};
      if (!stereotype.empty()) d.stereotype = stereotype;
      std::string key = declare(alias, d);
      // `class A extends B implements C, D`: generalisation and realisation.
      for (const auto& g : kInherit.match_all(inherits)) {
        for (const auto& raw_parent : split(g.view(2), ',')) {
          std::string parent = kGenericTail.replace(trim(raw_parent), "", false);
          if (parent.empty()) continue;
          std::string to = endpoint(parent, false);
          bool implements = g.view(1) == "implements";
          RelationshipInit init;
          init.line = implements ? LineStyle::Dashed : LineStyle::Solid;
          Style style{{"endArrow", "block"}, {"endFill", "0"}};
          if (implements) style.set("dashed", "1");
          init.style = std::move(style);
          b.add_relationship(key, to, std::move(init));
        }
      }
      if (keyword == "port" || keyword == "portin" || keyword == "portout") {
        // Ports declared inside a component: also a port of the parent element.
        if (auto parent = scope_key()) b.add_port(*parent, alias);
      }
      if (body && !body->empty() && body->front() == '{' && body->back() != '}') {
        const ClassKeyword* ck = class_keyword(keyword);
        bool object_like = keyword == "object" || keyword == "map" || keyword == "json";
        if (ck && !object_like) {
          // A class body: members until the closing brace.
          while (++i < lines.size() && trim(lines[i].text) != "}") add_member(key, lines[i].text);
        } else if (object_like) {
          while (++i < lines.size() && trim(lines[i].text) != "}")
            add_member(key, kArrowOnly.replace(lines[i].text, "=", false));
        } else {
          stack.emplace_back(ElementFrame{key, alias});
        }
      }
      continue;
    }

    // Relationships.
    if (auto m = relation_regex().exec(t)) {
      std::string arrow_text = m->str(3);
      auto am = arrow_regex().exec(arrow_text);
      if (am && am->view(0) == arrow_text) {
        Arrow arrow = parse_arrow(*am);
        std::string from = endpoint(m->view(1), true);
        std::string to = endpoint(m->view(5), false);
        std::optional<std::string> source_mult = m->get(2), target_mult = m->get(4);
        // `a <-- b` points from b to a: keep relationships in the arrow's direction.
        if (!arrow.left.empty() && arrow.right.empty() && kLeftOpen.test(arrow.left)) {
          std::swap(from, to);
          std::swap(source_mult, target_mult);
          arrow.right = arrow.left == "<|" ? "|>" : ">";
          arrow.left.clear();
        }
        std::optional<std::string> label;
        if (m->has(6) && !m->view(6).empty()) label = plain_label(kLabelArrows.replace(m->view(6), "", true));
        if (label && label->empty()) label.reset();
        auto left = head_style(arrow.left);
        auto right = head_style(arrow.right);
        Style style{{"endArrow", right ? right->arrow : "none"}};
        if (right && right->fill) style.set("endFill", *right->fill);
        if (left) {
          style.set("startArrow", left->arrow);
          if (left->fill) style.set("startFill", *left->fill);
        }
        if (arrow.dashed) style.set("dashed", "1");
        Properties properties;
        if (source_mult && !source_mult->empty()) properties.set("uml.sourceMultiplicity", *source_mult);
        if (target_mult && !target_mult->empty()) properties.set("uml.targetMultiplicity", *target_mult);
        if (arrow.hidden) {
          if (auto hint = direction_hint(*b.id(from), *b.id(to), arrow.direction.value_or("down")))
            ctx.hints.push_back(*hint);
          continue;
        }
        RelationshipInit init;
        if (label) init.label = label;
        init.line = arrow.dashed ? LineStyle::Dashed : LineStyle::Solid;
        init.style = std::move(style);
        init.properties = std::move(properties);
        b.add_relationship(from, to, std::move(init), line);
        if (auto hint = direction_hint(*b.id(from), *b.id(to), arrow.direction)) ctx.hints.push_back(*hint);
        continue;
      }
    }

    // Shorthand element forms on their own line: `[Comp]`, `(Use case)`, `:Actor:`, `() Iface`, with `as`.
    if (auto m = kShorthand.exec(t)) {
      ReadName r = read_name(m->view(1));
      declare(m->has(2) ? m->str(2) : r.name, {r.kind ? *r.kind : "component", r.name, r.shape, std::nullopt, std::nullopt});
      continue;
    }

    // `$AWSIcon(Lambda, "Handler", "AWS Lambda") as fn` (macro with an alias after it).
    if (auto m = kIconAlias.exec(t)) {
      std::vector<std::string> parts;
      for (const auto& x : split(m->view(2), ',')) parts.push_back(plain_label(unquote(trim(x))));
      auto part = [&](std::size_t k) { return k < parts.size() ? parts[k] : std::string(); };
      std::string label = !part(1).empty() ? part(1) : !part(0).empty() ? part(0) : m->str(3);
      std::string key = declare(m->str(3), {"component", label, icon_shape(ctx, !part(0).empty() ? part(0) : m->str(1)),
                                            std::nullopt, std::nullopt});
      b.set_property(key, "icon", parts.empty() ? m->str(1) : parts[0]);
      if (!part(2).empty()) {
        std::string tech = part(2);
        b.patch(key, [&](Element& e) { e.technology = tech; });
      }
      continue;
    }
    // An icon-library macro (AWS, Azure, …): `Lambda(fn, "Handler", "Node")`.
    if (auto m = kIconMacro.exec(t); m && !ctx.libraries.empty()) {
      std::string icon = m->str(1);
      std::vector<std::string> args;
      for (const auto& x : kIconArgs.match_all(m->view(3))) args.push_back(plain_label(unquote(trim(x.view(0)))));
      std::string key = declare(m->str(2), {"component", !args.empty() && !args[0].empty() ? args[0] : m->str(2),
                                            icon_shape(ctx, icon), std::nullopt, std::nullopt});
      b.set_property(key, "icon", icon);
      std::vector<std::string> libs;
      for (const auto& l : ctx.libraries)
        if (l != "C4") libs.push_back(l);
      b.set_property(key, "iconLibrary", join(libs, ","));
      if (args.size() > 1 && !args[1].empty()) {
        std::string tech = args[1];
        b.patch(key, [&](Element& e) { e.technology = tech; });
      }
      if (m->has(4)) stack.emplace_back(ElementFrame{key, m->str(2)});
      continue;
    }

    ctx.drop(line, unsupported_construct(t), t);
  }

  for (const auto& [key, mem] : members) {
    if (!mem.attributes.empty()) b.set_property(key, "uml.attributes", join(mem.attributes, "\n"));
    if (!mem.methods.empty()) b.set_property(key, "uml.methods", join(mem.methods, "\n"));
  }
}

}  // namespace dd::io::plantuml
