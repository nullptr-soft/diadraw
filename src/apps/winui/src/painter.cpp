#include "painter.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "diadraw/base/text.hpp"

namespace dd::winui {

namespace {

using namespace dd::draw;
namespace canvas = winrt::Microsoft::Graphics::Canvas;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3x2;

float3x2 to_matrix(const Matrix& m) {
  return float3x2(float(m.a), float(m.b), float(m.c), float(m.d), float(m.e), float(m.f));
}

winrt::Windows::UI::Color colour(const Rgb& c, double alpha) {
  auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
  return {byte(alpha), byte(c.r), byte(c.g), byte(c.b)};
}

float2 pt(const Point& p) { return {float(p.x), float(p.y)}; }

canvas::Geometry::CanvasGeometry geometry(canvas::CanvasDrawingSession const& ds, const std::vector<Segment>& path,
                                          bool even_odd) {
  canvas::Geometry::CanvasPathBuilder builder(ds);
  if (even_odd) builder.SetFilledRegionDetermination(canvas::Geometry::CanvasFilledRegionDetermination::Alternate);
  bool open = false;
  Point start, current;
  // SVG continues after a close from the figure's start; Direct2D needs a new figure there.
  auto ensure_open = [&] {
    if (!open) {
      builder.BeginFigure(pt(current));
      open = true;
    }
  };
  for (const auto& s : path) {
    switch (s.op) {
      case Segment::Move:
        if (open) builder.EndFigure(canvas::Geometry::CanvasFigureLoop::Open);
        start = current = s.p[0];
        builder.BeginFigure(pt(start));
        open = true;
        break;
      case Segment::Line:
        ensure_open();
        builder.AddLine(pt(s.p[0]));
        current = s.p[0];
        break;
      case Segment::Cubic:
        ensure_open();
        builder.AddCubicBezier(pt(s.p[0]), pt(s.p[1]), pt(s.p[2]));
        current = s.p[2];
        break;
      case Segment::Close:
        if (open) builder.EndFigure(canvas::Geometry::CanvasFigureLoop::Closed);
        open = false;
        current = start;
        break;
    }
  }
  if (open) builder.EndFigure(canvas::Geometry::CanvasFigureLoop::Open);
  return canvas::Geometry::CanvasGeometry::CreatePath(builder);
}

void draw_path(canvas::CanvasDrawingSession const& ds, const Item& item) {
  if (item.path.empty()) return;
  const Paint& p = item.paint;
  auto geom = geometry(ds, item.path, p.even_odd);
  if (p.fill) ds.FillGeometry(geom, colour(*p.fill, p.fill_alpha));
  if (p.stroke && p.stroke_width > 0) {
    canvas::Geometry::CanvasStrokeStyle style;
    auto cap = p.cap == LineCap::Round    ? canvas::Geometry::CanvasCapStyle::Round
               : p.cap == LineCap::Square ? canvas::Geometry::CanvasCapStyle::Square
                                          : canvas::Geometry::CanvasCapStyle::Flat;
    style.StartCap(cap);
    style.EndCap(cap);
    style.DashCap(cap);
    style.LineJoin(p.join == LineJoin::Round   ? canvas::Geometry::CanvasLineJoin::Round
                   : p.join == LineJoin::Bevel ? canvas::Geometry::CanvasLineJoin::Bevel
                                               : canvas::Geometry::CanvasLineJoin::Miter);
    style.MiterLimit(float(p.miter));
    if (!p.dash.empty()) {
      // Win2D measures dashes in stroke widths; SVG in user units.
      std::vector<float> dashes;
      for (double d : p.dash) dashes.push_back(float(d / p.stroke_width));
      style.CustomDashStyle(dashes);
    }
    ds.DrawGeometry(geom, colour(*p.stroke, p.stroke_alpha), float(p.stroke_width), style);
  }
}

/// The first concrete family of a CSS font-family list; generic names become Windows fonts.
winrt::hstring family(const std::string& css) {
  std::size_t start = 0;
  while (start <= css.size()) {
    std::size_t comma = css.find(',', start);
    if (comma == std::string::npos) comma = css.size();
    std::string f = css.substr(start, comma - start);
    f.erase(0, f.find_first_not_of(" \t\"'"));
    f.erase(f.find_last_not_of(" \t\"'") + 1);
    if (f == "system-ui" || f == "-apple-system" || f == "sans-serif" || f == "BlinkMacSystemFont") return L"Segoe UI";
    if (f == "monospace" || f == "ui-monospace") return L"Consolas";
    if (f == "serif") return L"Times New Roman";
    if (!f.empty()) return wide(f);
    start = comma + 1;
  }
  return L"Segoe UI";
}

void draw_text(canvas::CanvasDrawingSession const& ds, const Text& t) {
  canvas::Text::CanvasTextFormat format;
  format.FontFamily(family(t.font_family));
  format.FontSize(float(t.font_size));
  format.FontWeight(t.bold ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                           : winrt::Microsoft::UI::Text::FontWeights::Normal());
  format.WordWrapping(canvas::Text::CanvasWordWrapping::NoWrap);
  canvas::Text::CanvasTextLayout layout(ds, wide(t.content), format, 0.0f, 0.0f);
  // Each run's weight, slant and lines, by its range of UTF-16 code units; a label in one style
  // sets only what the format does not.
  int32_t at = 0;
  for_each_span(t, [&](std::string_view run, bool bold, bool italic, bool underline, bool strike) {
    const auto count = static_cast<int32_t>(dd::utf16_length(run));
    if (bold != t.bold)
      layout.SetFontWeight(at, count,
                           bold ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                                : winrt::Microsoft::UI::Text::FontWeights::Normal());
    if (italic) layout.SetFontStyle(at, count, winrt::Windows::UI::Text::FontStyle::Italic);
    if (underline) layout.SetUnderline(at, count, true);
    if (strike) layout.SetStrikethrough(at, count, true);
    at += count;
  });
  auto lines = layout.LineMetrics();
  if (lines.size() == 0) return;
  double width = layout.LayoutBounds().Width;
  double ascent = lines[0].Baseline;
  double descent = lines[0].Height - lines[0].Baseline;
  double x = t.x, baseline = t.y;
  if (t.anchor == "middle") x -= width / 2;
  else if (t.anchor == "end") x -= width;
  if (t.baseline == "central" || t.baseline == "middle") baseline += (ascent - descent) / 2;
  else if (t.baseline == "hanging" || t.baseline == "text-before-edge") baseline += ascent;
  else if (t.baseline == "text-after-edge" || t.baseline == "ideographic") baseline -= descent;
  ds.DrawTextLayout(layout, float(x), float(baseline - ascent), colour(t.color, t.alpha));
}

}  // namespace

void paint(canvas::CanvasDrawingSession const& ds, const DisplayList& list) {
  struct Saved {
    float3x2 transform;
    std::size_t layers;
  };
  float3x2 outer = ds.Transform();
  std::vector<Saved> stack;
  std::vector<canvas::CanvasActiveLayer> layers;
  // Row-vector matrices: the item's transform applies first, then what is already set.
  ds.Transform(to_matrix(list.root) * outer);
  for (const auto& item : list.items) {
    switch (item.kind) {
      case Item::Save: stack.push_back({ds.Transform(), layers.size()}); break;
      case Item::Restore:
        if (!stack.empty()) {
          while (layers.size() > stack.back().layers) {
            layers.back().Close();
            layers.pop_back();
          }
          ds.Transform(stack.back().transform);
          stack.pop_back();
        }
        break;
      case Item::Transform: ds.Transform(to_matrix(item.matrix) * ds.Transform()); break;
      case Item::Clip:
        layers.push_back(ds.CreateLayer(1.0f, winrt::Windows::Foundation::Rect{float(item.rect[0]), float(item.rect[1]),
                                                                                float(item.rect[2]), float(item.rect[3])}));
        break;
      case Item::Path: draw_path(ds, item); break;
      case Item::Label: draw_text(ds, item.text); break;
    }
  }
  while (!layers.empty()) {
    layers.back().Close();
    layers.pop_back();
  }
  ds.Transform(outer);
}

}  // namespace dd::winui
