// Port of packages/import/src/structurizr/json.ts.

#include <algorithm>
#include <cmath>
#include <functional>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "structurizr_ir.hpp"

namespace dd::io::structurizr {

namespace {

const Json::Array kEmpty;

/// `obj[key]` for any value (undefined, as nullptr, unless it is an object with that key).
const Json* field(const Json* j, std::string_view key) { return j ? j->get(key) : nullptr; }

/// `Array.isArray(v) ? v : []`.
const Json::Array& arr(const Json* v) { return v && v->is_array() ? v->as_array() : kEmpty; }

/// `typeof v === 'string' && v !== '' ? v : undefined`.
std::optional<std::string> str(const Json* v) {
  return v && v->is_string() && !v->as_string().empty() ? std::optional<std::string>(v->as_string()) : std::nullopt;
}

/// JavaScript `String(v)`; `undefined` for a missing value.
std::string js_string(const Json* v) {
  if (!v) return "undefined";
  if (v->is_null()) return "null";
  if (v->is_bool()) return v->as_bool() ? "true" : "false";
  if (v->is_number()) return js_number_string(v->as_number());
  if (v->is_string()) return v->as_string();
  if (v->is_array()) {
    std::string out;
    bool first = true;
    for (const auto& x : v->as_array()) {
      if (!first) out += ",";
      first = false;
      if (!x.is_null()) out += js_string(&x);
    }
    return out;
  }
  return "[object Object]";
}

/// `String(v ?? fallback)`.
std::string js_string_or(const Json* v, const std::string& fallback) {
  return !v || v->is_null() ? fallback : js_string(v);
}

/// JavaScript `Number(v)`.
double js_number(const Json* v) {
  if (!v) return std::nan("");
  if (v->is_null()) return 0;
  if (v->is_bool()) return v->as_bool() ? 1 : 0;
  if (v->is_number()) return v->as_number();
  if (v->is_string()) return js_to_number(v->as_string());
  if (v->is_array()) return js_to_number(js_string(v));
  return std::nan("");
}

/// JavaScript truthiness of a value.
bool js_truthy(const Json* v) {
  if (!v || v->is_null()) return false;
  if (v->is_bool()) return v->as_bool();
  if (v->is_number()) return v->as_number() != 0 && !std::isnan(v->as_number());
  if (v->is_string()) return !v->as_string().empty();
  return true;
}

/// An array index key (`"0"`, `"17"`): such keys come first in a JavaScript object, ascending.
std::optional<std::uint32_t> array_index(std::string_view k) {
  if (k.empty() || k.size() > 10 || (k.size() > 1 && k[0] == '0')) return std::nullopt;
  std::uint64_t n = 0;
  for (char c : k) {
    if (c < '0' || c > '9') return std::nullopt;
    n = n * 10 + static_cast<std::uint64_t>(c - '0');
  }
  if (n >= 4294967295ULL) return std::nullopt;
  return static_cast<std::uint32_t>(n);
}

/// Keys and values in JavaScript property order.
template <class V>
std::vector<std::pair<std::string, V>> js_order(std::vector<std::pair<std::string, V>> items) {
  std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
    auto ia = array_index(a.first), ib = array_index(b.first);
    if (ia && ib) return *ia < *ib;
    return ia.has_value() && !ib.has_value();
  });
  return items;
}

/// An object's members as strings (`String(v)`), in JavaScript property order.
Record record_of(const Json* v) {
  Record out;
  if (!v || !v->is_object()) return out;
  std::vector<std::pair<std::string, std::string>> items;
  for (const auto& [k, x] : v->as_object()) items.emplace_back(k, js_string(&x));
  for (auto& [k, x] : js_order(std::move(items))) out.set(k, std::move(x));
  return out;
}

/// `tags(v)`: a comma-separated string as a list.
std::vector<std::string> tags_of(const Json* v) {
  auto s = str(v);
  return s ? split_list(*s) : std::vector<std::string>{};
}

