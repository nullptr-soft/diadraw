// Port of packages/import/src/bpmn/model.ts.

#include "bpmn_model.hpp"

#include "diadraw/base/text.hpp"

namespace dd::io::bpmn {

namespace {
bool in(std::string_view type, std::initializer_list<std::string_view> set) {
  for (auto s : set)
    if (s == type) return true;
  return false;
}
}  // namespace

bool is_event(std::string_view t) {
  return in(t, {"startEvent", "endEvent", "intermediateCatchEvent", "intermediateThrowEvent", "boundaryEvent",
                "implicitThrowEvent"});
}
bool is_task(std::string_view t) {
  return in(t, {"task", "userTask", "serviceTask", "sendTask", "receiveTask", "manualTask", "businessRuleTask",
                "scriptTask", "callActivity"});
}
bool is_subprocess(std::string_view t) { return in(t, {"subProcess", "transaction", "adHocSubProcess"}); }
bool is_gateway(std::string_view t) {
  return in(t, {"exclusiveGateway", "inclusiveGateway", "parallelGateway", "complexGateway", "eventBasedGateway"});
}
bool is_choreography(std::string_view t) { return in(t, {"choreographyTask", "subChoreography", "callChoreography"}); }
bool is_conversation(std::string_view t) { return in(t, {"conversation", "subConversation", "callConversation"}); }
bool is_data(std::string_view t) {
  return in(t, {"dataObjectReference", "dataStoreReference", "dataInput", "dataOutput", "dataStore", "message"});
}
bool is_artifact(std::string_view t) { return in(t, {"textAnnotation", "group"}); }
bool is_flow(std::string_view t) {
  return in(t, {"sequenceFlow", "messageFlow", "association", "dataInputAssociation", "dataOutputAssociation",
                "conversationLink"});
}

const std::vector<std::pair<std::string_view, std::string_view>>& event_definitions() {
  static const std::vector<std::pair<std::string_view, std::string_view>> defs{
      {"messageEventDefinition", "message"},         {"timerEventDefinition", "timer"},
      {"errorEventDefinition", "error"},             {"escalationEventDefinition", "escalation"},
      {"cancelEventDefinition", "cancel"},           {"compensateEventDefinition", "compensation"},
      {"conditionalEventDefinition", "conditional"}, {"linkEventDefinition", "link"},
      {"signalEventDefinition", "signal"},           {"terminateEventDefinition", "terminate"}};
  return defs;
}

std::optional<std::string_view> event_marker(std::string_view definition) {
  for (const auto& [k, v] : event_definitions())
    if (k == definition) return v;
  return std::nullopt;
}

std::optional<std::string_view> definition_element(std::string_view marker) {
  for (const auto& [k, v] : event_definitions())
    if (v == marker) return k;
  return std::nullopt;
}

std::string_view local(std::string_view name) {
  auto colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

std::string kind_of(std::string_view type) {
  if (is_event(type)) return "event";
  if (is_task(type) || type == "choreographyTask" || type == "callChoreography") return "task";
  if (is_subprocess(type) || type == "subChoreography") return "subprocess";
  if (is_gateway(type)) return "gateway";
  if (type == "dataStoreReference" || type == "dataStore") return "database";
  if (is_data(type)) return "data";
  if (type == "textAnnotation") return "annotation";
  if (type == "group") return "group";
  if (is_conversation(type)) return "conversation";
  if (type == "participant") return "pool";
  if (type == "lane") return "lane";
  return "task";
}

std::string escape_attr(std::string_view v) {
  std::string out;
  for (char c : v) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\n') out += "&#10;";
    else out.push_back(c);
  }
  return out;
}

std::string escape_text(std::string_view v) {
  std::string out;
  for (char c : v) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out.push_back(c);
  }
  return out;
}

std::string serialize_xml(const XmlNode& node) {
  std::string attrs;
  for (const auto& [k, v] : node.attrs) attrs += " " + k + "=\"" + escape_attr(v) + "\"";
  std::string inner;
  for (const auto& c : node.children) inner += serialize_xml(*c);
  inner += escape_text(js_trim(node.text));
  return inner.empty() ? "<" + node.name + attrs + "/>" : "<" + node.name + attrs + ">" + inner + "</" + node.name + ">";
}

std::string camel_to_snake(std::string_view type) {
  std::string out;
  for (char c : type) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back('_');
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace dd::io::bpmn
