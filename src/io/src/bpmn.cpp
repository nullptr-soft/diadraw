// Port of packages/import/src/bpmn/import.ts.

#include <cmath>
#include <map>
#include <regex>
#include <set>

#include "bpmn_model.hpp"
#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/library/builtin.hpp"

namespace dd::io {

using namespace bpmn;

namespace {

struct Bounds {
  double x = 0, y = 0, width = 0, height = 0;
};

struct DiShape {
  Bounds bounds;
  std::optional<bool> expanded;
  std::optional<bool> horizontal;
  std::optional<bool> marker_visible;
};

/// A string-keyed map in insertion order where setting a key again keeps its place (a JS Map).
template <class V>
struct LinkedMap {
  std::vector<std::pair<std::string, V>> items;
  std::map<std::string, std::size_t, std::less<>> index;

  void set(const std::string& key, V value) {
    auto it = index.find(key);
    if (it != index.end()) {
      items[it->second].second = std::move(value);
      return;
    }
    index.emplace(key, items.size());
    items.emplace_back(key, std::move(value));
  }
  const V* get(std::string_view key) const {
    auto it = index.find(key);
    return it == index.end() ? nullptr : &items[it->second].second;
  }
  [[nodiscard]] bool has(std::string_view key) const { return index.find(key) != index.end(); }
};

struct Plane {
  std::string name;
  LinkedMap<DiShape> shapes;
  LinkedMap<std::vector<Point2>> edges;
};

using Nodes = std::vector<const XmlNode*>;

/// Child elements, ignoring namespace prefixes.
Nodes kids(const XmlNode& n, std::string_view name) {
  Nodes out;
  for (const auto& c : n.children)
    if (local(c->name) == name) out.push_back(c.get());
  return out;
}
const XmlNode* kid(const XmlNode& n, std::string_view name) {
  for (const auto& c : n.children)
    if (local(c->name) == name) return c.get();
  return nullptr;
}
std::string text_of(const XmlNode* n) {
  if (!n) return "";
  std::string out = n->text;
  for (const auto& c : n->children) out += text_of(c.get());
  return out;
}
std::string trimmed(std::string_view s) { return std::string(js_trim(s)); }

/// `n.attrs[key]`, as an optional.
std::optional<std::string> attr(const XmlNode& n, std::string_view key) {
  const std::string* v = n.attr(key);
  return v ? std::optional<std::string>(*v) : std::nullopt;
}
/// `n.attrs[key] ?? ''`.
std::string attr_or(const XmlNode& n, std::string_view key) {
  const std::string* v = n.attr(key);
  return v ? *v : std::string();
}
/// `n.attrs[key] === value`.
bool attr_is(const XmlNode& n, std::string_view key, std::string_view value) {
  const std::string* v = n.attr(key);
  return v && *v == value;
}
/// `Number(v) || 0`.
double number_or_zero(const std::string* v) {
  if (!v) return 0;
  double d = js_to_number(*v);
  return std::isnan(d) || d == 0 ? 0 : d;
}

bool is_standard_attr(std::string_view k) {
  static const std::set<std::string, std::less<>> kStandard{
      "id",          "name",           "sourceRef",          "targetRef",
      "attachedToRef", "cancelActivity", "isInterrupting",    "parallelMultiple",
      "isForCompensation", "startQuantity", "completionQuantity", "default",
      "gatewayDirection", "instantiate",  "eventGatewayType",   "triggeredByEvent",
      "processRef",  "isExecutable",   "isClosed",           "processType",
      "calledElement", "implementation", "scriptFormat",       "messageRef",
      "dataObjectRef", "dataStoreRef",  "itemSubjectRef",     "isCollection",
      "categoryValueRef", "associationDirection", "textFormat", "method",
      "ordering",    "cancelRemainingInstances", "isImmediate", "operationRef",
      "initiatingParticipantRef", "loopType"};
  return kStandard.count(k) > 0;
}

bool is_non_visual(std::string_view t) {
  static const std::set<std::string, std::less<>> kNonVisual{
      "extensionElements", "documentation", "incoming", "outgoing", "ioSpecification", "property", "dataObject",
      "laneSet", "auditing", "monitoring", "supportedInterfaceRef", "resourceRole", "performer", "humanPerformer",
      "potentialOwner", "standardLoopCharacteristics", "multiInstanceLoopCharacteristics", "correlationSubscription",
      "eventDefinitionRef", "dataInputAssociation", "dataOutputAssociation", "inputSet", "outputSet", "script",
      "rendering", "ioBinding", "completionCondition", "participantRef", "messageFlowRef", "correlationKey",
      "category", "categoryValue", "participant", "categoryValueRef", "participantAssociation",
      "messageFlowAssociation", "conversationAssociation"};
  return kNonVisual.count(t) > 0;
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

double round2(double v) { return js_round(v * 100) / 100; }

}  // namespace

ImportResult import_bpmn(std::string_view xml, const BpmnOptions& options) {
  ModelBuilder b;
  const bool keep = options.keep_geometry;
  std::vector<std::pair<std::string, double>> stats{{"elements", 0}, {"relationships", 0}, {"views", 0}, {"planes", 0}};
  auto stat = [&](const std::string& key) -> double& {
    for (auto& [k, v] : stats)
      if (k == key) return v;
    return stats.emplace_back(key, 0).second;
  };
  std::vector<std::pair<std::string, int>> unknown_shapes;
  const auto& shapes = library::builtin_shape_ids("bpmn");
  auto has_shape = [&](std::string_view s) { return shapes.count(s) > 0; };

  auto doc = parse_xml(xml);
  const XmlNode* definitions = nullptr;
  for (const auto& c : doc->children)
    if (local(c->name) == "definitions") {
      definitions = c.get();
      break;
    }
  if (!definitions) {
    b.drop("Not a BPMN 2.0 file: no <definitions>", std::nullopt, "definitions");
    ImportResult r = b.result();
    r.report.stats = stats;
    return r;
  }
  b.use_library("bpmn", "^1", "bpmn");

  // Every element by id (the last one wins).
  std::map<std::string, const XmlNode*, std::less<>> by_id;
  auto index = [&](auto& self, const XmlNode& n) -> void {
    if (const std::string* id = n.attr("id"); id && !id->empty()) by_id[*id] = &n;
    for (const auto& c : n.children) self(self, *c);
  };
  index(index, *definitions);
  auto lookup = [&](std::string_view id) -> const XmlNode* {
    auto it = by_id.find(id);
    return it == by_id.end() ? nullptr : it->second;
  };
  std::map<std::string, std::string, std::less<>> namespaces;
  for (const auto& [k, v] : definitions->attrs)
    if (k.rfind("xmlns:", 0) == 0) namespaces[k.substr(6)] = v;

  // ---------------------------------------------------------------- diagram interchange
  std::vector<Plane> planes;
  LinkedMap<DiShape> di_shapes;
  for (const XmlNode* diagram : kids(*definitions, "BPMNDiagram")) {
    for (const XmlNode* plane : kids(*diagram, "BPMNPlane")) {
      Plane p;
      std::string dn = attr_or(*diagram, "name"), pe = attr_or(*plane, "bpmnElement"), di = attr_or(*diagram, "id");
      p.name = !dn.empty() ? dn : !pe.empty() ? pe : !di.empty() ? di : "diagram";
      for (const XmlNode* s : kids(*plane, "BPMNShape")) {
        const XmlNode* bounds = kid(*s, "Bounds");
        std::string ref = attr_or(*s, "bpmnElement");
        if (!bounds || ref.empty()) continue;
        DiShape shape;
        shape.bounds = {number_or_zero(bounds->attr("x")), number_or_zero(bounds->attr("y")),
                        number_or_zero(bounds->attr("width")), number_or_zero(bounds->attr("height"))};
        if (auto v = attr(*s, "isExpanded")) shape.expanded = *v == "true";
        if (auto v = attr(*s, "isHorizontal")) shape.horizontal = *v != "false";
        if (auto v = attr(*s, "isMarkerVisible")) shape.marker_visible = *v == "true";
        p.shapes.set(ref, shape);
        if (!di_shapes.has(ref)) di_shapes.set(ref, shape);
      }
      for (const XmlNode* e : kids(*plane, "BPMNEdge")) {
        std::string ref = attr_or(*e, "bpmnElement");
        if (ref.empty()) continue;
        std::vector<Point2> points;
        for (const XmlNode* w : kids(*e, "waypoint"))
          points.push_back({number_or_zero(w->attr("x")), number_or_zero(w->attr("y"))});
        p.edges.set(ref, std::move(points));
      }
      planes.push_back(std::move(p));
    }
  }
  stat("planes") = static_cast<double>(planes.size());
  auto di_of = [&](const XmlNode& n) { return di_shapes.get(attr_or(n, "id")); };

  // ---------------------------------------------------------------- elements
  auto key_of = [&](std::string_view id) { return b.has(id); };

  auto properties = [&](const XmlNode& n, std::string_view type) {
    Properties props;
    props.set("bpmn.type", std::string(type));
    if (const std::string* id = n.attr("id"); id && !id->empty()) props.set("bpmn.id", *id);
    for (const auto& [k, v] : n.attrs) {
      if (k == "id" || k == "name" || k.rfind("xmlns", 0) == 0) continue;
      if (is_standard_attr(k)) props.set("bpmn." + k, v);
      else props.set("bpmn.attr." + k, v);
    }
    const XmlNode* ext = kid(n, "extensionElements");
    if (ext && !ext->children.empty()) {
      std::string joined;
      for (const auto& c : ext->children) joined += serialize_xml(*c);
      props.set("bpmn.extensions", joined);
    }
    // Namespaces of vendor attributes and extensions, so the exporter can declare them again.
    std::vector<std::string> prefixes;
    auto add_prefix = [&](std::string p) {
      for (const auto& q : prefixes)
        if (q == p) return;
      prefixes.push_back(std::move(p));
    };
    for (const auto& [k, _] : props)
      if (k.rfind("bpmn.attr.", 0) == 0 && k.find(':') != std::string::npos) add_prefix(k.substr(10, k.find(':') - 10));
    if (const std::string* ext_text = props.find("bpmn.extensions")) {
      static const std::regex kPrefix("<\\/?([\\w.-]+):");
      for (auto it = std::sregex_iterator(ext_text->begin(), ext_text->end(), kPrefix); it != std::sregex_iterator();
           ++it)
        add_prefix((*it)[1].str());
    }
    for (const auto& prefix : prefixes) {
      auto it = namespaces.find(prefix);
      if (it != namespaces.end() && !it->second.empty()) props.set("bpmn.xmlns." + prefix, it->second);
    }
    return props;
  };
  auto documentation = [&](const XmlNode& n) {
    std::string out;
    bool first = true;
    for (const XmlNode* d : kids(n, "documentation")) {
      if (!first) out += "\n";
      first = false;
      out += text_of(d);
    }
    return trimmed(out);
  };

  auto event_shape = [&](const XmlNode& n, std::string_view type) -> std::string {
    std::vector<std::string> defs;
    for (const auto& c : n.children)
      if (event_marker(local(c->name))) defs.emplace_back(local(c->name));
    for (const XmlNode* r : kids(n, "eventDefinitionRef")) {
      const XmlNode* target = lookup(trimmed(text_of(r)));
      std::string t(local(target ? target->name : std::string()));
      if (event_marker(t)) defs.push_back(t);
    }
    std::string position =
        type == "startEvent"   ? (attr_is(n, "isInterrupting", "false") ? "start_non_interrupting" : "start")
        : type == "endEvent"   ? "end"
        : type == "boundaryEvent"
            ? (attr_is(n, "cancelActivity", "false") ? "intermediate_catch_non_interrupting" : "intermediate_catch")
        : type == "intermediateThrowEvent" || type == "implicitThrowEvent" ? "intermediate_throw"
                                                                           : "intermediate_catch";
    std::string marker = defs.empty()      ? ""
                         : defs.size() > 1 ? (attr_is(n, "parallelMultiple", "true") ? "parallel_multiple" : "multiple")
                                           : std::string(*event_marker(defs[0]));
    std::vector<std::string> candidates{position + "_event" + (marker.empty() ? "" : "_" + marker)};
    // An event sub-process start may be interrupting with an error, escalation… marker.
    if (position == "start" && !marker.empty()) {
      candidates.push_back("start_event_subprocess_event_" + marker);
      candidates.push_back("start_non_interrupting_event_" + marker);
    }
    candidates.push_back(position + "_event");
    candidates.emplace_back(position.rfind("intermediate_catch", 0) == 0 ? "intermediate_catch_event" : "start_event");
    for (const auto& c : candidates)
      if (has_shape(c)) return c;
    return "start_event";
  };

  auto activity_shape = [&](const XmlNode& n, std::string_view type, bool expanded) -> std::string {
    std::string loop;
    if (kid(n, "standardLoopCharacteristics")) loop = "loop";
    else if (const XmlNode* mi = kid(n, "multiInstanceLoopCharacteristics"))
      loop = attr_is(*mi, "isSequential", "true") ? "mi_sequential" : "mi_parallel";
    else if (attr_is(n, "isForCompensation", "true")) loop = "compensation";
    std::string base;
    if (type == "subProcess") base = attr_is(n, "triggeredByEvent", "true") ? "event_subprocess" : "subprocess";
    else if (type == "transaction") base = "transaction";
    else if (type == "adHocSubProcess") base = "ad_hoc_subprocess";
    else if (type == "callActivity") base = expanded ? "call_subprocess" : "call_activity";
    else base = camel_to_snake(type);
    std::string id = loop.empty() ? base : base + "_" + loop;
    return has_shape(id) ? id : has_shape(base) ? base : "task";
  };

  auto gateway_shape = [&](const XmlNode& n, std::string_view type) -> std::string {
    if (type == "exclusiveGateway") {
      const DiShape* di = di_of(n);
      // DI says whether the X is shown; without DI there is no reason to hide it.
      return di && di->marker_visible == false ? "exclusive_gateway_plain" : "exclusive_gateway";
    }
    if (type == "eventBasedGateway") {
      if (attr_is(n, "instantiate", "true"))
        return attr_is(n, "eventGatewayType", "Parallel") ? "parallel_event_based_gateway" : "event_based_gateway_start";
      return "event_based_gateway";
    }
    return camel_to_snake(type);
  };

  auto data_shape = [&](const XmlNode& n, std::string_view type) -> std::string {
    if (type == "dataStoreReference" || type == "dataStore") return "data_store";
    if (type == "message") return "message";
    const XmlNode* target = type == "dataObjectReference" ? lookup(attr_or(n, "dataObjectRef")) : &n;
    std::string collection = target && attr_is(*target, "isCollection", "true") ? "_collection" : "";
    std::string base = type == "dataInput" ? "data_input" : type == "dataOutput" ? "data_output" : "data_object";
    return base + collection;
  };

  /// Whether an element is drawn expanded (as a container of its children).
  auto is_expanded = [&](const XmlNode& n) {
    const DiShape* di = di_of(n);
    if (di && di->expanded) return *di->expanded;
    // Without DI: expanded when it has something to show.
    for (const auto& c : n.children) {
      std::string_view t = local(c->name);
      if (is_event(t) || is_task(t) || is_subprocess(t) || is_gateway(t) || is_choreography(t)) return true;
    }
    return false;
  };

  auto name_of = [&](const XmlNode& n, std::string_view type) -> std::string {
    if (type == "textAnnotation") return trimmed(text_of(kid(n, "text")));
    if (type == "group") {
      const XmlNode* value = lookup(attr_or(n, "categoryValueRef"));
      if (value && value->attr("value")) return trimmed(*value->attr("value"));
      return trimmed(attr_or(n, "name"));
    }
    if (type == "participant" && attr_or(n, "name").empty()) {
      const XmlNode* process = lookup(attr_or(n, "processRef"));
      return trimmed(process ? attr_or(*process, "name") : std::string());
    }
    return trimmed(collapse_whitespace(attr_or(n, "name")));
  };

  auto add_node = [&](const XmlNode& n, const std::string& type,
                      const std::optional<std::string>& parent) -> std::optional<std::string> {
    std::string id = attr_or(n, "id");
    if (id.empty()) {
      b.drop("<" + type + "> without an id", std::nullopt, "element " + type);
      return std::nullopt;
    }
    std::optional<std::string> shape;
    std::optional<Style> style;
    if (is_event(type)) shape = event_shape(n, type);
    else if (is_task(type) || is_subprocess(type)) {
      bool expanded = is_subprocess(type) || type == "callActivity" ? is_expanded(n) : false;
      if (expanded && is_subprocess(type)) {
        shape = "rounded";
        Style s;
        s.set("rounded", "1");
        s.set("fillColor", "#ffffff");
        if (attr_is(n, "triggeredByEvent", "true")) {
          s.set("dashed", "1");
          s.set("dashPattern", "3 3");
        }
        if (type == "transaction") s.set("strokeWidth", "2");
        style = std::move(s);
      } else {
        shape = activity_shape(n, type, expanded);
      }
    } else if (is_gateway(type)) shape = gateway_shape(n, type);
    else if (is_data(type)) shape = data_shape(n, type);
    else if (type == "textAnnotation") shape = "text_annotation";
    else if (type == "group") shape = "group";
    else if (is_conversation(type)) shape = camel_to_snake(type);
    else if (type == "choreographyTask" || type == "callChoreography") shape = "choreography_task";
    else if (type == "subChoreography") shape = is_expanded(n) ? "rounded" : "sub_choreography";
    else if (type == "participant" || type == "lane") {
      shape = "swimlane";
      const DiShape* di = di_shapes.get(id);
      Style s;
      s.set("fillColor", "#ffffff");
      if (!(di && di->horizontal == false)) s.set("horizontal", "0");
      style = std::move(s);
    }
    if (shape && !shape->empty() && *shape != "rounded" && *shape != "swimlane") {
      if (!has_shape(*shape)) {
        bool found = false;
        for (auto& [k, count] : unknown_shapes)
          if (k == *shape) {
            ++count;
            found = true;
          }
        if (!found) unknown_shapes.emplace_back(*shape, 1);
        shape.reset();
      } else {
        shape = "bpmn." + *shape;
      }
    }
    std::string description = documentation(n);
    ElementInit init;
    init.kind = kind_of(type);
    init.name = name_of(n, type);
    init.parent = parent;
    if (shape && !shape->empty()) init.shape = shape;
    init.style = std::move(style);
    if (!description.empty()) init.description = description;
    init.properties = properties(n, type);
    b.add_element(id, std::move(init));
    ++stat("elements");
    ++stat("type:" + type);
    return id;
  };

  // Event definitions, loop characteristics and the like are kept as properties of their node.
  auto describe_node = [&](const XmlNode& n, const std::string& id) {
    Nodes defs;
    for (const auto& c : n.children)
      if (event_marker(local(c->name))) defs.push_back(c.get());
    if (!defs.empty()) {
      std::string list;
      for (std::size_t i = 0; i < defs.size(); ++i) {
        if (i) list += ",";
        list += local(defs[i]->name);
        std::string did = attr_or(*defs[i], "id");
        if (!did.empty()) list += "#" + did;
      }
      b.set_property(id, "bpmn.eventDefinitions", list);
    }
    for (const XmlNode* d : defs) {
      std::string timer;
      for (std::size_t i = 0; i < d->children.size(); ++i) {
        if (i) timer += ";";
        timer += std::string(local(d->children[i]->name)) + "=" + trimmed(text_of(d->children[i].get()));
      }
      if (!timer.empty() && local(d->name) == "timerEventDefinition") b.set_property(id, "bpmn.timer", timer);
      for (const char* ref : {"messageRef", "signalRef", "errorRef", "escalationRef", "activityRef"}) {
        std::string v = attr_or(*d, ref);
        if (!v.empty()) b.set_property(id, std::string("bpmn.") + ref, v);
      }
      if (const XmlNode* condition = kid(*d, "condition"))
        b.set_property(id, "bpmn.condition", trimmed(text_of(condition)));
      if (local(d->name) == "linkEventDefinition") {
        std::string name = attr_or(*d, "name");
        if (!name.empty()) b.set_property(id, "bpmn.link", name);
      }
    }
    const XmlNode* loop = kid(n, "standardLoopCharacteristics");
    if (!loop) loop = kid(n, "multiInstanceLoopCharacteristics");
    if (loop)
      b.set_property(id, "bpmn.loop",
                     local(loop->name) == "standardLoopCharacteristics" ? "standard"
                     : attr_is(*loop, "isSequential", "true")          ? "sequential"
                                                                        : "parallel");
    if (const XmlNode* script = kid(n, "script")) b.set_property(id, "bpmn.script", text_of(script));
  };

  /// Lanes of a process: flow node id → lane id (the innermost lane listing it).
  std::map<std::string, std::string, std::less<>> lane_of;
  auto lane_for = [&](std::string_view id) -> std::optional<std::string> {
    auto it = lane_of.find(id);
    return it == lane_of.end() ? std::nullopt : std::optional<std::string>(it->second);
  };
  auto add_lanes = [&](auto& self, const XmlNode& lane_set, const std::optional<std::string>& parent) -> void {
    for (const XmlNode* lane : kids(lane_set, "lane")) {
      auto key = add_node(*lane, "lane", parent);
      if (!key) continue;
      for (const XmlNode* ref : kids(*lane, "flowNodeRef")) lane_of[trimmed(text_of(ref))] = *key;
      if (const XmlNode* child = kid(*lane, "childLaneSet")) self(self, *child, *key);
    }
  };

  /// The flow elements of a process or sub-process, under `parent`.
  auto add_contents = [&](auto& self, const XmlNode& container, const std::optional<std::string>& parent,
                          bool in_lanes) -> void {
    Nodes later;
    std::vector<std::pair<const XmlNode*, std::string>> nested;
    // Process data inputs and outputs are drawn when the diagram shows them.
    if (const XmlNode* io = kid(container, "ioSpecification")) {
      Nodes ds = kids(*io, "dataInput");
      for (const XmlNode* d : kids(*io, "dataOutput")) ds.push_back(d);
      for (const XmlNode* d : ds)
        if (di_of(*d)) add_node(*d, std::string(local(d->name)), parent);
    }
    for (const auto& child : container.children) {
      const XmlNode& n = *child;
      std::string type(local(n.name));
      if (is_non_visual(type) || is_flow(type) || ends_with(type, "EventDefinition")) continue;
      if (type == "boundaryEvent") {
        later.push_back(&n);
        continue;
      }
      // Definitions that are only drawn when the diagram shows them.
      if ((type == "dataStore" || type == "message") && !di_of(n)) continue;
      bool visual = is_event(type) || is_task(type) || is_subprocess(type) || is_gateway(type) || is_data(type) ||
                    is_artifact(type) || is_choreography(type) || is_conversation(type);
      if (!visual) {
        b.drop("<" + type + "> is not imported", std::nullopt, "element " + type);
        continue;
      }
      std::optional<std::string> lane = in_lanes ? lane_for(attr_or(n, "id")) : std::nullopt;
      auto key = add_node(n, type, lane ? lane : parent);
      if (!key) continue;
      describe_node(n, *key);
      if (is_subprocess(type) || type == "subChoreography") nested.emplace_back(&n, *key);
    }
    for (const XmlNode* n : later) {
      auto host = attr(*n, "attachedToRef");
      bool host_known = host && !host->empty() && key_of(*host);
      // Boundary events sit next to their activity: same parent, attachment kept as a property.
      std::optional<ElementId> host_parent;
      if (host_known) host_parent = b.model().elements.find(*b.id(*host))->parent;
      std::optional<std::string> parent_key;
      if (host_parent) parent_key = b.key_of(*host_parent);
      else {
        std::optional<std::string> lane = in_lanes ? lane_for(host ? *host : std::string()) : std::nullopt;
        parent_key = lane ? lane : parent;
      }
      if (!host_known) {
        auto id = attr(*n, "id");
        b.drop("Boundary event " + (id ? *id : std::string("undefined")) + " is attached to an unknown activity",
               std::nullopt, "boundaryEvent");
      }
      if (auto key = add_node(*n, "boundaryEvent", parent_key)) describe_node(*n, *key);
    }
    for (const auto& [n, id] : nested) self(self, *n, id, false);
  };

  Nodes processes = kids(*definitions, "process");
  std::set<std::string, std::less<>> used;
  Nodes collaborations = kids(*definitions, "collaboration");
  for (const XmlNode* c : kids(*definitions, "choreography")) collaborations.push_back(c);
  for (const XmlNode* c : kids(*definitions, "globalConversation")) collaborations.push_back(c);
  for (const XmlNode* collaboration : collaborations) {
    for (const XmlNode* participant : kids(*collaboration, "participant")) {
      auto key = add_node(*participant, "participant", std::nullopt);
      if (!key) continue;
      const XmlNode* process = lookup(attr_or(*participant, "processRef"));
      if (process && local(process->name) == "process" && !used.count(attr_or(*process, "id"))) {
        used.insert(attr_or(*process, "id"));
        std::string pname = attr_or(*process, "name");
        if (!pname.empty()) b.set_property(*key, "bpmn.processName", pname);
        std::string exec = attr_or(*process, "isExecutable");
        if (!exec.empty()) b.set_property(*key, "bpmn.isExecutable", exec);
        for (const XmlNode* set : kids(*process, "laneSet")) add_lanes(add_lanes, *set, *key);
        add_contents(add_contents, *process, *key, true);
      }
    }
    // Conversations, choreography activities and artifacts of the collaboration itself.
    add_contents(add_contents, *collaboration, std::nullopt, false);
  }
  for (const XmlNode* process : processes) {
    if (used.count(attr_or(*process, "id"))) continue;
    for (const XmlNode* set : kids(*process, "laneSet")) add_lanes(add_lanes, *set, std::nullopt);
    add_contents(add_contents, *process, std::nullopt, true);
  }
  // Root-level elements that are only drawn when the diagram shows them (messages, data stores).
  for (const auto& child : definitions->children) {
    std::string type(local(child->name));
    if ((type == "message" || type == "dataStore") && di_of(*child) && !b.has(attr_or(*child, "id")))
      add_node(*child, type, std::nullopt);
  }

  // ---------------------------------------------------------------- flows
  std::map<std::string, RelationshipId, std::less<>> flow_ids;
  auto add_flow = [&](const XmlNode& n, const std::string& type, const std::optional<std::string>& source,
                      const std::optional<std::string>& target) {
    bool s = source && !source->empty() && key_of(*source);
    bool t = target && !target->empty() && key_of(*target);
    if (!s || !t) {
      b.drop(type + " " + attr_or(n, "id") + " connects " + (source ? *source : "?") + " → " +
                 (target ? *target : "?") + ", which are not both drawn elements",
             std::nullopt, "flow " + type);
      return;
    }
    Properties props = properties(n, type);
    Style style;
    if (type == "sequenceFlow") {
      style.set("endArrow", "block");
      style.set("endFill", "1");
      const XmlNode* src = lookup(*source);
      const XmlNode* condition = kid(n, "conditionExpression");
      if (condition) props.set("bpmn.condition", trimmed(text_of(condition)));
      if (src && attr(*src, "default") == attr(n, "id")) {
        style.set("startArrow", "dash");
        props.set("bpmn.isDefault", "true");
      } else if (condition && src && !is_gateway(local(src->name))) {
        style.set("startArrow", "diamond");
        style.set("startFill", "0");
      }
    } else if (type == "messageFlow") {
      style = {{"dashed", "1"}, {"dashPattern", "6 3"}, {"startArrow", "oval"},
               {"startFill", "0"}, {"endArrow", "block"}, {"endFill", "0"}};
    } else if (type == "conversationLink") {
      style = {{"endArrow", "none"}, {"strokeWidth", "2.5"}};
    } else {
      auto direction = attr(n, "associationDirection");
      bool one_or_both = direction && (*direction == "One" || *direction == "Both");
      style = {{"dashed", "1"},
               {"dashPattern", "2 3"},
               {"endArrow", type == "association" ? (one_or_both ? "open" : "none") : "open"}};
      if (direction && *direction == "Both") style.set("startArrow", "open");
    }
    std::string label = trimmed(collapse_whitespace(attr_or(n, "name")));
    RelationshipInit init;
    if (!label.empty()) init.label = label;
    const std::string* dashed = style.find("dashed");
    init.line = dashed && *dashed == "1" ? LineStyle::Dashed : LineStyle::Solid;
    init.style = std::move(style);
    init.properties = std::move(props);
    auto rel = b.add_relationship(*source, *target, std::move(init));
    if (!rel) return;
    std::string id = attr_or(n, "id");
    if (!id.empty()) flow_ids[id] = *rel;
    ++stat("relationships");
    ++stat("type:" + type);
  };
  auto walk_flows = [&](auto& self, const XmlNode& n) -> void {
    for (const auto& child : n.children) {
      const XmlNode& c = *child;
      std::string type(local(c.name));
      if (type == "sequenceFlow" || type == "messageFlow" || type == "association" || type == "conversationLink") {
        add_flow(c, type, attr(c, "sourceRef"), attr(c, "targetRef"));
      } else if (type == "dataInputAssociation" || type == "dataOutputAssociation") {
        // Inside an activity: data → activity, or activity → data.
        auto activity = attr(n, "id");
        std::vector<std::string> sources;
        for (const XmlNode* r : kids(c, "sourceRef")) sources.push_back(trimmed(text_of(r)));
        std::string target = trimmed(text_of(kid(c, "targetRef")));
        if (type == "dataInputAssociation") {
          // The target is usually the activity's own (undrawn) data input: draw it to the activity.
          for (const auto& s : sources) add_flow(c, type, s, key_of(target) ? std::optional(target) : activity);
        } else {
          std::string first = sources.empty() ? std::string() : sources[0];
          add_flow(c, type,
                   key_of(first) ? std::optional<std::string>(sources[0]) : activity, target);
        }
      }
      if (!is_flow(type)) self(self, c);
    }
  };
  walk_flows(walk_flows, *definitions);

  // ---------------------------------------------------------------- views
  auto relative = [](const Bounds& box, const Point2& p) -> Point2 {
    return {box.width != 0 ? std::min(1.0, std::max(0.0, round2((p.x - box.x) / box.width))) : 0.5,
            box.height != 0 ? std::min(1.0, std::max(0.0, round2((p.y - box.y) / box.height))) : 0.5};
  };
  if (keep) {
    for (const Plane& plane : planes) {
      if (plane.shapes.items.empty()) continue;
      std::vector<std::string> include;
      std::vector<LayoutHint> hints;
      for (const auto& [ref, shape] : plane.shapes.items) {
        auto id = b.id(ref);
        if (!id) {
          if (!lookup(ref)) b.drop("Diagram \"" + plane.name + "\" draws an unknown element " + ref, std::nullopt, "di shape");
          continue;
        }
        include.push_back(ref);
        const Bounds& box = shape.bounds;
        hints.emplace_back(PinAtHint{*id, round2(box.x), round2(box.y), round2(box.width), round2(box.height)});
      }
      for (const auto& [ref, points] : plane.edges.items) {
        auto it = flow_ids.find(ref);
        if (it == flow_ids.end() || points.size() < 2) continue;
        const Relationship& r = *b.model().relationships.find(it->second);
        auto source_key = b.key_of(r.source.element);
        auto target_key = b.key_of(r.target.element);
        const DiShape* source_shape = plane.shapes.get(source_key ? *source_key : std::string());
        const DiShape* target_shape = plane.shapes.get(target_key ? *target_key : std::string());
        RouteHint route{it->second, {}, std::nullopt, std::nullopt};
        for (std::size_t i = 1; i + 1 < points.size(); ++i) route.points.push_back({round2(points[i].x), round2(points[i].y)});
        if (source_shape) route.exit = relative(source_shape->bounds, points.front());
        if (target_shape) route.entry = relative(target_shape->bounds, points.back());
        hints.emplace_back(std::move(route));
      }
      if (include.empty()) continue;
      ViewInit view;
      view.algorithm = "fixed";
      view.include = std::move(include);
      view.hints = std::move(hints);
      b.add_view(plane.name, std::move(view));
      ++stat("views");
    }
  }
  if (stat("views") == 0 && !b.model().elements.empty()) {
    ViewInit view;
    view.algorithm = "layered";
    view.direction = Direction::Right;
    b.add_view("process", std::move(view));
    ++stat("views");
    if (keep && planes.empty()) b.note("The file has no diagram interchange; the view is laid out automatically");
  }

  ImportResult r = b.result();
  r.report.stats = stats;
  if (!unknown_shapes.empty()) r.report.unknown_shapes = unknown_shapes;
  return r;
}

}  // namespace dd::io
