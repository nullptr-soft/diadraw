// Port of packages/import/src/plantuml/c4.ts: C4-PlantUML macros read with the vendored
// signatures instead of being expanded.

#include <functional>
#include <map>
#include <variant>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

namespace {

using Args = std::map<std::string, std::string, std::less<>>;

const std::map<std::string, std::vector<std::string>, std::less<>>& signatures() {
  static const std::map<std::string, std::vector<std::string>, std::less<>> kSignatures{
#include "c4_signatures.inc"
  };
  return kSignatures;
}

const Args::mapped_type* arg(const Args& args, std::string_view name) {
  auto it = args.find(name);
  return it == args.end() ? nullptr : &it->second;
}
/// `args.$x` is set and not empty.
bool has(const Args& args, std::string_view name) {
  const auto* v = arg(args, name);
  return v && !v->empty();
}
std::optional<std::string> get(const Args& args, std::string_view name) {
  const auto* v = arg(args, name);
  return v ? std::optional<std::string>(*v) : std::nullopt;
}

/// `Rel_(a, b, "label", ["techn",] "-DOWN->")`, or with an index first in dynamic diagrams.
Args bind_rel_underscore(std::string_view raw) {
  std::vector<std::string> args;
  auto raw_args = split_args(raw);
  for (const auto& a : raw_args) args.push_back(unquote(a));
  std::string direction;
  if (!args.empty()) {
    direction = args.back();
    args.pop_back();
  }
  static const Regex kQuoted("^\\s*\"");
  bool dynamic = args.size() >= 4 && kQuoted.test(raw_args.empty() ? "" : raw_args[0]);
  std::vector<std::string> a = dynamic ? std::vector<std::string>(args.begin() + 1, args.end()) : args;
  auto at = [&](std::size_t i) { return i < a.size() ? a[i] : std::string(); };
  Args out{{"$from", at(0)}, {"$to", at(1)}, {"$label", at(2)}, {"$direction", direction}};
  if (!at(3).empty()) out["$techn"] = at(3);
  if (dynamic && !args.empty() && !args[0].empty()) out["$index"] = args[0];
  return out;
}

/// Arguments of a call by parameter name, using the vendored C4-PlantUML signatures.
Args bind_args(std::string_view name, std::string_view raw) {
  static const std::vector<std::string> kDefault{"$alias", "$label", "$techn", "$descr"};
  auto it = signatures().find(name);
  const auto& params = it == signatures().end() ? kDefault : it->second;
  static const Regex kNamed(R"re(^(\$\w+)\s*=\s*([\s\S]*)$)re");
  Args out;
  std::size_t pos = 0;
  for (const auto& a : split_args(raw)) {
    if (auto named = kNamed.exec(a)) out[named->str(1)] = unquote(named->view(2));
    else if (pos < params.size()) out[params[pos++]] = unquote(a);
  }
  return out;
}

std::optional<Placement> direction_of(std::string_view word) {
  if (word == "U" || word == "Up") return Placement::Above;
  if (word == "D" || word == "Down") return Placement::Below;
  if (word == "L" || word == "Left") return Placement::LeftOf;
  if (word == "R" || word == "Right") return Placement::RightOf;
  return std::nullopt;
}

struct ElementSpec {
  const char* kind;
  const char* shape;
};
const ElementSpec* element_spec(std::string_view base) {
  static const std::map<std::string, ElementSpec, std::less<>> kElements{
      {"Person", {"person", nullptr}},          {"System", {"system", nullptr}},
      {"SystemDb", {"system", "cylinder"}},     {"SystemQueue", {"system", "queue"}},
      {"Container", {"container", nullptr}},    {"ContainerDb", {"container", "cylinder"}},
      {"ContainerQueue", {"container", "queue"}}, {"Component", {"component", nullptr}},
      {"ComponentDb", {"component", "cylinder"}}, {"ComponentQueue", {"component", "queue"}}};
  auto it = kElements.find(base);
  return it == kElements.end() ? nullptr : &it->second;
}

/// Boundaries: the ones that stand for an element become that element; the rest are groups.
const char* boundary_kind(std::string_view name) {
  if (name == "System_Boundary") return "system";
  if (name == "Container_Boundary") return "container";
  if (name == "Enterprise_Boundary" || name == "Boundary") return "group";
  return nullptr;
}

bool is_node(std::string_view name) {
  return name == "Deployment_Node" || name == "Deployment_Node_L" || name == "Deployment_Node_R" || name == "Node" ||
         name == "Node_L" || name == "Node_R";
}

struct ElementFrame {
  std::string key;
};
struct GroupFrame {
  GroupId id;
};

}  // namespace

