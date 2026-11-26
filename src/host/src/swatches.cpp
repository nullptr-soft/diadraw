#include "host/swatches.hpp"

#include <utility>

#include "ascii.hpp"
#include "host/field_editors.hpp"
#include "swatches_data.hpp"

namespace dd::host {

using detail::trim_ascii;

SwatchRows parse_swatches(std::string_view text) {
  SwatchRows rows(1);
  while (!text.empty()) {
    auto end = text.find('\n');
    std::string_view line = trim_ascii(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    if (line.empty()) {
      if (!rows.back().empty()) rows.emplace_back();
      continue;
    }
    if (line.starts_with("//")) continue;
    auto space = line.find_first_of(" \t");
    if (space == std::string_view::npos) continue;
    auto color = normalized_color(line.substr(0, space));
    std::string_view name = trim_ascii(line.substr(space));
    if (!color || name.empty()) continue;
    rows.back().push_back({std::move(*color), std::string(name)});
  }
  if (rows.back().empty()) rows.pop_back();
  return rows;
}

const SwatchRows& color_swatches() {
  static const SwatchRows rows = parse_swatches(detail::swatches_text);
  return rows;
}

}  // namespace dd::host
