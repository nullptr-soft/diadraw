#pragma once

// SVG rendering, a port of packages/render/src (index.ts and shapes.ts). The output is byte for
// byte what the TypeScript renderer writes for the same view and geometry.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "diadraw/layout/types.hpp"
#include "diadraw/model/views.hpp"

namespace dd {

struct NodeStyle {
  std::string fill;
  std::string stroke;
  std::string text;
};

struct Theme {
  /// The name `theme …` picks it by.
  std::string name;
  std::string background;
  std::string font_family;
  double font_size = 13;
  double small_font_size = 11;
  std::string edge;
  std::string edge_text;
  std::string boundary;
  std::string boundary_text;
  /// Style per element kind; `default` is the fallback.
  std::map<std::string, NodeStyle, std::less<>> nodes;
  /// Style for elements tagged `external`.
  NodeStyle external;
  /// Fill of hollow arrowheads and sequence activations.
  std::string surface = "#ffffff";
  /// Fill behind a sequence divider's label.
  std::string divider_fill = "#eeeeee";
  /// Labels drawn outside a shape (under library icons).
  std::string label_text = "#1f2937";
};

const Theme& default_theme();
/// A built-in theme by name (`default`, `dark`, `mono`), or null.
const Theme* find_theme(std::string_view name);
/// The built-in themes' names, `default` first.
std::vector<std::string> theme_names();
/// Keys a theme declaration sets (`background`, `edge`, `fontFamily`, …), and keys of its
/// `node kind { … }` blocks (`fill`, `stroke`, `text`).
const std::vector<std::string>& theme_keys();
const std::vector<std::string>& theme_node_keys();

/// Library themes by `alias.name`; hosts resolve them through the libraries a document uses.
using ThemeResolver = std::function<std::optional<ThemeDef>(const std::string&)>;

/// A theme by name: one of `declared`, a library theme (`alias.name`) or a built-in, with its
/// `extends` chain applied. Null when the name (or a theme it extends) is unknown.
std::optional<Theme> resolve_theme(std::string_view name, const std::vector<ThemeDef>& declared,
                                   const ThemeResolver& library = {});
/// The theme a resolved view asks for (its own, else the document's); the default theme when it
/// asks for none or for one that does not resolve.
Theme theme_for(const ResolvedView& view, const ThemeResolver& library = {});

/// A library shape (`shape aws.lambda`) as the renderer draws it: SVG markup in a view box, or a
/// base shape. Hosts resolve references through the libraries a document uses.
struct ShapeArt {
  std::optional<std::string> body;
  std::optional<std::string> view_box;
  std::optional<std::string> base;
  std::optional<double> width;
  std::optional<double> height;
  /// `inside`: the drawing fills the node and the label sits in it (BPMN tasks); default below.
  bool label_inside = false;
  /// Placeholder colours in `body` that take the element's fill and stroke colours.
  std::vector<std::string> fill_colors;
  std::vector<std::string> stroke_colors;
};

using ShapeResolver = std::function<std::optional<ShapeArt>(const std::string&)>;

/// An element kind a library adds (`x = alias.kind`): how its elements look unless they say
/// otherwise. Hosts resolve kinds through the libraries a document uses, like shapes.
struct KindArt {
  /// What its elements draw as when they have no `shape`: a library shape (`alias.id`) or a base
  /// shape (`hexagon`).
  std::optional<std::string> shape;
  /// Colours in place of the theme's default node style; unset ones keep the default.
  std::optional<std::string> fill;
  std::optional<std::string> stroke;
  std::optional<std::string> text;
  /// Shown in brackets on the boundary of an element that has children, as C4 kinds are.
  std::optional<std::string> stereotype;
  std::string label;
};

using KindResolver = std::function<std::optional<KindArt>(const std::string&)>;

/// What a document's libraries give the renderer: shapes, element kinds and themes.
struct LibraryArt {
  LibraryArt() = default;
  LibraryArt(ShapeResolver shapes_in, KindResolver kinds_in = {},  // NOLINT: a shape resolver alone converts
             ThemeResolver themes_in = {})
      : shapes(std::move(shapes_in)), kinds(std::move(kinds_in)), themes(std::move(themes_in)) {}
  ShapeResolver shapes;
  KindResolver kinds;
  ThemeResolver themes;
};

struct RenderOptions {
  const Theme* theme = nullptr;  ///< theme_for(view) when null
  /// Padding around the diagram, in px.
  double padding = 20;
  /// Library shapes; references it does not resolve draw as the element kind's shape.
  ShapeResolver shapes;
  /// Library kinds; kinds it does not resolve draw like unknown kinds (a box in default colours).
  KindResolver kinds;
  /// Library themes (`theme alias.name`).
  ThemeResolver themes;
};

/// A relationship line drawn with `style` straight across a `width` × `height` box, ends and
/// all, as render_svg draws it: a picker's preview of an arrowhead or a dash pattern. The line runs
/// from 4 to `width` − 4, so the box should be wider than 8.
std::string line_sample_svg(const Style& style, double width, double height, const Theme& theme = default_theme());

/// The base shape drawn for a `shape` value and element kind.
std::string base_shape(const std::optional<std::string>& shape, const std::string& kind);
/// The style keys that change a base shape's outline (draw.io's names and defaults). Unset, each
/// shape keeps DiaDraw's own look.
struct ShapeParams {
  /// `size`: a cube's depth, a note's folded corner, a lifeline's head.
  std::optional<double> size;
  /// `direction` of a cube: `east` puts its depth at the top left (draw.io's default), `south` at
  /// the top right, `west` at the bottom right, `north` at the bottom left. Empty: the top right,
  /// as DiaDraw's own cube, which without a size too is 15% of the box deep.
  std::string direction;
  /// A folder's square tab (`tabWidth`, `tabHeight`, `tabPosition` left or right); without
  /// either size the folder has DiaDraw's slanted tab.
  std::optional<double> tab_width, tab_height;
  std::string tab_position;
  /// A module's two jetties on its left edge (`jettyWidth`, `jettyHeight`).
  double jetty_width = 20, jetty_height = 10;
  /// A lifeline's head (`participant`): draw.io's `umlActor` draws a stick figure, anything else a
  /// box. `lifelineDashed=0` draws its line solid.
  std::string participant;
  bool lifeline_dashed = true;
};

/// The shape parameters an inline style (or style block) sets.
ShapeParams shape_params(const Style& style);

/// SVG for a base shape's outline in a box; `paint` holds fill and stroke attributes.
std::string shape_svg(const std::string& shape, const Box& b, const std::string& paint, bool rounded = false,
                      const ShapeParams& params = {});

std::string escape_xml(std::string_view text);
/// Approximate text width without a font: average glyph widths of a proportional sans font.
double text_width(std::string_view text, double font_size);
/// Splits text into lines that fit `width`; explicit newlines always break.
std::vector<std::string> wrap_text(std::string_view content, double width, double font_size);

/// Node and label sizes that match how `render_svg` draws them.
Measure create_measure(const Theme& theme = default_theme(), ShapeResolver shapes = {}, KindResolver kinds = {});

/// An element as a library kind draws it: the kind's shape when the element has none.
Element with_kind(const Element& element, const KindResolver& kinds);

/// The pieces of a rendered diagram, for hosts that wrap it in their own `<svg>`.
struct SvgScene {
  double width = 0;
  double height = 0;
  std::string view_box;
  /// Markers, background and the diagram itself.
  std::string content;
};

SvgScene render_scene(const ResolvedView& view, const Geometry& geometry, const RenderOptions& options = {});
/// A standalone SVG document.
std::string render_svg(const ResolvedView& view, const Geometry& geometry, const RenderOptions& options = {});

}  // namespace dd
