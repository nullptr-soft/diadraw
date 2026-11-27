#pragma once

// The text of data/canvas-help.txt, which CMake writes into canvas_help_data.cpp (from
// canvas_help_data.cpp.in). Not part of the host's interface.

#include <string_view>

namespace dd::host::detail {
extern const std::string_view canvas_help_text;
}  // namespace dd::host::detail
