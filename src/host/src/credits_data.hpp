#pragma once

// The text of data/credits.tsv, which CMake writes into credits_data.cpp (from
// credits_data.cpp.in). Not part of the host's interface.

#include <string_view>

namespace dd::host::detail {
extern const std::string_view credits_text;
}  // namespace dd::host::detail
