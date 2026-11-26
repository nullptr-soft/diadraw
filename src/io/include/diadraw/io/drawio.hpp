#pragma once

// draw.io / diagrams.net import, a port of packages/import/src/drawio.ts and drawio-shapes.ts.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/io/builder.hpp"
#include "diadraw/layout/types.hpp"

namespace dd::io {

/// The index of draw.io's stencil sets shipped as `drawio-*` libraries
/// (packages/library/drawio/index.json.gz): style name → library and shape.
struct DrawioIndex {
  struct Set {
    std::string name;
    std::string version;
  };
  std::vector<Set> sets;
  /// Lowercase draw.io style name → (index into `sets`, shape id).
  std::map<std::string, std::pair<std::size_t, std::string>, std::less<>> styles;

  static Result<DrawioIndex> from_json(const Json& json);
};

struct DrawioOptions {
  /// Keep the drawing's positions, sizes and routes (a fixed-layout view per page); else lay out.
  bool keep_geometry = true;
  /// Draw stencils with the artwork of the `drawio-*` libraries, when an index is given.
  const DrawioIndex* stencils = nullptr;
};

/// The draw.io XML in a file of any supported form (plain, compressed, `.drawio.svg`,
/// `.drawio.png`), or the reason there is none.
struct DrawioFile {
  std::optional<std::string> xml;
  std::optional<std::string> problem;
};
DrawioFile read_drawio_file(std::string_view bytes);

/// A compressed page: base64 of raw deflate of the URI-encoded XML. nullopt when it is not one.
std::optional<std::string> inflate_page(std::string_view text);

/// HTML labels to text: line breaks and blocks become newlines, tags go, entities decode.
std::string html_to_text(std::string_view html);

/// draw.io's fontStyle bits (bold 1, italic 2, underline 4, strike-through 8) of each line
/// html_to_text gives for `html`: `base` (the cell's fontStyle) with the bits of the <b>/<strong>,
/// <i>/<em>, <u> and <s>/<strike>/<del> elements around all of the line's text. Empty when the
/// lines cannot be matched up.
std::vector<int> html_line_font_styles(std::string_view html, int base);

/// A run of `count` code points of a label in the fontStyle bits `bits`.
struct FontRun {
  std::size_t count = 0;
  int bits = 0;
  friend bool operator==(const FontRun&, const FontRun&) = default;
};

/// The text html_to_text gives for `html` as runs of code points in one fontStyle (a line break is
/// one code point, in the style of the run before it): `base` with the bits of the font elements
/// around each. Empty when the text cannot be matched up.
std::vector<FontRun> html_font_runs(std::string_view html, int base);

struct ShapeInfo {
  std::string base;
  std::string kind;
  /// The draw.io shape name, when there was one.
  std::optional<std::string> name;
  /// The name was not recognised and `base` is a guess.
  bool unknown = false;
  /// A library stencil (icon), drawn as a plain icon box unless artwork is found.
  bool stencil = false;
};

/// Maps a parsed draw.io style (bare names, key=value pairs) to the nearest base shape and kind.
ShapeInfo shape_for(const std::vector<std::string>& names,
                    const std::vector<std::pair<std::string, std::string>>& values);

/// Style keys DiaDraw understands or deliberately passes through; others are reported.
bool is_known_style_key(std::string_view key);

ImportResult import_drawio(std::string_view bytes, const DrawioOptions& options = {});

/// One page to export: a view and its layout.
struct DrawioPage {
  ViewId view;
  OrderedMap<Box> nodes;
  OrderedMap<EdgeRoute> edges;
};

/// Exports views as a draw.io file, one page per view (a port of drawio-export.ts). Elements and
/// relationships that came from draw.io get their original style, label HTML, custom data and
/// cell id back, so a draw.io → DiaDraw → draw.io round trip keeps what DiaDraw does not use.
std::string export_drawio(const Model& model, const std::vector<DrawioPage>& pages);

}  // namespace dd::io
