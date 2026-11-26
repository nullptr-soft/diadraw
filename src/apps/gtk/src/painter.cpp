#include "painter.hpp"

#include <pango/pangocairo.h>

#include <string>
#include <string_view>

namespace dd::gtk {

namespace {

using namespace dd::draw;

void set_matrix(cairo_t* cr, const Matrix& m) {
  cairo_matrix_t c;
  cairo_matrix_init(&c, m.a, m.b, m.c, m.d, m.e, m.f);
  cairo_transform(cr, &c);
}

void add_path(cairo_t* cr, const std::vector<Segment>& path) {
  cairo_new_path(cr);
  for (const auto& s : path) {
    switch (s.op) {
      case Segment::Move: cairo_move_to(cr, s.p[0].x, s.p[0].y); break;
      case Segment::Line: cairo_line_to(cr, s.p[0].x, s.p[0].y); break;
      case Segment::Cubic: cairo_curve_to(cr, s.p[0].x, s.p[0].y, s.p[1].x, s.p[1].y, s.p[2].x, s.p[2].y); break;
      case Segment::Close: cairo_close_path(cr); break;
    }
  }
}

void draw_path(cairo_t* cr, const Item& item) {
  const Paint& p = item.paint;
  cairo_save(cr);
  add_path(cr, item.path);
  if (p.fill) {
    cairo_set_source_rgba(cr, p.fill->r, p.fill->g, p.fill->b, p.fill_alpha);
    cairo_set_fill_rule(cr, p.even_odd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
    if (p.stroke) cairo_fill_preserve(cr);
    else cairo_fill(cr);
  }
  if (p.stroke) {
    cairo_set_source_rgba(cr, p.stroke->r, p.stroke->g, p.stroke->b, p.stroke_alpha);
    cairo_set_line_width(cr, p.stroke_width);
    cairo_set_line_cap(cr, p.cap == LineCap::Round    ? CAIRO_LINE_CAP_ROUND
                           : p.cap == LineCap::Square ? CAIRO_LINE_CAP_SQUARE
                                                      : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(cr, p.join == LineJoin::Round   ? CAIRO_LINE_JOIN_ROUND
                            : p.join == LineJoin::Bevel ? CAIRO_LINE_JOIN_BEVEL
                                                        : CAIRO_LINE_JOIN_MITER);
    cairo_set_miter_limit(cr, p.miter);
    if (!p.dash.empty()) cairo_set_dash(cr, p.dash.data(), static_cast<int>(p.dash.size()), 0);
    cairo_stroke(cr);
  }
  cairo_new_path(cr);
  cairo_restore(cr);
}

/// A CSS font-family list as Pango reads it: generic names mapped, quotes dropped.
std::string pango_families(const std::string& css) {
  std::string out;
  std::size_t start = 0;
  while (start <= css.size()) {
    std::size_t comma = css.find(',', start);
    if (comma == std::string::npos) comma = css.size();
    std::string f = css.substr(start, comma - start);
    auto strip = [](std::string& s, const char* chars) {
      s.erase(0, s.find_first_not_of(chars));
      s.erase(s.find_last_not_of(chars) + 1);
    };
    strip(f, " \t");
    strip(f, "\"'");
    if (f == "system-ui" || f == "-apple-system" || f == "sans-serif" || f == "BlinkMacSystemFont") f = "Sans";
    else if (f == "monospace" || f == "ui-monospace") f = "Monospace";
    else if (f == "serif") f = "Serif";
    if (!f.empty()) out += (out.empty() ? "" : ",") + f;
    start = comma + 1;
  }
  return out.empty() ? "Sans" : out;
}

void draw_text(cairo_t* cr, const Text& t) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* font = pango_font_description_new();
  pango_font_description_set_family(font, pango_families(t.font_family).c_str());
  pango_font_description_set_absolute_size(font, t.font_size * PANGO_SCALE);
  pango_font_description_set_weight(font, t.bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, font);
  pango_layout_set_text(layout, t.content.c_str(), static_cast<int>(t.content.size()));
  // Each run's slant and lines, and its weight where it is not the label's, by its byte range in
  // the text (the runs join up to it).
  if (!t.spans.empty() || t.italic || t.underline || t.strike) {
    PangoAttrList* attrs = pango_attr_list_new();
    guint start = 0;
    for_each_span(t, [&](std::string_view run, bool bold, bool italic, bool underline, bool strike) {
      const auto end = static_cast<guint>(start + run.size());
      auto add = [&](PangoAttribute* a) {
        a->start_index = start;
        a->end_index = end;
        pango_attr_list_insert(attrs, a);
      };
      if (bold != t.bold) add(pango_attr_weight_new(bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL));
      if (italic) add(pango_attr_style_new(PANGO_STYLE_ITALIC));
      if (underline) add(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
      if (strike) add(pango_attr_strikethrough_new(TRUE));
      start = end;
    });
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
  }
  PangoRectangle logical;
  pango_layout_get_extents(layout, nullptr, &logical);
  double width = logical.width / double(PANGO_SCALE);
  double ascent = pango_layout_get_baseline(layout) / double(PANGO_SCALE);
  double descent = logical.height / double(PANGO_SCALE) - ascent;
  double x = t.x, baseline = t.y;
  if (t.anchor == "middle") x -= width / 2;
  else if (t.anchor == "end") x -= width;
  if (t.baseline == "central" || t.baseline == "middle") baseline += (ascent - descent) / 2;
  else if (t.baseline == "hanging" || t.baseline == "text-before-edge") baseline += ascent;
  else if (t.baseline == "text-after-edge" || t.baseline == "ideographic") baseline -= descent;
  cairo_save(cr);
  cairo_set_source_rgba(cr, t.color.r, t.color.g, t.color.b, t.alpha);
  cairo_move_to(cr, x, baseline - ascent);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(font);
  g_object_unref(layout);
}

}  // namespace

void paint(cairo_t* cr, const DisplayList& list) {
  cairo_save(cr);
  set_matrix(cr, list.root);
  int depth = 0;
  for (const auto& item : list.items) {
    switch (item.kind) {
      case Item::Save:
        cairo_save(cr);
        ++depth;
        break;
      case Item::Restore:
        if (depth > 0) {
          cairo_restore(cr);
          --depth;
        }
        break;
      case Item::Transform: set_matrix(cr, item.matrix); break;
      case Item::Clip:
        cairo_rectangle(cr, item.rect[0], item.rect[1], item.rect[2], item.rect[3]);
        cairo_clip(cr);
        break;
      case Item::Path: draw_path(cr, item); break;
      case Item::Label: draw_text(cr, item.text); break;
    }
  }
  while (depth-- > 0) cairo_restore(cr);
  cairo_restore(cr);
}

}  // namespace dd::gtk