void parse_c4(Ctx& ctx) {
  ModelBuilder& b = ctx.b;
  std::vector<std::variant<ElementFrame, GroupFrame>> stack;
  std::vector<std::pair<std::string, Style>> tag_styles;
  std::vector<std::pair<std::string, std::vector<std::string>>> tagged;
  int last_rel = 0;

  auto parent = [&]() -> std::optional<std::string> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (const auto* f = std::get_if<ElementFrame>(&*it)) return f->key;
    return std::nullopt;
  };
  auto group = [&]() -> std::optional<GroupId> {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if (const auto* f = std::get_if<GroupFrame>(&*it)) return f->id;
      if (std::holds_alternative<ElementFrame>(*it)) return std::nullopt;
    }
    return std::nullopt;
  };
  auto element = [&](const std::string& alias, const std::string& kind, const Args& args,
                     const std::optional<std::string>& shape = std::nullopt, bool external = false) -> std::string {
    std::string key = ctx.key(alias);
    if (b.has(key)) return key;
    std::vector<std::string> tags;
    for (const auto& t : split(get(args, "$tags").value_or(""), '+'))
      if (!js_trim(t).empty()) tags.push_back(trim(t));
    Properties properties;
    if (has(args, "$sprite")) properties.set("sprite", *arg(args, "$sprite"));
    if (has(args, "$link")) properties.set("link", *arg(args, "$link"));
    if (has(args, "$type")) properties.set("type", *arg(args, "$type"));
    ElementInit init;
    init.kind = kind;
    init.name = plain_label(has(args, "$label") ? *arg(args, "$label") : alias);
    init.local = alias;
    if (shape && !shape->empty()) init.shape = shape;
    if (has(args, "$techn")) init.technology = plain_label(*arg(args, "$techn"));
    if (has(args, "$descr")) init.description = plain_label(*arg(args, "$descr"));
    init.parent = parent();
    init.group = group();
    std::vector<std::string> all;
    if (external) all.emplace_back("external");
    all.insert(all.end(), tags.begin(), tags.end());
    init.tags = std::move(all);
    init.properties = std::move(properties);
    b.add_element(key, std::move(init));
    ctx.include.push_back(key);
    if (!tags.empty()) tagged.emplace_back(key, tags);
    return key;
  };
  auto ref = [&](std::string_view alias) -> std::optional<std::string> {
    std::string key = ctx.key(trim(alias));
    return b.has(key) ? std::optional(key) : std::nullopt;
  };
  auto pin = [&](const std::string& from, const std::string& to, Placement placement) {
    ctx.hints.emplace_back(PinHint{*b.id(to), placement, *b.id(from)});
  };

  // Relationships may name elements declared further down, so they wait until the end.
  std::vector<std::function<void()>> deferred;
  static const Regex kCall(R"re(^(\$?\w+)\s*\(([\s\S]*)\)\s*(\{)?\s*$)re");
  static const Regex kExt("_Ext$");
  static const Regex kRel("^(Bi)?Rel(Index)?(?:_(Back|Neighbor|U|Up|D|Down|L|Left|R|Right)?)?(?:_(Back|Neighbor))?$");
  static const Regex kCustomDirection("(UP|DOWN|LEFT|RIGHT)", "i");
  static const Regex kLay("^Lay_(U|Up|D|Down|L|Left|R|Right|Distance)$");
  static const Regex kLeftRight("^LAYOUT_(LEFT_RIGHT|LANDSCAPE)$");
  static const Regex kAddTag(
      "^(AddElementTag|AddBoundaryTag|AddPersonTag|AddSystemTag|AddContainerTag|AddComponentTag|AddNodeTag|AddExternal\\w*"
      "Tag)$");
  static const Regex kRounded("Rounded", "i");
  static const Regex kEightSided("EightSided", "i");
  static const Regex kDashDot("dash|dot", "i");
  static const Regex kStyling(
      "^(LAYOUT_\\w+|SHOW_\\w+|HIDE_\\w+|Add\\w+|Update\\w+|SetDefault\\w+|SetPropertyHeader|AddProperty|"
      "WithoutPropertyHeader|SetSketchStyle|LAYOUT_AS_SKETCH|title|increment|setIndex|lastIndex|RoundedBoxShape|"
      "EightSidedShape|DashedLine|DottedLine|BoldLine|SolidLine)$");

  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }
    const int line = lines[i].line;
    std::string t = trim(lines[i].text);
    if (t == "}") {
      if (!stack.empty()) stack.pop_back();
      continue;
    }
    auto call = kCall.exec(t);
    if (!call) {
      ctx.drop(line, unsupported_construct(t), t);
      continue;
    }
    std::string name = call->str(1);
    std::string raw_args = call->str(2);
    Args args = bind_args(name, raw_args);
    bool opens = call->has(3);
    std::string base = kExt.replace(name, "", false);
    const std::string ln = std::to_string(line);

    if (const ElementSpec* spec = element_spec(base)) {
      std::string key = element(get(args, "$alias").value_or("e" + ln), spec->kind, args,
                                spec->shape ? std::optional<std::string>(spec->shape) : std::nullopt,
                                name.size() >= 4 && name.compare(name.size() - 4, 4, "_Ext") == 0);
      if (opens) stack.emplace_back(ElementFrame{key});
      continue;
    }
    if (const char* kind = boundary_kind(name)) {
      if (std::string_view(kind) == "group") {
        std::string label = get(args, "$label") ? *get(args, "$label") : get(args, "$alias").value_or("Boundary");
        GroupId id = b.add_group(plain_label(label), group());
        if (opens) stack.emplace_back(GroupFrame{id});
      } else {
        std::string key = element(get(args, "$alias").value_or("b" + ln), kind, args);
        if (opens) stack.emplace_back(ElementFrame{key});
      }
      continue;
    }
    if (is_node(name)) {
      std::string key = element(get(args, "$alias").value_or("n" + ln), "system", args, std::string("cube"));
      if (opens) stack.emplace_back(ElementFrame{key});
      continue;
    }
    if (auto m = kRel.exec(name)) {
      // The match points into `name`, which this iteration owns: keep what the closure needs.
      const bool bi = m->has(1), indexed = m->has(2);
      const std::string way = m->str(3), back_word = m->str(4);
      deferred.emplace_back([&, bi, indexed, way, back_word, name, raw_args, args, line, ln] {
        Args rel_args = indexed ? bind_args("RelIndex", raw_args) : name == "Rel_" ? bind_rel_underscore(raw_args) : args;
        // PlantUML creates elements a relationship names but nothing declares.
        auto named = [&](const std::optional<std::string>& alias, const std::string& side) -> std::string {
          if (auto found = ref(alias.value_or(""))) return *found;
          ctx.implicit();
          std::string a = alias && !alias->empty() ? *alias : side + ln;
          return element(a, "system", Args{{"$label", alias && !alias->empty() ? *alias : side}});
        };
        std::string from = named(get(rel_args, "$from") ? get(rel_args, "$from") : get(rel_args, "$alias"), "from");
        std::string to = named(get(rel_args, "$to"), "to");
        bool back = way == "Back" || back_word == "Back";
        const std::string& s = back ? to : from;
        const std::string& d = back ? from : to;
        Properties properties;
        std::optional<std::string> index = get(rel_args, "$index") ? get(rel_args, "$index") : get(rel_args, "$e_index");
        if (index) {
          if (!index->empty()) properties.set("order", *index);
        } else if (indexed) {
          properties.set("order", std::to_string(++last_rel));
        }
        if (has(rel_args, "$tags")) properties.set("tags", *arg(rel_args, "$tags"));
        RelationshipInit init;
        if (has(rel_args, "$label")) init.label = plain_label(*arg(rel_args, "$label"));
        if (has(rel_args, "$techn")) init.technology = plain_label(*arg(rel_args, "$techn"));
        init.bidirectional = bi;
        init.properties = std::move(properties);
        b.add_relationship(s, d, std::move(init), line);
        // `Rel_(a, b, "label", "-DOWN->")` carries its direction in the arrow.
        std::optional<Placement> dir;
        if (!way.empty()) {
          dir = direction_of(way);
        } else if (const std::string direction = get(rel_args, "$direction").value_or("");
                   auto custom = kCustomDirection.exec(direction)) {
          dir = direction_of(std::string(1, static_cast<char>(js::to_upper(static_cast<std::uint32_t>(custom->view(1)[0])))));
        }
        if (dir) pin(from, to, *dir);
      });
      continue;
    }
    if (auto m = kLay.exec(name)) {
      std::vector<std::string> all;
      for (const auto& a : split_args(raw_args)) all.push_back(unquote(a));
      auto dir = direction_of(m->view(1));
      deferred.emplace_back([&, all, dir] {
        auto from = ref(all.empty() ? "" : all[0]);
        auto to = ref(all.size() > 1 ? all[1] : "");
        if (from && to && dir) pin(*from, *to, *dir);
      });
      continue;
    }
    if (kLeftRight.test(name)) {
      ctx.direction = Direction::Right;
      continue;
    }
    if (name == "LAYOUT_TOP_DOWN") {
      ctx.direction = Direction::Down;
      continue;
    }
    if (kAddTag.test(name)) {
      std::string tag = get(args, "$tagStereo").value_or("");
      Style style;
      if (has(args, "$bgColor")) style.set("fill", *arg(args, "$bgColor"));
      if (has(args, "$fontColor")) style.set("fontColor", *arg(args, "$fontColor"));
      if (has(args, "$borderColor")) style.set("stroke", *arg(args, "$borderColor"));
      if (has(args, "$shape") && kRounded.test(*arg(args, "$shape"))) style.set("rounded", "1");
      if (has(args, "$shape") && kEightSided.test(*arg(args, "$shape"))) style.set("shape", "octagon");
      if (has(args, "$borderStyle") && kDashDot.test(*arg(args, "$borderStyle"))) style.set("dashed", "1");
      bool found = false;
      for (auto& [k, v] : tag_styles)
        if (k == tag) {
          v = style;
          found = true;
        }
      if (!found) tag_styles.emplace_back(tag, style);
      continue;
    }
    if (kStyling.test(name)) {
      ctx.style(line, name);
      continue;
    }
    // Calls of icon-library macros inside C4 diagrams (sprites used as elements).
    if (ctx.libraries.size() > 1 && has(args, "$alias")) {
      Args with_label = args;
      if (!get(args, "$label")) with_label["$label"] = *arg(args, "$alias");
      std::string key = element(*arg(args, "$alias"), "component", with_label, icon_shape(ctx, name));
      b.set_property(key, "icon", name);
      continue;
    }
    ctx.drop(line, "C4 " + name, t);
  }

  for (const auto& run : deferred) run();

  // Tag styles apply to every element carrying the tag.
  for (const auto& [key, tags] : tagged) {
    Style style;
    for (const auto& tag : tags)
      for (const auto& [k, v] : tag_styles)
        if (k == tag)
          for (const auto& [sk, sv] : v) style.set(sk, sv);
    if (style.empty()) continue;
    std::optional<std::string> shape;
    Style rest;
    for (const auto& [sk, sv] : style) {
      if (sk == "shape") shape = sv;
      else rest.set(sk, sv);
    }
    b.patch(key, [&](Element& e) {
      e.style = rest;
      if (shape && !shape->empty()) e.shape = shape;
    });
  }
}

}  // namespace dd::io::plantuml