/// ICU-like `a.localeCompare(b, undefined, { numeric: true })` for the step orders of dynamic
/// views (`1`, `2`, `1.1`, `10`): digit runs compare as numbers, other characters by class
/// (punctuation, digits, letters), letters case-insensitively first.
int locale_compare_numeric(std::string_view a, std::string_view b) {
  auto cls = [](unsigned char c) {
    if (c >= '0' && c <= '9') return 1;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) return 2;
    return 0;
  };
  auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c - 'A' + 'a') : c; };
  int tertiary = 0;
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
    if (cls(ca) == 1 && cls(cb) == 1) {
      std::size_t si = i, sj = j;
      while (i < a.size() && cls(static_cast<unsigned char>(a[i])) == 1) ++i;
      while (j < b.size() && cls(static_cast<unsigned char>(b[j])) == 1) ++j;
      std::string_view da = a.substr(si, i - si), db = b.substr(sj, j - sj);
      while (da.size() > 1 && da[0] == '0') da.remove_prefix(1);
      while (db.size() > 1 && db[0] == '0') db.remove_prefix(1);
      if (da.size() != db.size()) return da.size() < db.size() ? -1 : 1;
      if (int c = da.compare(db)) return c < 0 ? -1 : 1;
      continue;
    }
    if (cls(ca) != cls(cb)) return cls(ca) < cls(cb) ? -1 : 1;
    if (lower(ca) != lower(cb)) return lower(ca) < lower(cb) ? -1 : 1;
    if (!tertiary && ca != cb) tertiary = ca > cb ? -1 : 1;  // lowercase first
    ++i;
    ++j;
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return tertiary;
}

Direction rank_direction(const std::optional<std::string>& v) {
  std::string r = v ? *v : "TopBottom";
  if (r == "TopBottom") return Direction::Down;
  if (r == "BottomTop") return Direction::Up;
  if (r == "LeftRight") return Direction::Right;
  if (r == "RightLeft") return Direction::Left;
  return Direction::Down;
}

}  // namespace

