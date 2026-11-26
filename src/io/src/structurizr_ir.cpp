// Port of packages/import/src/structurizr/ir.ts (buildModel) and structurizr/index.ts.

#include <algorithm>
#include <cmath>
#include <functional>
#include <regex>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/layout/types.hpp"
#include "structurizr_ir.hpp"

namespace dd::io {

namespace structurizr {

const char* type_name(ElementType type) {
  switch (type) {
    case ElementType::Person: return "person";
    case ElementType::SoftwareSystem: return "softwareSystem";
    case ElementType::Container: return "container";
    case ElementType::Component: return "component";
    case ElementType::Element: return "element";
    case ElementType::DeploymentNode: return "deploymentNode";
    case ElementType::InfrastructureNode: return "infrastructureNode";
    case ElementType::ContainerInstance: return "containerInstance";
    case ElementType::SoftwareSystemInstance: return "softwareSystemInstance";
  }
  return "element";
}

std::vector<std::string> split_list(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == sep) {
      std::string_view part = js_trim(s.substr(start, i - start));
      if (!part.empty()) out.emplace_back(part);
      start = i + 1;
    }
  return out;
}

std::string ascii_lower(std::string_view s) {
  std::string out(s);
  for (auto& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

std::string utf16_prefix(std::string_view s, std::size_t n) {
  return std::string(s.substr(0, byte_offset(s, n)));
}

namespace {

struct Kind {
  const char* kind;
  const char* shape;  // nullptr: none
  const char* tag;
};

Kind kind_of(ElementType type) {
  switch (type) {
    case ElementType::Person: return {"person", nullptr, "Person"};
    case ElementType::SoftwareSystem: return {"system", nullptr, "Software System"};
    case ElementType::Container: return {"container", nullptr, "Container"};
    case ElementType::Component: return {"component", nullptr, "Component"};
    case ElementType::Element: return {"process", nullptr, "Element"};
    case ElementType::DeploymentNode: return {"system", "cube", "Deployment Node"};
    case ElementType::InfrastructureNode: return {"component", nullptr, "Infrastructure Node"};
    case ElementType::ContainerInstance: return {"container", nullptr, "Container Instance"};
    case ElementType::SoftwareSystemInstance: return {"system", nullptr, "Software System Instance"};
  }
  return {"process", nullptr, "Element"};
}

/// Structurizr shapes → DiaDraw base shapes.
const std::string* shape_for(std::string_view name) {
  static const std::map<std::string, std::string, std::less<>> kShapes{
      {"box", "rect"},          {"roundedbox", "rounded"},  {"circle", "circle"},
      {"ellipse", "ellipse"},   {"hexagon", "hexagon"},     {"diamond", "rhombus"},
      {"cylinder", "cylinder"}, {"bucket", "cylinder"},     {"pipe", "queue"},
      {"person", "person"},     {"robot", "actor"},         {"folder", "folder"},
      {"webbrowser", "rect"},   {"window", "rect"},         {"terminal", "rect"},
      {"shell", "rect"},        {"mobiledeviceportrait", "rect"}, {"mobiledevicelandscape", "rect"},
      {"component", "rect"}};
  auto it = kShapes.find(ascii_lower(name));
  return it == kShapes.end() ? nullptr : &it->second;
}

/// `s.split(sep)`, untrimmed.
std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == sep) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  return out;
}

using Counts = std::vector<std::pair<std::string, int>>;
void count(Counts& counts, const std::string& key) {
  for (auto& [k, n] : counts)
    if (k == key) {
      ++n;
      return;
    }
  counts.emplace_back(key, 1);
}

bool dash_or_dot(std::string_view v) {
  std::string l = ascii_lower(v);
  return l.find("dash") != std::string::npos || l.find("dot") != std::string::npos;
}

struct MappedStyle {
  Style style;
  std::optional<std::string> shape;
  std::optional<std::string> icon;
};

/// Style properties DiaDraw maps; the rest are kept as passthrough and counted.
MappedStyle map_element_style(const Record& props, Counts& unknown) {
  MappedStyle out;
  for (const auto& [k, v] : props.items) {
    std::string key = ascii_lower(k);
    if (key == "background") out.style.set("fill", v);
    else if (key == "color" || key == "colour") out.style.set("fontColor", v);
    else if (key == "stroke") out.style.set("stroke", v);
    else if (key == "strokewidth") out.style.set("strokeWidth", v);
    else if (key == "border") {
      if (dash_or_dot(v)) out.style.set("dashed", "1");
    } else if (key == "opacity") out.style.set("opacity", v);
    else if (key == "fontsize") out.style.set("fontSize", v);
    else if (key == "shape") {
      const std::string* shape = shape_for(v);
      out.shape = shape ? *shape : "rect";
      if (!shape) count(unknown, "shape " + v);
    } else if (key == "width" || key == "height") {
      // Sizes for fixed-layout views, read where the views are built.
    } else if (key == "icon") out.icon = v;
    else count(unknown, k);
  }
  if (out.icon && out.icon->empty()) out.icon.reset();
  if (out.shape && out.shape->empty()) out.shape.reset();
  return out;
}

Style map_relationship_style(const Record& props, Counts& unknown) {
  Style style;
  for (const auto& [k, v] : props.items) {
    std::string key = ascii_lower(k);
    if (key == "color" || key == "colour") {
      style.set("stroke", v);
      style.set("fontColor", v);
    } else if (key == "thickness") style.set("strokeWidth", v);
    else if (key == "style") style.set("dashed", dash_or_dot(v) ? "1" : "0");
    else if (key == "dashed") style.set("dashed", v == "false" ? "0" : "1");
    else if (key == "routing") {
      if (ascii_lower(v).find("orthogonal") != std::string::npos) style.set("edgeStyle", "orthogonalEdgeStyle");
    } else if (key == "fontsize") style.set("fontSize", v);
    else if (key == "opacity") style.set("opacity", v);
    else count(unknown, "relationship " + k);
  }
  return style;
}

bool truthy(const std::optional<std::string>& s) { return s && !s->empty(); }

template <class T>
void push_unique(std::vector<T>& out, const T& v) {
  if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
}

/// `Number(v)` is a non-zero number (`Number(v) || undefined`).
std::optional<double> nonzero_number(const std::string* v) {
  if (!v) return std::nullopt;
  double d = js_to_number(*v);
  return std::isnan(d) || d == 0 ? std::nullopt : std::optional<double>(d);
}

}  // namespace

ImportResult build_model(const Workspace& ws, ModelBuilder& b, const StructurizrOptions& options) {
  const bool keep = options.keep_geometry;
  std::map<std::string, const IrElement*, std::less<>> by_key;
  for (const auto& e : ws.elements) by_key[e->key] = e.get();
  auto find = [&](const std::optional<std::string>& key) -> const IrElement* {
    if (!key) return nullptr;
    auto it = by_key.find(*key);
    return it == by_key.end() ? nullptr : it->second;
  };
  Counts unknown_styles;
  std::vector<std::pair<std::string, double>> stats{
      {"elements", 0}, {"relationships", 0}, {"views", 0}, {"instanceRelationships", 0}};
  auto stat = [&](std::string_view key) -> double& {
    for (auto& [k, v] : stats)
      if (k == key) return v;
    return stats.front().second;
  };

  // Tags, including the implicit type tags styles refer to ("Element", "Person", …).
  auto all_tags = [&](const IrElement& e) {
    std::vector<std::string> tags{"Element", kind_of(e.type).tag};
    tags.insert(tags.end(), e.tags.begin(), e.tags.end());
    return tags;
  };
  auto includes = [](const std::vector<std::string>& list, const std::string& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
  };
  auto style_for = [&](const std::vector<std::string>& tags) {
    Record props;
    for (const auto& s : ws.element_styles)
      if (includes(tags, s->tag))
        for (const auto& [k, v] : s->props.items) props.set(k, v);
    return props;
  };

  // Groups, by path.
  std::map<std::string, GroupId, std::less<>> groups;
  std::function<GroupId(const std::string&)> group_id = [&](const std::string& path) -> GroupId {
    if (auto it = groups.find(path); it != groups.end() && !it->second.empty()) return it->second;
    auto slash = path.rfind('/');
    std::optional<GroupId> parent;
    if (slash != std::string::npos) parent = group_id(path.substr(0, slash));
    GroupId id = b.add_group(slash == std::string::npos ? path : path.substr(slash + 1), parent);
    groups[path] = id;
    return id;
  };

  std::function<int(const IrElement&)> depth = [&](const IrElement& e) -> int {
    const IrElement* p = truthy(e.parent) ? find(e.parent) : nullptr;
    return p ? 1 + depth(*p) : 0;
  };
  std::map<std::string, Box, std::less<>> sizes;
  std::vector<const IrElement*> sorted;
  for (const auto& e : ws.elements) sorted.push_back(e.get());
  std::stable_sort(sorted.begin(), sorted.end(),
                   [&](const IrElement* a, const IrElement* c) { return depth(*a) < depth(*c); });
  for (const IrElement* e : sorted) {
    Kind spec = kind_of(e->type);
    Record props = style_for(all_tags(*e));
    MappedStyle mapped = map_element_style(props, unknown_styles);
    Properties properties;
    for (const auto& [k, v] : e->properties.items) properties.set(k, v);
    if (mapped.icon) properties.set("icon", *mapped.icon);
    if (truthy(e->environment)) properties.set("environment", *e->environment);
    if (truthy(e->instance_of)) properties.set("instanceOf", *e->instance_of);
    if (!props.empty()) {
      Json::Object o;
      for (const auto& [k, v] : props.items) o.emplace_back(k, Json(v));
      properties.set("structurizr.style", Json(std::move(o)).dump());
    }
    std::optional<std::string> shape = mapped.shape;
    if (!shape && spec.shape) shape = spec.shape;
    if (!shape)
      for (const auto& t : e->tags)
        if (ascii_lower(t).find("database") != std::string::npos) {
          shape = "cylinder";
          break;
        }
    ElementInit init;
    init.kind = spec.kind;
    init.name = e->name;
    init.local = e->local;
    if (truthy(e->description)) init.description = e->description;
    if (truthy(e->technology)) init.technology = e->technology;
    if (shape) init.shape = shape;
    std::vector<std::string> tags;
    for (const auto& t : e->tags)
      if (t != "Element" && t != spec.tag) tags.push_back(t);
    init.tags = std::move(tags);
    init.properties = std::move(properties);
    if (!mapped.style.empty()) init.style = mapped.style;
    if (truthy(e->parent) && by_key.count(*e->parent)) init.parent = e->parent;
    if (truthy(e->group)) init.group = group_id(*e->group);
    b.add_element(e->key, std::move(init));
    ++stat("elements");
    Box size = e->type == ElementType::Person ? Box{0, 0, 400, 400} : Box{0, 0, 450, 300};
    if (auto w = nonzero_number(props.get("width"))) size.width = *w;
    if (auto h = nonzero_number(props.get("height"))) size.height = *h;
    sizes[e->key] = size;
  }

  // Relationships, then the ones deployment instances inherit from what they are instances of.
  std::map<std::string, RelationshipId, std::less<>> rel_keys;
  auto rel_style = [&](const std::vector<std::string>& tags) {
    Record props;
    for (const auto& s : ws.relationship_styles)
      if (s->tag == "Relationship" || includes(tags, s->tag))
        for (const auto& [k, v] : s->props.items) props.set(k, v);
    return props;
  };
  auto add_rel = [&](const IrRelationship& r, const std::string& source, const std::string& destination,
                     bool keyed, bool replicated) {
    Record props = rel_style(r.tags);
    Style style = map_relationship_style(props, unknown_styles);
    RelationshipInit init;
    if (truthy(r.description)) init.label = r.description;
    if (truthy(r.technology)) init.technology = r.technology;
    const std::string* dashed = style.find("dashed");
    init.line = dashed && *dashed == "1" ? LineStyle::Dashed : LineStyle::Solid;
    if (!style.empty()) init.style = style;
    Properties properties;
    for (const auto& [k, v] : r.properties.items) properties.set(k, v);
    if (!r.tags.empty()) {
      std::string joined;
      for (std::size_t i = 0; i < r.tags.size(); ++i) joined += (i ? "," : "") + r.tags[i];
      properties.set("tags", joined);
    }
    if (replicated) properties.set("replicated", "true");
    init.properties = std::move(properties);
    auto id = b.add_relationship(source, destination, std::move(init), r.line);
    if (id && keyed && truthy(r.key)) rel_keys[*r.key] = *id;
    return id;
  };
  for (const auto& r : ws.relationships)
    if (add_rel(*r, r->source, r->destination, true, false)) ++stat("relationships");
  std::vector<const IrElement*> instances;
  if (ws.replicate_instances != false)
    for (const auto& e : ws.elements)
      if (truthy(e->instance_of)) instances.push_back(e.get());
  for (const auto& r : ws.relationships)
    for (const IrElement* s : instances) {
      if (*s->instance_of != r->source) continue;
      for (const IrElement* d : instances)
        if (*d->instance_of == r->destination && d->environment == s->environment)
          if (add_rel(*r, s->key, d->key, false, true)) ++stat("instanceRelationships");
    }

  // Views.
  auto relations_of = [&](const std::string& key) {
    std::vector<const IrRelationship*> out;
    for (const auto& r : ws.relationships)
      if (r->source == key || r->destination == key) out.push_back(r.get());
    return out;
  };
  auto top = [&](const std::string& key) -> std::string {
    const IrElement* cur = find(key);
    while (cur && truthy(cur->parent) && find(cur->parent)) cur = find(cur->parent);
    return cur ? cur->key : key;
  };
  auto inside = [&](const std::string& key, const std::string& scope) {
    for (const IrElement* cur = find(key); cur; cur = truthy(cur->parent) ? find(cur->parent) : nullptr)
      if (cur->key == scope) return true;
    return false;
  };
  auto resolve_ref = [&](const std::string& ref) -> std::optional<std::string> {
    if (by_key.count(ref)) return ref;
    for (const auto& e : ws.elements)
      if (e->local == ref || (e->key.size() > ref.size() && e->key.compare(e->key.size() - ref.size(), ref.size(), ref) == 0 &&
                              e->key[e->key.size() - ref.size() - 1] == '.'))
        return e->key;
    return std::nullopt;
  };
  std::map<std::string, std::vector<std::string>, std::less<>> view_elements;
  auto expand = [&](const std::string& expr, const IrView& v,
                    const std::vector<std::string>& base) -> std::vector<std::string> {
    std::string e(js_trim(expr));
    if (e == "*") return base;
    std::smatch m;
    static const std::regex kTag("^element\\.tag\\s*==\\s*(.+)$", std::regex::icase);
    static const std::regex kType("^element\\.type\\s*==\\s*(.+)$", std::regex::icase);
    static const std::regex kArrows("^(->)?\\s*([\\w.-]+)\\s*(->)?$");
    static const std::regex kSplit("[=\\s]");
    std::vector<std::string> out;
    if (std::regex_search(e, m, kTag)) {
      std::vector<std::string> tags;
      for (const auto& part : split(m[1].str(), ',')) tags.emplace_back(js_trim(part));
      for (const auto& x : ws.elements)
        for (const auto& t : all_tags(*x))
          if (includes(tags, t)) {
            out.push_back(x->key);
            break;
          }
      return out;
    }
    if (std::regex_search(e, m, kType)) {
      std::vector<std::string> types;
      for (const auto& part : split(m[1].str(), ',')) {
        std::string t = ascii_lower(js_trim(part));
        std::string squeezed;
        for (std::size_t i = 0; i < t.size();) {
          std::size_t start = i;
          if (!is_js_whitespace(next_code_point(t, i))) squeezed.append(t, start, i - start);
        }
        types.push_back(squeezed);
      }
      for (const auto& x : ws.elements)
        if (includes(types, ascii_lower(type_name(x->type)))) out.push_back(x->key);
      return out;
    }
    if (std::regex_search(e, m, kArrows) && (m[1].matched || m[3].matched)) {
      auto key = resolve_ref(m[2].str());
      if (!key) return {};
      out.push_back(*key);
      for (const auto& r : ws.relationships) {
        if (m[1].matched && r->destination == *key) out.push_back(r->source);
        if (m[3].matched && r->source == *key) out.push_back(r->destination);
      }
      return out;
    }
    if (ascii_lower(e).rfind("relationship", 0) == 0 || e.find("->") != std::string::npos) {
      std::smatch sm;
      std::string first = std::regex_search(e, sm, kSplit) ? e.substr(0, static_cast<std::size_t>(sm.position(0))) : e;
      count(unknown_styles, "view expression " + first);
      return {};
    }
    auto key = resolve_ref(e);
    // Relationship identifiers in include lists add nothing (the relationship shows anyway).
    if (!key)
      for (const auto& r : ws.relationships)
        if (r->key == e) return {};
    if (!key) b.drop("View \"" + v.key + "\": unknown element " + e, v.line, "view include");
    if (key) out.push_back(*key);
    return out;
  };

  for (const auto& vp : ws.views) {
    const IrView& v = *vp;
    ++stat("views");
    std::optional<std::string> scope;
    std::vector<std::string> keys;
    std::optional<std::string> subject = truthy(v.subject) ? resolve_ref(*v.subject) : std::nullopt;
    std::vector<std::string> tops;
    for (const auto& e : ws.elements)
      if (!truthy(e->parent) && !truthy(e->environment) && e->type != ElementType::DeploymentNode)
        tops.push_back(e->key);
    switch (v.type) {
      case ViewType::SystemLandscape: keys = tops; break;
      case ViewType::SystemContext:
        if (subject) {
          keys.push_back(*subject);
          for (const auto* r : relations_of(*subject)) keys.push_back(top(r->source == *subject ? r->destination : r->source));
          for (const auto& k : ws.elements) {
            if (!inside(k->key, *subject) || k->key == *subject) continue;
            for (const auto* r : relations_of(k->key)) {
              const std::string& other = r->source == k->key ? r->destination : r->source;
              if (!inside(other, *subject)) keys.push_back(top(other));
            }
          }
        } else {
          keys = tops;
        }
        break;
      case ViewType::Container:
      case ViewType::Component: {
        scope = subject;
        std::vector<std::string> outside;
        for (const auto& e : ws.elements)
          if (e->parent == subject) keys.push_back(e->key);
        if (subject)
          for (const auto& k : ws.elements) {
            if (!inside(k->key, *subject)) continue;
            for (const auto* r : relations_of(k->key)) {
              const std::string& other = r->source == k->key ? r->destination : r->source;
              if (inside(other, *subject)) continue;
              // Lift to the level next to the subject.
              const IrElement* cur = find(other);
              const IrElement* subject_el = find(subject);
              std::optional<std::string> parent_of_subject = subject_el ? subject_el->parent : std::nullopt;
              while (cur && truthy(cur->parent) && cur->parent != parent_of_subject && find(cur->parent))
                cur = find(cur->parent);
              if (cur) outside.push_back(cur->key);
            }
          }
        keys.insert(keys.end(), outside.begin(), outside.end());
        break;
      }
      case ViewType::Deployment:
        for (const auto& e : ws.elements)
          if (e->environment && (!truthy(v.environment) || e->environment == v.environment)) keys.push_back(e->key);
        break;
      case ViewType::Dynamic:
        if (v.steps)
          for (const auto& s : *v.steps) {
            if (auto k = resolve_ref(s.source); truthy(k)) push_unique(keys, *k);
            if (auto k = resolve_ref(s.destination); truthy(k)) push_unique(keys, *k);
          }
        break;
      case ViewType::Filtered: {
        auto it = view_elements.find(v.base_key ? *v.base_key : std::string());
        if (it != view_elements.end())
          for (const auto& k : it->second) {
            bool has = false;
            for (const auto& t : all_tags(*find(k)))
              if (includes(v.filter_tags, t)) {
                has = true;
                break;
              }
            if (v.filter_exclude ? !has : has) keys.push_back(k);
          }
        break;
      }
      default: break;
    }
    // Explicit include/exclude expressions (with `*` meaning the view's default set).
    if (!v.include.empty() && !(v.include.size() == 1 && v.include[0] == "*")) {
      std::vector<std::string> expanded;
      for (const auto& x : v.include)
        for (const auto& k : expand(x, v, keys)) push_unique(expanded, k);
      keys = std::move(expanded);
    }
    std::vector<std::string> excluded;
    for (const auto& x : v.exclude)
      for (const auto& k : expand(x, v, {})) excluded.push_back(k);
    {
      std::vector<std::string> unique;
      for (const auto& k : keys) push_unique(unique, k);
      keys.clear();
      for (const auto& k : unique)
        if (!includes(excluded, k) && b.has(k)) keys.push_back(k);
    }
    if (v.type == ViewType::Deployment) {
      std::vector<std::string> kept;
      for (const auto& k : keys)
        if (by_key.count(k)) kept.push_back(k);
      keys = std::move(kept);
    }
    view_elements[v.key] = keys;

    std::vector<LayoutHint> hints;
    const bool positioned = keep && !v.positions.empty() && !v.auto_layout;
    if (positioned) {
      for (const auto& k : keys) {
        auto p = v.positions.find(k);
        auto el = b.id(k);
        if (p == v.positions.end() || !el) continue;
        const Box& size = sizes[k];
        hints.emplace_back(PinAtHint{*el, p->second.x, p->second.y, size.width, size.height});
      }
      for (const auto& [rk, pts] : v.vertices) {
        auto rel = rel_keys.find(rk);
        if (rel != rel_keys.end() && !pts.empty()) hints.emplace_back(RouteHint{rel->second, pts, std::nullopt, std::nullopt});
      }
    }
    std::optional<std::vector<SequenceItem>> sequence;
    if (v.type == ViewType::Dynamic) {
      sequence.emplace();
      std::optional<std::pair<double, std::size_t>> parallel;  // group number, index in `sequence`
      if (v.steps)
        for (const auto& s : *v.steps) {
          auto from_key = resolve_ref(s.source);
          auto to_key = resolve_ref(s.destination);
          auto from = from_key ? b.id(*from_key) : std::nullopt;
          auto to = to_key ? b.id(*to_key) : std::nullopt;
          if (!from || !to) {
            b.drop("Dynamic view \"" + v.key + "\": unknown step " + s.source + " -> " + s.destination, v.line,
                   "dynamic step");
            continue;
          }
          std::string label;
          if (truthy(s.description)) label = *s.description;
          if (truthy(s.technology)) label += (label.empty() ? "" : " ") + ("[" + *s.technology + "]");
          SequenceMessage msg{*from, *to, std::nullopt, MessageStyle::Sync};
          if (!label.empty()) msg.label = label;
          if (s.parallel) {
            if (!parallel || parallel->first != *s.parallel) {
              parallel = std::make_pair(*s.parallel, sequence->size());
              sequence->push_back(SequenceFrame{FrameKind::Par, {}});
            }
            std::get<SequenceFrame>((*sequence)[parallel->second]).sections.push_back(FrameSection{std::nullopt, {msg}});
          } else {
            parallel.reset();
            sequence->push_back(msg);
          }
        }
    }
    if (keys.empty()) {
      b.note("View \"" + v.key + "\" shows nothing and was skipped");
      continue;
    }
    ViewInit init;
    init.algorithm = v.type == ViewType::Dynamic ? "sequence" : positioned ? "fixed" : "layered";
    init.direction = v.auto_layout ? v.auto_layout->direction : Direction::Down;
    const bool scoped = truthy(scope) && b.has(*scope);
    if (scoped && v.type != ViewType::Dynamic) init.scope = scope;
    if (scoped) {
      std::vector<std::string> include{"*"};
      for (const auto& k : keys) {
        const IrElement* e = find(k);
        if (!e || e->parent != scope) include.push_back(k);
      }
      init.include = std::move(include);
    } else {
      init.include = keys;
    }
    init.hints = std::move(hints);
    if (sequence) {
      init.sequence = std::move(sequence);
      init.autonumber = true;
    }
    ViewId view_id = b.add_view(v.key, std::move(init));
    if (v.auto_layout && v.auto_layout->node_separation && *v.auto_layout->node_separation != 0 &&
        !std::isnan(*v.auto_layout->node_separation))
      b.set_view_spacing(view_id, js_round(*v.auto_layout->node_separation / 3));
  }
  ImportResult result = b.result();
  result.report.unknown_styles = unknown_styles;
  result.report.stats = stats;
  return result;
}

}  // namespace structurizr

ImportResult import_structurizr(std::string_view source, const StructurizrOptions& options) {
  ModelBuilder b;
  std::string_view head = js_trim(source);
  bool json = !head.empty() && head.front() == '{';
  structurizr::Workspace ws =
      json ? structurizr::parse_json(source, b) : structurizr::parse_dsl(source, b, options);
  return structurizr::build_model(ws, b, options);
}

}  // namespace dd::io
