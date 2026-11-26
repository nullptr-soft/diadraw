#pragma once

// draw.io stencils to SVG: a port of packages/import/src/drawio-stencil.ts. It reads draw.io's
// stencil sources (a set's `.xml`, or `stencils.min.js` where every set is stored compressed) and
// draws each stencil as SVG markup, for `diadraw lib build --drawio`.

#include <string>
#include <string_view>
#include <vector>

namespace dd::io {

/// A draw.io stencil drawn as SVG markup in its own coordinates (`0 0 width height`).
struct StencilSvg {
  std::string style;  ///< the `shape=` name draw.io styles use: `mxgraph.<set>.<name>`, lowercase
  std::string id;     ///< an identifier for the shape within its set
  std::string label;
  double width = 0, height = 0;
  std::string body;
};

struct StencilSet {
  std::string name;  ///< `mxgraph.aws4`, from the `<shapes name>` attribute
  std::string file;  ///< the file the set came from in draw.io's sources, e.g. `aws4.xml`
  std::vector<StencilSvg> shapes;
  struct Skipped {
    std::string name, reason;
  };
  std::vector<Skipped> skipped;
};

/// Placeholder colours for what draw.io takes from the cell's style (see the TypeScript port).
inline constexpr std::string_view kStencilFill = "#fffffe";
inline constexpr std::string_view kStencilStroke = "#000001";
inline constexpr std::string_view kGlyphFill = "#374152";

/// Converts a stencil set (`<shapes name="mxgraph.x">…</shapes>`).
StencilSet convert_stencil_set(std::string_view xml, std::string_view file);

/// Reads draw.io's stencil sources: a plain set `.xml`, or `stencils.min.js`.
std::vector<StencilSet> read_stencil_sets(std::string_view text, std::string_view file = "stencils.xml");

}  // namespace dd::io
