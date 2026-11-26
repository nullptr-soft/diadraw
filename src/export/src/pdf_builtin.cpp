// The built-in PDF backend: a vector PDF of a rendered diagram, one page the size of the diagram,
// drawn from the display list (render/display_list.hpp). Text uses the standard Helvetica faces
// (WinAnsi; characters outside it become "?"), so no font is embedded.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "diadraw/base/text.hpp"
#include "diadraw/export/export.hpp"
#include "diadraw/render/display_list.hpp"
#include "miniz.h"

namespace dd::exporting {

namespace {

#include "helvetica_metrics.inc"

using namespace dd::draw;

std::string fmt(double v) {
  if (std::fabs(v) < 1e-9) return "0";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", v);
  std::string s(buf);
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s;
}

// ── Text ────────────────────────────────────────────────────────────────────────────────────

/// UTF-8 to WinAnsi bytes; characters outside it become "?".
std::string to_win_ansi(std::string_view text) {
  static const std::map<std::uint32_t, unsigned char> kHigh{
      {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86},
      {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C},
      {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95},
      {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B},
      {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    std::uint32_t cp = next_code_point(text, i);
    if (cp == '\n' || cp == '\t') cp = ' ';
    if ((cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= 0xFF)) out.push_back(static_cast<char>(cp));
    else if (auto it = kHigh.find(cp); it != kHigh.end()) out.push_back(static_cast<char>(it->second));
    else out.push_back('?');
  }
  return out;
}

double text_width(const std::string& win_ansi, bool bold, double size) {
  double w = 0;
  for (char ch : win_ansi) {
    auto c = static_cast<unsigned char>(ch);
    if (c >= 0x20) w += bold ? kHelveticaBold[c - 0x20] : kHelvetica[c - 0x20];
  }
  return w * size / 1000;
}

std::string pdf_string(const std::string& bytes) {
  std::string out = "(";
  for (char ch : bytes) {
    auto c = static_cast<unsigned char>(ch);
    if (c == '(' || c == ')' || c == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    } else if (c < 0x20 || c >= 0x7F) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\%03o", c);
      out += buf;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out + ")";
}

// ── Drawing ─────────────────────────────────────────────────────────────────────────────────

class Writer {
 public:
  std::string content;
  std::map<std::string, std::string> states;  ///< "ca CA" → name

  void draw(const DisplayList& list) {
    // SVG y grows down; PDF y grows up.
    Matrix flip{1, 0, 0, -1, 0, list.height};
    content += "q " + cm(flip * list.root);
    for (const auto& item : list.items) {
      switch (item.kind) {
        case Item::Save: content += "q\n"; break;
        case Item::Restore: content += "Q\n"; break;
        case Item::Transform: content += cm(item.matrix); break;
        case Item::Clip:
          content += fmt(item.rect[0]) + " " + fmt(item.rect[1]) + " " + fmt(item.rect[2]) + " " + fmt(item.rect[3]) +
                     " re W n\n";
          break;
        case Item::Path: path(item); break;
        case Item::Label: label(item.text); break;
      }
    }
    content += "Q\n";
  }

 private:
  static std::string cm(const Matrix& m) {
    return fmt(m.a) + " " + fmt(m.b) + " " + fmt(m.c) + " " + fmt(m.d) + " " + fmt(m.e) + " " + fmt(m.f) + " cm\n";
  }

  std::string state(double fill_alpha, double stroke_alpha) {
    std::string key = fmt(fill_alpha) + " " + fmt(stroke_alpha);
    auto it = states.find(key);
    if (it == states.end()) it = states.emplace(key, "GS" + std::to_string(states.size() + 1)).first;
    return "/" + it->second + " gs\n";
  }

  void path(const Item& item) {
    const Paint& p = item.paint;
    content += "q\n";
    if (p.fill) content += fmt(p.fill->r) + " " + fmt(p.fill->g) + " " + fmt(p.fill->b) + " rg\n";
    if (p.stroke) {
      content += fmt(p.stroke->r) + " " + fmt(p.stroke->g) + " " + fmt(p.stroke->b) + " RG\n";
      content += fmt(p.stroke_width) + " w\n";
      if (!p.dash.empty()) {
        content += "[";
        for (std::size_t i = 0; i < p.dash.size(); ++i) content += (i ? " " : "") + fmt(p.dash[i]);
        content += "] 0 d\n";
      }
      if (p.cap == LineCap::Round) content += "1 J\n";
      else if (p.cap == LineCap::Square) content += "2 J\n";
      if (p.join == LineJoin::Round) content += "1 j\n";
      else if (p.join == LineJoin::Bevel) content += "2 j\n";
      if (p.miter != 4) content += fmt(std::max(1.0, p.miter)) + " M\n";
    }
    if (p.fill_alpha < 1 || p.stroke_alpha < 1) content += state(p.fill_alpha, p.stroke_alpha);
    for (const auto& seg : item.path) {
      switch (seg.op) {
        case Segment::Move: content += fmt(seg.p[0].x) + " " + fmt(seg.p[0].y) + " m\n"; break;
        case Segment::Line: content += fmt(seg.p[0].x) + " " + fmt(seg.p[0].y) + " l\n"; break;
        case Segment::Cubic:
          content += fmt(seg.p[0].x) + " " + fmt(seg.p[0].y) + " " + fmt(seg.p[1].x) + " " + fmt(seg.p[1].y) + " " +
                     fmt(seg.p[2].x) + " " + fmt(seg.p[2].y) + " c\n";
          break;
        case Segment::Close: content += "h\n"; break;
      }
    }
    const char* op = p.fill && p.stroke ? (p.even_odd ? "B*" : "B") : p.fill ? (p.even_odd ? "f*" : "f") : "S";
    content += std::string(op) + "\nQ\n";
  }

  void label(const Text& t) {
    // Each run in its Helvetica face (oblique for italic), laid out left to right.
    struct Run {
      std::string bytes;
      bool bold, italic, underline, strike;
      double width;
    };
    std::vector<Run> runs;
    double width = 0;
    for_each_span(t, [&](std::string_view run, bool bold, bool italic, bool underline, bool strike) {
      std::string bytes = to_win_ansi(run);
      if (bytes.empty()) return;
      const double w = text_width(bytes, bold, t.font_size);
      width += w;
      runs.push_back({std::move(bytes), bold, italic, underline, strike, w});
    });
    if (runs.empty()) return;
    double x = t.x, y = t.y;
    if (t.anchor == "middle") x -= width / 2;
    else if (t.anchor == "end") x -= width;
    // Helvetica: ascender 718, descender −207 per 1000 em.
    if (t.baseline == "central" || t.baseline == "middle") y += (718.0 - 207.0) / 2000 * t.font_size;
    else if (t.baseline == "hanging" || t.baseline == "text-before-edge") y += 0.718 * t.font_size;
    else if (t.baseline == "text-after-edge" || t.baseline == "ideographic") y -= 0.207 * t.font_size;
    content += "q\n" + fmt(t.color.r) + " " + fmt(t.color.g) + " " + fmt(t.color.b) + " rg\n";
    if (t.alpha < 1) content += state(t.alpha, 1);
    // Helvetica's underline: 100 below the baseline, 50 thick per 1000 em; a strike-through
    // goes through the middle of its x-height (523).
    const double thickness = 0.05 * t.font_size;
    for (const auto& r : runs) {
      const char* face = r.bold ? (r.italic ? "F4 " : "F2 ") : (r.italic ? "F3 " : "F1 ");
      content += "BT /" + std::string(face) + fmt(t.font_size) + " Tf 1 0 0 -1 " + fmt(x) + " " + fmt(y) + " Tm " +
                 pdf_string(r.bytes) + " Tj ET\n";
      if (r.underline)
        content += fmt(x) + " " + fmt(y + 0.1 * t.font_size - thickness / 2) + " " + fmt(r.width) + " " +
                   fmt(thickness) + " re f\n";
      if (r.strike)
        content += fmt(x) + " " + fmt(y - 0.2615 * t.font_size - thickness / 2) + " " + fmt(r.width) + " " +
                   fmt(thickness) + " re f\n";
      x += r.width;
    }
    content += "Q\n";
  }
};

std::string deflate_zlib(std::string_view bytes) {
  std::size_t out_len = 0;
  void* out = tdefl_compress_mem_to_heap(bytes.data(), bytes.size(), &out_len,
                                         static_cast<int>(TDEFL_WRITE_ZLIB_HEADER) | static_cast<int>(TDEFL_DEFAULT_MAX_PROBES));
  if (!out) return std::string(bytes);
  std::string result(static_cast<const char*>(out), out_len);
  std::free(out);
  return result;
}

class BuiltinPdfWriter final : public PdfWriter {
 public:
  [[nodiscard]] std::string name() const override { return "builtin"; }

  [[nodiscard]] Result<std::string> pdf(std::string_view svg) const override {
    auto size = svg_size(svg);
    if (!size) return size.error();
    auto list = display_list_from_svg(svg);
    if (!list) return fail("No <svg> element");
    list->width = size->width;
    list->height = size->height;
    Writer w;
    w.draw(*list);
    std::string stream = deflate_zlib(w.content);

    std::vector<std::string> objects;
    objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.emplace_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    std::string gs;
    int next = 9;
    std::vector<std::string> gs_objects;
    for (const auto& [key, name] : w.states) {
      auto sp = key.find(' ');
      gs += "/" + name + " " + std::to_string(next++) + " 0 R ";
      gs_objects.push_back("<< /Type /ExtGState /ca " + key.substr(0, sp) + " /CA " + key.substr(sp + 1) + " >>");
    }
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + fmt(size->width) + " " + fmt(size->height) +
                      "] /Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R /F4 8 0 R >>" + (gs.empty() ? "" : " /ExtGState << " + gs + ">>") +
                      " >> /Contents 4 0 R >>");
    objects.push_back("<< /Length " + std::to_string(stream.size()) + " /Filter /FlateDecode >>\nstream\n" + stream +
                      "\nendstream");
    objects.emplace_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.emplace_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    objects.emplace_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique /Encoding /WinAnsiEncoding >>");
    objects.emplace_back(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-BoldOblique /Encoding /WinAnsiEncoding >>");
    for (auto& g : gs_objects) objects.push_back(std::move(g));
    objects.emplace_back("<< /Title (DiaDraw diagram) /Creator (DiaDraw) /Producer (DiaDraw) >>");

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
      offsets.push_back(out.size());
      out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (auto o : offsets) {
      char buf[24];
      std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", o);
      out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info " +
           std::to_string(objects.size()) + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return out;
  }
};

}  // namespace

std::unique_ptr<PdfWriter> make_pdf_writer() { return std::make_unique<BuiltinPdfWriter>(); }

}  // namespace dd::exporting