Workspace parse_json(std::string_view text, ModelBuilder& b) {
  Workspace ws;
  // The JSON already lists implied and instance relationships.
  ws.implied_relationships = false;
  ws.replicate_instances = false;
  auto parsed = Json::parse(text);
  if (!parsed) {
    b.drop("Not valid JSON: " + parsed.error().message, std::nullopt, "json");
    return ws;
  }
  const Json* doc = &*parsed;
  ws.name = str(field(doc, "name")).value_or("");
  const Json* model = field(doc, "model");

  struct Extra {
    std::optional<std::string> parent;
    std::optional<std::string> environment;
    std::optional<std::string> instance_of;
    std::optional<Record> properties;
    std::optional<std::string> name;  // `{ ...i, name }`
  };
  auto add = [&](const Json& e, ElementType type, const Extra& extra) {
    std::string id = js_string_or(field(&e, "id"), "");
    Record props = record_of(field(&e, "properties"));
    std::optional<std::string> identifier;
    if (const std::string* v = props.get("structurizr.dsl.identifier")) {
      identifier = *v;
      std::erase_if(props.items, [](const auto& kv) { return kv.first == "structurizr.dsl.identifier"; });
    }
    if (auto url = str(field(&e, "url"))) props.set("url", *url);
    std::optional<std::string> name = extra.name ? (extra.name->empty() ? std::nullopt : extra.name) : str(field(&e, "name"));
    auto el = std::make_shared<IrElement>();
    el->key = id;
    if (identifier) el->local = identifier->substr(identifier->rfind('.') == std::string::npos ? 0 : identifier->rfind('.') + 1);
    else el->local = name ? *name : "e" + id;
    if (identifier && !identifier->empty()) el->identifier = identifier;
    el->type = type;
    el->name = name.value_or("");
    el->description = str(field(&e, "description"));
    el->technology = str(field(&e, "technology"));
    el->tags = tags_of(field(&e, "tags"));
    el->properties = extra.properties ? *extra.properties : std::move(props);
    el->group = str(field(&e, "group"));
    el->parent = extra.parent;
    el->environment = extra.environment;
    el->instance_of = extra.instance_of;
    ws.elements.push_back(el);
    for (const auto& r : arr(field(&e, "relationships"))) {
      auto rel = std::make_shared<IrRelationship>();
      rel->key = js_string_or(field(&r, "id"), "");
      rel->source = js_string_or(field(&r, "sourceId"), id);
      rel->destination = js_string_or(field(&r, "destinationId"), "");
      rel->description = str(field(&r, "description"));
      rel->technology = str(field(&r, "technology"));
      rel->tags = tags_of(field(&r, "tags"));
      rel->properties = record_of(field(&r, "properties"));
      if (auto linked = str(field(&r, "linkedRelationshipId"))) rel->properties.set("linkedRelationshipId", *linked);
      ws.relationships.push_back(rel);
    }
  };

  for (const auto& p : arr(field(model, "people"))) add(p, ElementType::Person, {});
  for (const auto& c : arr(field(model, "customElements"))) {
    Record metadata;
    metadata.set("metadata", str(field(&c, "metadata")).value_or(""));
    add(c, ElementType::Element, {std::nullopt, std::nullopt, std::nullopt, metadata, std::nullopt});
  }
  for (const auto& s : arr(field(model, "softwareSystems"))) {
    add(s, ElementType::SoftwareSystem, {});
    for (const auto& c : arr(field(&s, "containers"))) {
      add(c, ElementType::Container, {js_string(field(&s, "id")), std::nullopt, std::nullopt, std::nullopt, std::nullopt});
      for (const auto& k : arr(field(&c, "components")))
        add(k, ElementType::Component, {js_string(field(&c, "id")), std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    }
  }
  std::function<void(const Json&, const std::optional<std::string>&)> node = [&](const Json& n,
                                                                                  const std::optional<std::string>& parent) {
    std::string environment = str(field(&n, "environment")).value_or("Default");
    add(n, ElementType::DeploymentNode,
        {parent && !parent->empty() ? parent : std::nullopt, environment, std::nullopt, std::nullopt, std::nullopt});
    std::string nid = js_string(field(&n, "id"));
    for (const auto& c : arr(field(&n, "children"))) node(c, nid);
    for (const auto& i : arr(field(&n, "infrastructureNodes")))
      add(i, ElementType::InfrastructureNode, {nid, environment, std::nullopt, std::nullopt, std::nullopt});
    auto instance = [&](const Json& i, const char* ref, ElementType type, const char* fallback) {
      std::string of_key = js_string(field(&i, ref));
      const IrElement* of = nullptr;
      for (const auto& e : ws.elements)
        if (e->key == of_key) {
          of = e.get();
          break;
        }
      add(i, type, {nid, environment, of_key, std::nullopt, of ? of->name : std::string(fallback)});
    };
    for (const auto& i : arr(field(&n, "containerInstances")))
      instance(i, "containerId", ElementType::ContainerInstance, "Container instance");
    for (const auto& i : arr(field(&n, "softwareSystemInstances")))
      instance(i, "softwareSystemId", ElementType::SoftwareSystemInstance, "Software system instance");
  };
  for (const auto& n : arr(field(model, "deploymentNodes"))) node(n, std::nullopt);

  const Json* views = field(doc, "views");
  struct List {
    const char* list;
    ViewType type;
    const char* name;
    const char* subject;
  };
  static const List kLists[] = {{"systemLandscapeViews", ViewType::SystemLandscape, "systemLandscape", nullptr},
                                {"systemContextViews", ViewType::SystemContext, "systemContext", "softwareSystemId"},
                                {"containerViews", ViewType::Container, "container", "softwareSystemId"},
                                {"componentViews", ViewType::Component, "component", "containerId"},
                                {"dynamicViews", ViewType::Dynamic, "dynamic", "elementId"},
                                {"deploymentViews", ViewType::Deployment, "deployment", "softwareSystemId"},
                                {"customViews", ViewType::Custom, "custom", nullptr}};
  for (const auto& list : kLists) {
    for (const auto& v : arr(field(views, list.list))) {
      auto view = std::make_shared<IrView>();
      view->type = list.type;
      for (const auto& e : arr(field(&v, "elements"))) {
        const Json* x = field(&e, "x");
        const Json* y = field(&e, "y");
        if (x && y && x->is_number() && y->is_number() && (x->as_number() != 0 || y->as_number() != 0))
          view->positions[js_string(field(&e, "id"))] = Point2{x->as_number(), y->as_number()};
      }
      std::vector<std::pair<std::string, std::vector<Point2>>> vertices;
      for (const auto& r : arr(field(&v, "relationships"))) {
        std::vector<Point2> vs;
        for (const auto& p : arr(field(&r, "vertices"))) vs.push_back({js_number(field(&p, "x")), js_number(field(&p, "y"))});
        if (vs.empty()) continue;
        std::string key = js_string(field(&r, "id"));
        bool found = false;
        for (auto& [k, pts] : vertices)
          if (k == key) {
            pts = vs;
            found = true;
          }
        if (!found) vertices.emplace_back(key, std::move(vs));
      }
      view->vertices = js_order(std::move(vertices));
      const Json* automatic = field(&v, "automaticLayout");
      view->key = str(field(&v, "key")).value_or(std::string(list.name) + "-" + std::to_string(ws.views.size() + 1));
      if (list.subject) view->subject = str(field(&v, list.subject));
      if (list.type == ViewType::Deployment) view->environment = str(field(&v, "environment")).value_or("Default");
      // JSON views list exactly what they show.
      for (const auto& e : arr(field(&v, "elements"))) view->include.push_back(js_string(field(&e, "id")));
      if (js_truthy(automatic)) {
        AutoLayout a;
        a.direction = rank_direction(str(field(automatic, "rankDirection")));
        if (const Json* r = field(automatic, "rankSeparation"); r && r->is_number()) a.rank_separation = r->as_number();
        if (const Json* n = field(automatic, "nodeSeparation"); n && n->is_number()) a.node_separation = n->as_number();
        view->auto_layout = a;
      }
      if (list.type == ViewType::Dynamic) {
        std::vector<const Json*> rels;
        for (const auto& r : arr(field(&v, "relationships"))) rels.push_back(&r);
        auto order = [](const Json* r) { return js_string_or(field(r, "order"), ""); };
        std::stable_sort(rels.begin(), rels.end(),
                         [&](const Json* a, const Json* c) { return locale_compare_numeric(order(a), order(c)) < 0; });
        std::vector<IrStep> steps;
        for (const Json* r : rels) {
          std::string rid = js_string(field(r, "id"));
          const IrRelationship* rel = nullptr;
          for (const auto& x : ws.relationships)
            if (x->key == rid) {
              rel = x.get();
              break;
            }
          IrStep step;
          step.source = rel ? rel->source : "";
          step.destination = rel ? rel->destination : "";
          auto description = str(field(r, "description"));
          if (description) step.description = description;
          else if (rel && rel->description && !rel->description->empty()) step.description = rel->description;
          if (rel && rel->technology && !rel->technology->empty()) step.technology = rel->technology;
          std::string o = order(r);
          if (o.find('.') != std::string::npos) step.parallel = js_to_number(o.substr(0, o.find('.')));
          steps.push_back(std::move(step));
        }
        view->steps = std::move(steps);
      }
      ws.views.push_back(view);
    }
  }
  for (const auto& v : arr(field(views, "filteredViews"))) {
    auto view = std::make_shared<IrView>();
    view->type = ViewType::Filtered;
    view->key = str(field(&v, "key")).value_or("Filtered-" + std::to_string(ws.views.size() + 1));
    view->base_key = str(field(&v, "baseViewKey")).value_or("");
    view->filter_exclude = str(field(&v, "mode")) == std::optional<std::string>("Exclude");
    for (const auto& t : arr(field(&v, "tags"))) view->filter_tags.push_back(js_string(&t));
    ws.views.push_back(view);
  }
  if (std::size_t n = arr(field(views, "imageViews")).size())
    b.drop(std::to_string(n) + " image views are not imported", std::nullopt, "image view");

  const Json* configuration = field(views, "configuration");
  const Json* styles = field(configuration, "styles");
  auto read_styles = [&](const Json* list, std::vector<std::shared_ptr<TagStyle>>& into) {
    for (const auto& s : arr(list)) {
      auto style = std::make_shared<TagStyle>();
      style->tag = js_string(field(&s, "tag"));
      Record props = record_of(&s);
      std::erase_if(props.items, [](const auto& kv) { return kv.first == "tag"; });
      style->props = std::move(props);
      into.push_back(style);
    }
  };
  read_styles(field(styles, "elements"), ws.element_styles);
  read_styles(field(styles, "relationships"), ws.relationship_styles);
  if (std::size_t n = arr(field(configuration, "themes")).size())
    b.drop("Themes are not fetched (" + std::to_string(n) + ")", std::nullopt, "theme");
  return ws;
}

}  // namespace dd::io::structurizr
