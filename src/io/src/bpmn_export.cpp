// Port of packages/import/src/bpmn/export.ts.

#include <functional>
#include <map>
#include <set>

#include "bpmn_model.hpp"
#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/diadraw.h"

namespace dd::io {

using namespace bpmn;

namespace {

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool ends_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool contains(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }
bool is_word(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// `props[key]`, or nullptr.
const std::string* prop(const Properties& props, std::string_view key) { return props.find(key); }
/// `props[key]` is set and not empty (JavaScript truthiness).
bool truthy(const std::string* v) { return v && !v->empty(); }

/// `s.replace(/_(\w)/g, (_, c) => c.toUpperCase())`.
std::string camel(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '_' && i + 1 < s.size() && is_word(s[i + 1])) {
      char c = s[i + 1];
      out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
      ++i;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

/// BPMN type for an element without a `bpmn.type` (drawn in DiaDraw), from its shape or kind.
std::string type_of(const Element& e) {
  if (const std::string* t = prop(e.properties, "bpmn.type"); truthy(t)) return *t;
  std::string shape = e.shape && starts_with(*e.shape, "bpmn.") ? e.shape->substr(5) : "";
  if (starts_with(shape, "start_event") || starts_with(shape, "start_non_interrupting_event")) return "startEvent";
  if (starts_with(shape, "end_event")) return "endEvent";
  if (starts_with(shape, "intermediate_throw_event")) return "intermediateThrowEvent";
  if (starts_with(shape, "intermediate_catch"))
    return truthy(prop(e.properties, "bpmn.attachedToRef")) || truthy(prop(e.properties, "bpmn.attachedTo"))
               ? "boundaryEvent"
               : "intermediateCatchEvent";
  std::string base = shape;
  for (const char* suffix : {"_loop", "_mi_parallel", "_mi_sequential", "_compensation"})
    if (ends_with(base, suffix)) {
      base.resize(base.size() - std::string_view(suffix).size());
      break;
    }
  static const std::map<std::string, std::string, std::less<>> kDirect{
      {"subprocess", "subProcess"},
      {"event_subprocess", "subProcess"},
      {"ad_hoc_subprocess", "adHocSubProcess"},
      {"call_subprocess", "callActivity"},
      {"exclusive_gateway_plain", "exclusiveGateway"},
      {"event_based_gateway_start", "eventBasedGateway"},
      {"parallel_event_based_gateway", "eventBasedGateway"},
      {"data_object", "dataObjectReference"},
      {"data_object_collection", "dataObjectReference"},
      {"data_store", "dataStoreReference"}};
  if (auto it = kDirect.find(base); it != kDirect.end()) return it->second;
  if (!base.empty()) {
    for (const char* word : {"task", "activity", "gateway", "annotation", "conversation", "transaction", "group",
                             "message", "data_input", "data_output"})
      if (contains(base, word)) {
        std::string_view b = base;
        if (ends_with(b, "_collection")) b.remove_suffix(11);
        return camel(b);
      }
  }
  const std::string& k = e.kind;
  if (k == "event" || k == "start") return "startEvent";
  if (k == "end") return "endEvent";
  if (k == "gateway" || k == "decision") return "exclusiveGateway";
  if (k == "subprocess") return "subProcess";
  if (k == "pool") return "participant";
  if (k == "lane") return "lane";
  if (k == "data") return "dataObjectReference";
  if (k == "database") return "dataStoreReference";
  if (k == "annotation" || k == "note") return "textAnnotation";
  if (k == "group") return "group";
  return "task";
}

/// Properties that belong to the export itself, not written as attributes.
bool is_special(std::string_view k) {
  static const std::set<std::string, std::less<>> kSpecial{
      "bpmn.type",         "bpmn.id",        "bpmn.eventDefinitions", "bpmn.timer",          "bpmn.loop",
      "bpmn.script",       "bpmn.extensions", "bpmn.condition",       "bpmn.isDefault",      "bpmn.processName",
      "bpmn.isExecutable", "bpmn.processRef", "bpmn.attachedToRef",   "bpmn.attachedTo",     "bpmn.default",
      "bpmn.messageRef",   "bpmn.signalRef", "bpmn.errorRef",         "bpmn.escalationRef",  "bpmn.activityRef",
      "bpmn.link",         "bpmn.categoryValueRef", "bpmn.dataObjectRef", "bpmn.sourceRef",   "bpmn.targetRef"};
  return kSpecial.count(k) > 0;
}

bool is_flow_node(std::string_view t) {
  return ends_with(t, "Event") || ends_with(t, "Task") || t == "task" || t == "callActivity" || t == "subProcess" ||
         t == "transaction" || t == "adHocSubProcess" || ends_with(t, "Gateway") || t == "choreographyTask" ||
         t == "subChoreography" || t == "callChoreography";
}
bool is_container_type(std::string_view t) {
  return t == "subProcess" || t == "transaction" || t == "adHocSubProcess" || t == "subChoreography";
}

/// `id.replace(/[^\w.-]/g, '_').replace(/^(?=[\d.-])/, '_')`, per UTF-16 code unit.
std::string sanitize(std::string_view id) {
  std::string out;
  for (std::size_t i = 0; i < id.size();) {
    char c = id[i];
    if (static_cast<unsigned char>(c) < 0x80) {
      out.push_back(is_word(c) || c == '.' || c == '-' ? c : '_');
      ++i;
      continue;
    }
    std::uint32_t cp = next_code_point(id, i);
    out += cp > 0xFFFF ? "__" : "_";
  }
  if (!out.empty() && ((out[0] >= '0' && out[0] <= '9') || out[0] == '.' || out[0] == '-')) out.insert(0, "_");
  return out;
}

std::string num(double v) { return js_number_string(js_round(v * 100) / 100); }

/// `s.split(sep)`.
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

using OptId = std::optional<ElementId>;

}  // namespace

std::string export_bpmn(const Model& model, const std::vector<BpmnPage>& pages) {
  std::vector<const Element*> elements;
  for (const Element& e : model.elements) elements.push_back(&e);
  std::vector<const Relationship*> relationships;
  for (const Relationship& r : model.relationships) relationships.push_back(&r);
  std::map<ElementId, std::string, std::less<>> type;
  for (const Element* e : elements) type[e->id] = type_of(*e);
  auto type_at = [&](std::string_view id) -> std::string {
    auto it = type.find(id);
    return it == type.end() ? std::string() : it->second;
  };

  // XML ids: the original BPMN ids when they are still unique, else the DiaDraw id.
  std::map<ElementId, std::string, std::less<>> xml_id;
  std::set<std::string, std::less<>> taken;
  for (const Element* e : elements) {
    const std::string* original = prop(e->properties, "bpmn.id");
    std::string dotted = e->id;
    for (auto& c : dotted)
      if (c == '.') c = '_';
    std::string id = original ? *original : sanitize(dotted);
    while (taken.count(id)) id += "_";
    taken.insert(id);
    xml_id[e->id] = id;
  }
  auto xid = [&](std::string_view id) -> std::string {
    auto it = xml_id.find(id);
    return it == xml_id.end() ? std::string("undefined") : it->second;
  };
  std::map<std::string, ElementId, std::less<>> by_original;
  for (const Element* e : elements) {
    const std::string* original = prop(e->properties, "bpmn.id");
    by_original[original ? *original : std::string()] = e->id;
  }
  std::map<RelationshipId, std::string, std::less<>> rel_ids;
  for (const Relationship* r : relationships) {
    const std::string* original = prop(r->properties, "bpmn.id");
    std::string id = original ? *original : sanitize("Flow_" + r->id);
    while (taken.count(id)) id += "_";
    taken.insert(id);
    rel_ids[r->id] = id;
  }
  auto fresh = [&](const std::string& base) {
    std::string id = base;
    for (int n = 2; taken.count(id); ++n) id = base + "_" + std::to_string(n);
    taken.insert(id);
    return id;
  };

  std::map<OptId, std::vector<const Element*>> children;
  for (const Element* e : elements) children[e->parent].push_back(e);
  auto kids_of = [&](const OptId& id) -> const std::vector<const Element*>& {
    static const std::vector<const Element*> kNone;
    auto it = children.find(id);
    return it == children.end() ? kNone : it->second;
  };
  std::vector<const Element*> pools;
  for (const Element* e : elements)
    if (type_at(e->id) == "participant") pools.push_back(e);
  auto pool_of = [&](const ElementId& id) -> OptId {
    for (const Element* e = model.elements.find(id); e; e = e->parent ? model.elements.find(*e->parent) : nullptr)
      if (type_at(e->id) == "participant") return e->id;
    return std::nullopt;
  };
  /// The process or sub-process an element's XML goes in (lanes are transparent).
  auto container_of = [&](const ElementId& id) -> OptId {
    auto parent_of = [&](const ElementId& x) -> OptId {
      const Element* e = model.elements.find(x);
      return e ? e->parent : std::nullopt;
    };
    for (OptId p = parent_of(id); p; p = parent_of(*p)) {
      std::string t = type_at(*p);
      if (t == "participant" || is_container_type(t)) return p;
    }
    return std::nullopt;
  };

  std::vector<std::pair<std::string, std::string>> xmlns;
  auto collect_xmlns = [&](const Properties& props) {
    for (const auto& [k, v] : props) {
      if (!starts_with(k, "bpmn.xmlns.")) continue;
      std::string prefix = k.substr(11);
      bool found = false;
      for (auto& [p, uri] : xmlns)
        if (p == prefix) {
          uri = v;
          found = true;
        }
      if (!found) xmlns.emplace_back(prefix, v);
    }
  };
  for (const Element* e : elements) collect_xmlns(e->properties);
  for (const Relationship* r : relationships) collect_xmlns(r->properties);

  auto attrs = [&](const Properties& props) {
    std::string out;
    for (const auto& [k, v] : props) {
      if (!starts_with(k, "bpmn.") || is_special(k) || starts_with(k, "bpmn.xmlns.")) continue;
      out += " " + (starts_with(k, "bpmn.attr.") ? k.substr(10) : k.substr(5)) + "=\"" + escape_attr(v) + "\"";
    }
    return out;
  };
  auto common = [&](const Properties& props) -> std::string {
    const std::string* ext = prop(props, "bpmn.extensions");
    return truthy(ext) ? "<bpmn:extensionElements>" + *ext + "</bpmn:extensionElements>" : "";
  };

  std::vector<std::string> categories;
  /// Root-level elements (messages, data stores) and process inputs/outputs, written apart.
  std::vector<std::string> roots;
  std::map<OptId, std::vector<std::string>> io;
  std::map<OptId, std::vector<std::string>> data_objects;

  /// How a relationship is written, and where.
  auto flow_kind = [&](const Relationship& r) -> std::string {
    if (const std::string* t = prop(r.properties, "bpmn.type"); truthy(t)) return *t;
    std::string s = type_at(r.source.element);
    std::string d = type_at(r.target.element);
    if (s == "textAnnotation" || d == "textAnnotation" || s == "group" || d == "group") return "association";
    if (starts_with(s, "data") && is_flow_node(d)) return "dataInputAssociation";
    if (is_flow_node(s) && starts_with(d, "data")) return "dataOutputAssociation";
    if (pool_of(r.source.element) != pool_of(r.target.element) || s == "participant" || d == "participant")
      return "messageFlow";
    return "sequenceFlow";
  };
  std::map<RelationshipId, std::string, std::less<>> kinds;
  for (const Relationship* r : relationships) kinds[r->id] = flow_kind(*r);

  auto event_definitions_xml = [&](const Element& e) {
    struct Def {
      std::string name;
      std::optional<std::string> id;
    };
    std::vector<Def> defs;
    const std::string* listed_text = prop(e.properties, "bpmn.eventDefinitions");
    std::vector<std::string> listed;
    for (auto& d : split(listed_text ? *listed_text : "", ','))
      if (!d.empty()) listed.push_back(d);
    if (!listed.empty()) {
      for (const auto& d : listed) {
        auto parts = split(d, '#');
        defs.push_back({parts[0], parts.size() > 1 ? std::optional<std::string>(parts[1]) : std::nullopt});
      }
    } else {
      // Drawn in DiaDraw: the marker in the shape name.
      for (const auto& [element, marker] : event_definitions())
        if (e.shape && ends_with(*e.shape, "_" + std::string(marker))) {
          defs.push_back({std::string(element), std::nullopt});
          break;
        }
    }
    std::string out;
    for (const auto& [name, id] : defs) {
      std::string id_attr = " id=\"" + escape_attr(id ? *id : fresh(xid(e.id) + "_" + name)) + "\"";
      std::string refs;
      for (const auto& [k, prefix] : std::vector<std::pair<std::string, std::string>>{{"messageRef", "message"},
                                                                                       {"signalRef", "signal"},
                                                                                       {"errorRef", "error"},
                                                                                       {"escalationRef", "escalation"},
                                                                                       {"activityRef", "compensate"}}) {
        const std::string* v = prop(e.properties, "bpmn." + k);
        if (truthy(v) && starts_with(name, prefix)) refs += " " + k + "=\"" + escape_attr(*v) + "\"";
      }
      std::string link;
      if (const std::string* l = prop(e.properties, "bpmn.link"); name == "linkEventDefinition" && truthy(l))
        link = " name=\"" + escape_attr(*l) + "\"";
      std::string inner;
      if (const std::string* timer = prop(e.properties, "bpmn.timer"); name == "timerEventDefinition" && truthy(timer))
        for (const auto& part : split(*timer, ';')) {
          auto eq = part.find('=');
          std::string k = part.substr(0, eq);
          std::string v = eq == std::string::npos ? "" : part.substr(eq + 1);
          if (!k.empty())
            inner += "<bpmn:" + k + " xsi:type=\"bpmn:tFormalExpression\">" + escape_text(v) + "</bpmn:" + k + ">";
        }
      if (name == "conditionalEventDefinition") {
        const std::string* c = prop(e.properties, "bpmn.condition");
        inner = "<bpmn:condition xsi:type=\"bpmn:tFormalExpression\">" + escape_text(c ? *c : "") + "</bpmn:condition>";
      }
      out += inner.empty() ? "<bpmn:" + name + id_attr + refs + link + "/>"
                           : "<bpmn:" + name + id_attr + refs + link + ">" + inner + "</bpmn:" + name + ">";
    }
    return out;
  };

  std::function<std::string(const OptId&)> contents;

  auto node_xml = [&](const Element& e) -> std::string {
    std::string t = type_at(e.id);
    std::string id = xid(e.id);
    std::string name = e.name.empty() ? "" : " name=\"" + escape_attr(e.name) + "\"";
    std::string doc = e.description && !e.description->empty()
                          ? "<bpmn:documentation>" + escape_text(*e.description) + "</bpmn:documentation>"
                          : "";
    std::string extra, head;
    if (t == "boundaryEvent") {
      const std::string* a = prop(e.properties, "bpmn.attachedToRef");
      if (!a) a = prop(e.properties, "bpmn.attachedTo");
      std::string host = a ? *a : "";
      OptId host_id;
      if (auto it = by_original.find(host); it != by_original.end()) host_id = it->second;
      else if (model.elements.contains(host)) host_id = host;
      head += " attachedToRef=\"" + escape_attr(host_id && !host_id->empty() ? xid(*host_id) : host) + "\"";
    }
    if (ends_with(t, "Gateway") || is_flow_node(t)) {
      for (const Relationship* r : relationships) {
        const std::string* d = prop(r->properties, "bpmn.isDefault");
        if (r->source.element == e.id && d && *d == "true") {
          head += " default=\"" + rel_ids[r->id] + "\"";
          break;
        }
      }
    }
    if (t == "textAnnotation")
      return "<bpmn:textAnnotation id=\"" + id + "\"" + attrs(e.properties) + ">" + doc + common(e.properties) +
             "<bpmn:text>" + escape_text(e.name) + "</bpmn:text></bpmn:textAnnotation>";
    if (t == "group") {
      std::string value = fresh(id + "_value");
      std::string category = fresh(id + "_category");
      categories.push_back("<bpmn:category id=\"" + category + "\"><bpmn:categoryValue id=\"" + value + "\" value=\"" +
                           escape_attr(e.name) + "\"/></bpmn:category>");
      return "<bpmn:group id=\"" + id + "\" categoryValueRef=\"" + value + "\"" + attrs(e.properties) + ">" + doc +
             common(e.properties) + "</bpmn:group>";
    }
    if (t == "dataObjectReference") {
      const std::string* r = prop(e.properties, "bpmn.dataObjectRef");
      std::string ref = r ? *r : fresh(id + "_object");
      std::string collection = e.shape && contains(*e.shape, "collection") ? " isCollection=\"true\"" : "";
      data_objects[container_of(e.id)].push_back("<bpmn:dataObject id=\"" + escape_attr(ref) + "\"" + collection + "/>");
      head += " dataObjectRef=\"" + escape_attr(ref) + "\"";
    }
    if (ends_with(t, "Event")) extra += event_definitions_xml(e);
    std::optional<std::string> loop;
    if (const std::string* l = prop(e.properties, "bpmn.loop")) loop = *l;
    else {
      std::string shape = e.shape ? *e.shape : "";
      if (ends_with(shape, "_loop")) loop = "standard";
      else if (ends_with(shape, "_mi_parallel")) loop = "parallel";
      else if (ends_with(shape, "_mi_sequential")) loop = "sequential";
    }
    if (loop == "standard") extra += "<bpmn:standardLoopCharacteristics/>";
    else if (loop && !loop->empty())
      extra += std::string("<bpmn:multiInstanceLoopCharacteristics") +
               (*loop == "sequential" ? " isSequential=\"true\"" : "") + "/>";
    if (const std::string* script = prop(e.properties, "bpmn.script"))
      extra += "<bpmn:script>" + escape_text(*script) + "</bpmn:script>";
    if (e.shape && starts_with(*e.shape, "bpmn.event_subprocess") &&
        !truthy(prop(e.properties, "bpmn.triggeredByEvent")))
      head += " triggeredByEvent=\"true\"";
    // Data associations live inside their activity.
    for (const Relationship* r : relationships) {
      const std::string& k = kinds[r->id];
      if (k == "dataInputAssociation" && r->target.element == e.id)
        extra += "<bpmn:dataInputAssociation id=\"" + rel_ids[r->id] + "\"><bpmn:sourceRef>" + xid(r->source.element) +
                 "</bpmn:sourceRef></bpmn:dataInputAssociation>";
      if (k == "dataOutputAssociation" && r->source.element == e.id)
        extra += "<bpmn:dataOutputAssociation id=\"" + rel_ids[r->id] + "\"><bpmn:targetRef>" +
                 xid(r->target.element) + "</bpmn:targetRef></bpmn:dataOutputAssociation>";
    }
    std::string incoming, outgoing;
    for (const Relationship* r : relationships)
      if (kinds[r->id] == "sequenceFlow" && r->target.element == e.id)
        incoming += "<bpmn:incoming>" + rel_ids[r->id] + "</bpmn:incoming>";
    for (const Relationship* r : relationships)
      if (kinds[r->id] == "sequenceFlow" && r->source.element == e.id)
        outgoing += "<bpmn:outgoing>" + rel_ids[r->id] + "</bpmn:outgoing>";
    std::string body = is_container_type(t) ? contents(e.id) : "";
    return "<bpmn:" + t + " id=\"" + id + "\"" + name + head + attrs(e.properties) + ">" + doc + common(e.properties) +
           (is_flow_node(t) ? incoming + outgoing : "") + extra + body + "</bpmn:" + t + ">";
  };

  /// Lanes under a pool (or a process without pools), with the flow nodes in each.
  std::function<std::string(const OptId&)> lane_set = [&](const OptId& parent) -> std::string {
    std::vector<const Element*> lanes;
    for (const Element* e : kids_of(parent))
      if (type_at(e->id) == "lane") lanes.push_back(e);
    if (lanes.empty()) return "";
    std::function<void(const ElementId&, std::vector<std::string>&)> nodes_in = [&](const ElementId& lane,
                                                                                      std::vector<std::string>& out) {
      for (const Element* e : kids_of(lane)) {
        std::string t = type_at(e->id);
        if (t == "lane") nodes_in(e->id, out);
        else if (is_flow_node(t)) out.push_back(xid(e->id));
      }
    };
    auto it = parent ? xml_id.find(*parent) : xml_id.find(std::string_view());
    std::string out = "<bpmn:laneSet id=\"" + fresh("LaneSet_" + (it == xml_id.end() ? std::string("process") : it->second)) + "\">";
    for (const Element* l : lanes) {
      std::string sub = lane_set(l->id);
      out += "<bpmn:lane id=\"" + xid(l->id) + "\"" + (l->name.empty() ? "" : " name=\"" + escape_attr(l->name) + "\"") + ">";
      std::vector<std::string> nodes;
      nodes_in(l->id, nodes);
      for (const auto& n : nodes) out += "<bpmn:flowNodeRef>" + n + "</bpmn:flowNodeRef>";
      if (!sub.empty()) {
        if (auto at = sub.find("<bpmn:laneSet"); at != std::string::npos) sub.replace(at, 13, "<bpmn:childLaneSet");
        if (ends_with(sub, "</bpmn:laneSet>")) sub.replace(sub.size() - 15, 15, "</bpmn:childLaneSet>");
        out += sub;
      }
      out += "</bpmn:lane>";
    }
    return out + "</bpmn:laneSet>";
  };

  /// Flow elements of a process (pool) or sub-process: nodes, lanes skipped through, flows.
  contents = [&](const OptId& container) -> std::string {
    std::string out;
    std::function<void(const OptId&)> walk = [&](const OptId& parent) {
      for (const Element* e : kids_of(parent)) {
        std::string t = type_at(e->id);
        if (t == "lane") {
          walk(e->id);
          continue;
        }
        if (t == "participant") continue;
        if (t == "message" || t == "dataStore") roots.push_back(node_xml(*e));
        else if (t == "dataInput" || t == "dataOutput") {
          std::string xml = node_xml(*e);
          io[container].push_back(std::move(xml));
        } else {
          out += node_xml(*e);
        }
      }
    };
    walk(container);
    for (const Relationship* r : relationships) {
      const std::string& k = kinds[r->id];
      if (k != "sequenceFlow" && k != "association") continue;
      if (container_of(r->source.element) != container) continue;
      if (k == "association" && pool_of(r->source.element) != pool_of(r->target.element)) continue;
      std::string name = r->label && !r->label->empty() ? " name=\"" + escape_attr(*r->label) + "\"" : "";
      std::string ends = " sourceRef=\"" + xid(r->source.element) + "\" targetRef=\"" + xid(r->target.element) + "\"";
      const std::string* cond = prop(r->properties, "bpmn.condition");
      std::string open = "<bpmn:" + k + " id=\"" + rel_ids[r->id] + "\"" + name + ends + attrs(r->properties) + ">" +
                         common(r->properties);
      out += cond && k == "sequenceFlow"
                 ? open + "<bpmn:conditionExpression xsi:type=\"bpmn:tFormalExpression\">" + escape_text(*cond) +
                       "</bpmn:conditionExpression></bpmn:" + k + ">"
                 : open + "</bpmn:" + k + ">";
    }
    std::string lanes = !container || type_at(*container) == "participant" ? lane_set(container) : "";
    std::string spec;
    if (auto it = io.find(container); it != io.end() && !it->second.empty()) {
      spec = "<bpmn:ioSpecification id=\"" + fresh("IoSpecification") + "\">";
      for (const auto& x : it->second) spec += x;
      std::string input = fresh("InputSet");
      std::string output = fresh("OutputSet");
      spec += "<bpmn:inputSet id=\"" + input + "\"/><bpmn:outputSet id=\"" + output + "\"/></bpmn:ioSpecification>";
    }
    std::string objects;
    if (auto it = data_objects.find(container); it != data_objects.end())
      for (const auto& x : it->second) objects += x;
    return spec + lanes + objects + out;
  };

  // Processes: one per pool, and one for elements outside any pool.
  std::vector<std::string> processes;
  std::map<ElementId, std::string, std::less<>> process_ids;
  for (const Element* pool : pools) {
    const std::string* ref = prop(pool->properties, "bpmn.processRef");
    bool has_content = !kids_of(pool->id).empty() || truthy(ref);
    if (!has_content) continue;
    std::string id = ref ? *ref : fresh("Process_" + xid(pool->id));
    taken.insert(id);
    process_ids[pool->id] = id;
  }
  std::size_t loose = 0;
  for (const Element* e : kids_of(std::nullopt))
    if (type_at(e->id) != "participant") ++loose;
  for (const Element* pool : pools) {
    auto it = process_ids.find(pool->id);
    if (it == process_ids.end() || it->second.empty()) continue;
    std::string body = contents(pool->id);
    const std::string* name = prop(pool->properties, "bpmn.processName");
    const std::string* exec = prop(pool->properties, "bpmn.isExecutable");
    processes.push_back("<bpmn:process id=\"" + escape_attr(it->second) + "\"" +
                        (truthy(name) ? " name=\"" + escape_attr(*name) + "\"" : "") + " isExecutable=\"" +
                        (exec ? *exec : "false") + "\">" + body + "</bpmn:process>");
  }
  std::optional<std::string> loose_process;
  if (loose > 0 || pools.empty()) {
    loose_process = fresh("Process_1");
    std::string body = contents(std::nullopt);
    processes.insert(processes.begin(),
                     "<bpmn:process id=\"" + *loose_process + "\" isExecutable=\"false\">" + body + "</bpmn:process>");
  }

  // The collaboration: participants and message flows (and associations across pools).
  std::string collaboration;
  std::optional<std::string> collaboration_id;
  if (!pools.empty()) collaboration_id = fresh("Collaboration_1");
  if (collaboration_id) {
    collaboration = "<bpmn:collaboration id=\"" + *collaboration_id + "\">";
    for (const Element* p : pools) {
      auto it = process_ids.find(p->id);
      std::string ref = it == process_ids.end() ? "" : it->second;
      collaboration += "<bpmn:participant id=\"" + xid(p->id) + "\"" +
                       (p->name.empty() ? "" : " name=\"" + escape_attr(p->name) + "\"") +
                       (ref.empty() ? "" : " processRef=\"" + escape_attr(ref) + "\"") + attrs(p->properties) + ">" +
                       (p->description && !p->description->empty()
                            ? "<bpmn:documentation>" + escape_text(*p->description) + "</bpmn:documentation>"
                            : "") +
                       common(p->properties) + "</bpmn:participant>";
    }
    for (const Relationship* r : relationships) {
      const std::string& k = kinds[r->id];
      if (!(k == "messageFlow" ||
            (k == "association" && pool_of(r->source.element) != pool_of(r->target.element))))
        continue;
      collaboration += "<bpmn:" + k + " id=\"" + rel_ids[r->id] + "\"" +
                       (r->label && !r->label->empty() ? " name=\"" + escape_attr(*r->label) + "\"" : "") +
                       " sourceRef=\"" + xid(r->source.element) + "\" targetRef=\"" + xid(r->target.element) + "\"" +
                       attrs(r->properties) + ">" + common(r->properties) + "</bpmn:" + k + ">";
    }
    collaboration += "</bpmn:collaboration>";
  }

  // Diagram interchange, one diagram per page.
  std::vector<std::string> diagrams;
  for (std::size_t i = 0; i < pages.size(); ++i) {
    const BpmnPage& page = pages[i];
    std::vector<ElementId> ids;
    for (const auto& id : page.nodes.keys())
      if (model.elements.contains(id)) ids.push_back(id);
    // A page showing only the inside of one sub-process is that sub-process's plane.
    std::vector<OptId> inside;
    for (const auto& id : ids) inside.push_back(container_of(id));
    OptId sub;
    if (!inside.empty()) {
      bool all = true;
      for (const auto& c : inside)
        if (!c || c != inside[0] || !is_container_type(type_at(*c))) {
          all = false;
          break;
        }
      if (all && !page.nodes.contains(*inside[0])) sub = inside[0];
    }
    std::string plane = sub ? xid(*sub) : collaboration_id ? *collaboration_id : loose_process ? *loose_process : "Process_1";
    std::string shapes;
    for (const auto& id : ids) {
      const Element& e = *model.elements.find(id);
      std::string t = type_at(id);
      const Box& box = *page.nodes.find(id);
      std::string flags;
      if (t == "participant" || t == "lane") {
        const std::string* h = e.style ? e.style->find("horizontal") : nullptr;
        flags += std::string(" isHorizontal=\"") + (h && *h == "0" ? "true" : "false") + "\"";
      }
      if (is_container_type(t) || t == "callActivity") {
        bool expanded = false;
        for (const auto& c : ids) {
          const Element* ce = model.elements.find(c);
          if (ce && ce->parent == id) {
            expanded = true;
            break;
          }
        }
        flags += std::string(" isExpanded=\"") + (expanded ? "true" : "false") + "\"";
      }
      if (t == "exclusiveGateway")
        flags += std::string(" isMarkerVisible=\"") +
                 (e.shape && *e.shape == "bpmn.exclusive_gateway_plain" ? "false" : "true") + "\"";
      shapes += "<bpmndi:BPMNShape id=\"" + xid(id) + "_di\" bpmnElement=\"" + xid(id) + "\"" + flags +
                "><dc:Bounds x=\"" + num(box.x) + "\" y=\"" + num(box.y) + "\" width=\"" + num(box.width) +
                "\" height=\"" + num(box.height) + "\"/></bpmndi:BPMNShape>";
    }
    std::string edges;
    for (const auto& id : page.edges.keys()) {
      const EdgeRoute* route = page.edges.find(id);
      auto it = rel_ids.find(id);
      if (it == rel_ids.end()) continue;
      edges += "<bpmndi:BPMNEdge id=\"" + it->second + "_di\" bpmnElement=\"" + it->second + "\">";
      for (const auto& p : route->points) edges += "<di:waypoint x=\"" + num(p.x) + "\" y=\"" + num(p.y) + "\"/>";
      edges += "</bpmndi:BPMNEdge>";
    }
    std::string n = std::to_string(i + 1);
    diagrams.push_back("<bpmndi:BPMNDiagram id=\"BPMNDiagram_" + n + "\" name=\"" + escape_attr(page.view) +
                       "\"><bpmndi:BPMNPlane id=\"BPMNPlane_" + n + "\" bpmnElement=\"" + escape_attr(plane) + "\">" +
                       shapes + edges + "</bpmndi:BPMNPlane></bpmndi:BPMNDiagram>");
  }

  std::string extra_ns;
  for (const auto& [p, uri] : xmlns)
    if (p != "bpmn" && p != "bpmndi" && p != "dc" && p != "di" && p != "xsi")
      extra_ns += " xmlns:" + p + "=\"" + escape_attr(uri) + "\"";
  std::vector<std::string> lines{
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
      std::string("<bpmn:definitions xmlns:bpmn=\"") + kBpmnNs + "\" xmlns:bpmndi=\"" + kBpmndiNs + "\" xmlns:dc=\"" +
          kDcNs + "\" xmlns:di=\"" + kDiNs + "\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" + extra_ns +
          " id=\"Definitions_1\" targetNamespace=\"http://bpmn.io/schema/bpmn\" exporter=\"DiaDraw\" "
          "exporterVersion=\"" + dd_version() + "\">"};
  for (auto& c : categories) lines.push_back(std::move(c));
  for (auto& r : roots) lines.push_back(std::move(r));
  lines.push_back(collaboration);
  for (auto& p : processes) lines.push_back(std::move(p));
  for (auto& d : diagrams) lines.push_back(std::move(d));
  lines.emplace_back("</bpmn:definitions>");
  std::string out;
  for (const auto& line : lines) {
    if (line.empty()) continue;
    if (!out.empty()) out += "\n";
    out += line;
  }
  return out;
}

std::vector<BpmnPage> fixed_pages(const Model& model) {
  std::vector<BpmnPage> pages;
  for (const View& view : model.views) {
    BpmnPage page;
    page.view = view.id;
    for (const auto& h : view.hints)
      if (const auto* pin = std::get_if<PinAtHint>(&h))
        page.nodes.set(pin->element, Box{pin->x, pin->y, pin->width.value_or(0), pin->height.value_or(0)});
    auto centre = [](const Box& b) { return Point{b.x + b.width / 2, b.y + b.height / 2}; };
    auto at = [](const Box& b, const Point2& p) {
      return Point{js_round((b.x + b.width * p.x) * 100) / 100, js_round((b.y + b.height * p.y) * 100) / 100};
    };
    for (const auto& h : view.hints) {
      const auto* route = std::get_if<RouteHint>(&h);
      if (!route) continue;
      const Relationship* r = model.relationships.find(route->relationship);
      const Box* s = r ? page.nodes.find(r->source.element) : nullptr;
      const Box* t = r ? page.nodes.find(r->target.element) : nullptr;
      if (!s || !t) continue;
      EdgeRoute edge;
      edge.points.push_back(route->exit ? at(*s, *route->exit) : centre(*s));
      for (const auto& p : route->points) edge.points.push_back({p.x, p.y});
      edge.points.push_back(route->entry ? at(*t, *route->entry) : centre(*t));
      page.edges.set(route->relationship, std::move(edge));
    }
    if (!page.nodes.empty()) pages.push_back(std::move(page));
  }
  return pages;
}

}  // namespace dd::io
