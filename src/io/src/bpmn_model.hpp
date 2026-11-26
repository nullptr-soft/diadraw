#pragma once

// Shared knowledge of BPMN 2.0 element types, for import and export (bpmn/model.ts).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "diadraw/io/bpmn.hpp"

namespace dd::io::bpmn {

inline constexpr const char* kBpmnNs = "http://www.omg.org/spec/BPMN/20100524/MODEL";
inline constexpr const char* kBpmndiNs = "http://www.omg.org/spec/BPMN/20100524/DI";
inline constexpr const char* kDcNs = "http://www.omg.org/spec/DD/20100524/DC";
inline constexpr const char* kDiNs = "http://www.omg.org/spec/DD/20100524/DI";

bool is_event(std::string_view t);
bool is_task(std::string_view t);
bool is_subprocess(std::string_view t);
bool is_gateway(std::string_view t);
bool is_choreography(std::string_view t);
bool is_conversation(std::string_view t);
bool is_data(std::string_view t);
bool is_artifact(std::string_view t);
bool is_flow(std::string_view t);

/// Event definition element → marker name in the `bpmn` library, in declaration order.
const std::vector<std::pair<std::string_view, std::string_view>>& event_definitions();
std::optional<std::string_view> event_marker(std::string_view definition);
std::optional<std::string_view> definition_element(std::string_view marker);

/// `userTask` → `user_task`.
std::string camel_to_snake(std::string_view type);

}  // namespace dd::io::bpmn
