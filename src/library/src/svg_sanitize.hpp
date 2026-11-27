#pragma once

// Internal to the library module: the SVG sanitizer under `sanitize_svg` and
// `sanitize_svg_body` (see svg_sanitize.cpp for the rules).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dd::library {

using SvgAttributes = std::vector<std::pair<std::string, std::string>>;

struct SanitizedSvg {
  /// The outer `<svg>` element's attributes as written (decoded, not checked): the caller reads
  /// its size from them and does not copy them into the drawing.
  SvgAttributes root_attributes;
  /// The safe inner markup, ids renamed with the prefix.
  std::string body;
};

/// Nullopt when the markup has no `<svg>` element.
std::optional<SanitizedSvg> sanitize_svg_markup(std::string_view svg, std::string_view id_prefix);

}  // namespace dd::library
