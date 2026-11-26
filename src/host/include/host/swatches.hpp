#pragma once

// The preset colours every colour picker offers (src/host/data/color-swatches.txt), so that
// diagrams keep to one set of colours. The web app reads the same file.

#include <string>
#include <string_view>
#include <vector>

namespace dd::host {

/// One preset colour.
struct Swatch {
  std::string color;  ///< `#rrggbb`, lower case
  std::string name;   ///< `Light blue`: the swatch's tooltip and accessible label
  bool operator==(const Swatch&) const = default;
};

/// The swatches as a picker lays them out, row by row.
using SwatchRows = std::vector<std::vector<Swatch>>;

/// Reads swatches in the file's format: `#rrggbb Name` per line, a blank line between rows,
/// `//` comment lines. Lines that are not a colour and a name are left out, as are empty rows.
SwatchRows parse_swatches(std::string_view text);

/// The built-in swatches, read once from the file compiled into the core.
const SwatchRows& color_swatches();

}  // namespace dd::host
