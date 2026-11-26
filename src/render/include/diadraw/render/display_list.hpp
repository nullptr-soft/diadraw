#pragma once

// A display list: the rendered diagram as drawing commands, for hosts that paint natively (the
// AppKit, WinUI and gtkmm shells through Core Graphics, Direct2D or Cairo) and for the PDF writer.
// It is read from the SVG the renderer writes, so every output draws the same picture.
//
// Coordinates are SVG user units, y down. Each item carries the id of the diagram item it belongs
// to (element, relationship or group, from the renderer's `data-id`), for hit-testing.
//
// Text is not laid out here: hosts measure it with their own fonts and place it by its anchor and
// baseline, so a host's typeface never ends up clipped or misplaced.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dd::draw {

struct Point {
  double x = 0, y = 0;
};

struct Rgb {
  double r = 0, g = 0, b = 0;
};

/// An affine transform `[a c e; b d f]`, as SVG and PDF write it.
struct Matrix {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  /// this · m (m applied first).
  [[nodiscard]] Matrix operator*(const Matrix& m) const {
    return {a * m.a + c * m.b, b * m.a + d * m.b, a * m.c + c * m.d, b * m.c + d * m.d, a * m.e + c * m.f + e,
            b * m.e + d * m.f + f};
  }
  [[nodiscard]] bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
  [[nodiscard]] Point apply(Point p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  static Matrix translate(double x, double y) { return {1, 0, 0, 1, x, y}; }
  static Matrix scale(double x, double y) { return {x, 0, 0, y, 0, 0}; }
  static Matrix rotate(double degrees);
};

/// A path segment in absolute coordinates; arcs and quadratics are already cubic Béziers.
struct Segment {
  enum Op { Move, Line, Cubic, Close } op = Move;
  Point p[3];
};

enum class LineCap { Butt, Round, Square };
enum class LineJoin { Miter, Round, Bevel };

struct Paint {
  std::optional<Rgb> fill;
  std::optional<Rgb> stroke;
  double stroke_width = 1;
  /// Opacities already multiplied by the element's `opacity`.
  double fill_alpha = 1;
  double stroke_alpha = 1;
  std::vector<double> dash;
  LineCap cap = LineCap::Butt;
  LineJoin join = LineJoin::Miter;
  double miter = 4;
  bool even_odd = false;
};

/// A run of a label in its own font style: part of Text::content, in order.
struct TextSpan {
  /// UTF-8.
  std::string content;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strike = false;  ///< struck through (SVG text-decoration line-through)
};

struct Text {
  /// UTF-8, one line.
  std::string content;
  /// The anchor point: `x` is where `anchor` sits, `y` where `baseline` sits.
  double x = 0, y = 0;
  std::string anchor = "start";    ///< start, middle or end
  std::string baseline;            ///< SVG dominant-baseline: "" (alphabetic), central, middle, hanging…
  std::string font_family;
  double font_size = 16;
  bool bold = false;
  Rgb color;
  double alpha = 1;
  bool italic = false;
  bool underline = false;
  bool strike = false;
  /// The runs of `content` when they differ in font style (each run's own bold, italic,
  /// underline and strike-through stand for the whole label's); empty when the label has one.
  std::vector<TextSpan> spans{};
};

/// Calls `f(content, bold, italic, underline, strike)` for each run to draw `t` as, in order: its
/// spans, or all of it in its own font style. The runs' contents join up to `t.content`.
template <class F>
void for_each_span(const Text& t, F&& f) {
  if (t.spans.empty()) {
    f(std::string_view(t.content), t.bold, t.italic, t.underline, t.strike);
    return;
  }
  for (const auto& s : t.spans) f(std::string_view(s.content), s.bold, s.italic, s.underline, s.strike);
}

struct Item {
  enum Kind {
    Save,       ///< push the graphics state (transform, clip)
    Restore,    ///< pop it
    Transform,  ///< concatenate `matrix`
    Clip,       ///< intersect the clip with `rect` (x, y, width, height)
    Path,       ///< paint `path` with `paint`
    Label,      ///< draw `text`
  } kind = Save;
  Matrix matrix;
  double rect[4] = {0, 0, 0, 0};
  std::vector<Segment> path;
  Paint paint;
  Text text;
  /// The diagram item this belongs to (`data-id`), and what it is: `node`, `edge` or `group`.
  std::string owner;
  std::string owner_kind;
};

struct DisplayList {
  /// The page: `width` × `height`, y down.
  double width = 0, height = 0;
  /// From user space (where items are) to the page: the root view box.
  Matrix root;
  std::vector<Item> items;
};

/// Reads the SVG the renderer writes (and the library art inside it) into a display list. Shapes,
/// all path commands, nested <svg> with view boxes, transforms, colours and opacities, dashes,
/// joins and caps, end markers and text are kept; gradients, filters and images are not.
std::optional<DisplayList> display_list_from_svg(std::string_view svg);

/// Parses SVG path data into absolute segments (arcs and quadratics as cubics).
std::vector<Segment> parse_path_data(std::string_view d);

}  // namespace dd::draw
