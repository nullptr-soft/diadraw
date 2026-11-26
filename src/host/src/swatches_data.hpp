#pragma once

// The text of data/color-swatches.txt, which CMake writes into swatches_data.cpp (from
// swatches_data.cpp.in). Not part of the host's interface.

#include <string_view>

namespace dd::host::detail {
extern const std::string_view swatches_text;
}  // namespace dd::host::detail
