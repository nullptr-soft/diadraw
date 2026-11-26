// Reads the renderer's SVG into a display list (see the header). The PDF writer and the native
// shells draw from it.

#include "diadraw/render/display_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <numbers>
#include <sstream>

#include "diadraw/base/text.hpp"
#include "diadraw/io/xml.hpp"

namespace dd::draw {

Matrix Matrix::rotate(double degrees) {
  double r = degrees * std::numbers::pi / 180;
  return {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
}

namespace {

using io::XmlNode;

// ── Numbers, colours, transforms ────────────────────────────────────────────────────────────

double parse_number(std::string_view s, double fallback = 0) {
  std::string t(js_trim(s));
  if (t.empty()) return fallback;
  char* end = nullptr;
  double v = std::strtod(t.c_str(), &end);
  return end == t.c_str() ? fallback : v;
}

std::string fmt(double v) {
  if (std::fabs(v) < 1e-9) return "0";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", v);
  std::string s(buf);
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s;
}


std::optional<Rgb> parse_color(std::string_view text, const Rgb& current) {
  std::string c(js_trim(text));
  for (auto& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (c.empty() || c == "none" || c == "transparent") return std::nullopt;
  if (c == "currentcolor") return current;
  auto hex = [&](std::size_t i, std::size_t n) { return std::stoi(c.substr(i, n), nullptr, 16); };
  try {
    if (c[0] == '#' && c.size() == 4) return Rgb{hex(1, 1) * 17 / 255.0, hex(2, 1) * 17 / 255.0, hex(3, 1) * 17 / 255.0};
    if (c[0] == '#' && c.size() >= 7) return Rgb{hex(1, 2) / 255.0, hex(3, 2) / 255.0, hex(5, 2) / 255.0};
  } catch (...) {
    return Rgb{};
  }
  if (c.rfind("rgb", 0) == 0) {
    auto open = c.find('('), close = c.find(')');
    std::vector<double> parts;
    std::string inner = c.substr(open + 1, close - open - 1);
    for (char& ch : inner)
      if (ch == ',' || ch == '/') ch = ' ';
    std::istringstream in(inner);
    std::string p;
    while (in >> p) {
      double v = parse_number(p);
      parts.push_back(p.back() == '%' ? v * 2.55 : v);
    }
    if (parts.size() >= 3) return Rgb{parts[0] / 255, parts[1] / 255, parts[2] / 255};
  }
  static const std::map<std::string, Rgb, std::less<>> kNamed{
      {"black", {0, 0, 0}},           {"white", {1, 1, 1}},          {"red", {1, 0, 0}},
      {"green", {0, 128 / 255.0, 0}}, {"blue", {0, 0, 1}},           {"gray", {128 / 255.0, 128 / 255.0, 128 / 255.0}},
      {"grey", {128 / 255.0, 128 / 255.0, 128 / 255.0}},             {"yellow", {1, 1, 0}},
      {"orange", {1, 165 / 255.0, 0}}, {"purple", {128 / 255.0, 0, 128 / 255.0}},
      {"silver", {192 / 255.0, 192 / 255.0, 192 / 255.0}},           {"lightgray", {211 / 255.0, 211 / 255.0, 211 / 255.0}},
      {"darkgray", {169 / 255.0, 169 / 255.0, 169 / 255.0}},         {"navy", {0, 0, 128 / 255.0}}};
  if (auto it = kNamed.find(c); it != kNamed.end()) return it->second;
  return Rgb{};
}

using Mat = Matrix;

std::vector<double> numbers_in(std::string_view s) {
  std::vector<double> out;
  std::size_t i = 0;
  while (i < s.size()) {
    char ch = s[i];
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
      char* end = nullptr;
      std::string tmp(s.substr(i, 64));
      double v = std::strtod(tmp.c_str(), &end);
      std::size_t used = static_cast<std::size_t>(end - tmp.c_str());
      if (used == 0) {
        ++i;
        continue;
      }
      out.push_back(v);
      i += used;
    } else {
      ++i;
    }
  }
  return out;
}

Mat parse_transform(std::string_view text) {
  Mat m;
  std::size_t i = 0;
  while (i < text.size()) {
    auto open = text.find('(', i);
    if (open == std::string_view::npos) break;
    auto close = text.find(')', open);
    if (close == std::string_view::npos) break;
    std::string name(js_trim(text.substr(i, open - i)));
    if (!name.empty() && name.front() == ',') name.erase(0, 1);
    name = std::string(js_trim(name));
    auto v = numbers_in(text.substr(open + 1, close - open - 1));
    auto at = [&](std::size_t k, double fallback = 0) { return k < v.size() ? v[k] : fallback; };
    Mat t;
    if (name == "matrix" && v.size() >= 6) t = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (name == "translate") t = Mat::translate(at(0), at(1));
    else if (name == "scale") t = Mat::scale(at(0, 1), at(1, at(0, 1)));
    else if (name == "rotate") t = Mat::translate(at(1), at(2)) * Mat::rotate(at(0)) * Mat::translate(-at(1), -at(2));
    else if (name == "skewX") t = {1, 0, std::tan(at(0) * std::numbers::pi / 180), 1, 0, 0};
    else if (name == "skewY") t = {1, std::tan(at(0) * std::numbers::pi / 180), 0, 1, 0, 0};
    m = m * t;
    i = close + 1;
  }
  return m;
}

// ── Paths ───────────────────────────────────────────────────────────────────────────────────


void arc_to_beziers(std::vector<Segment>& out, Point p0, double rx, double ry, double angle, bool large, bool sweep,
                    Point p1) {
  if (rx == 0 || ry == 0 || (p0.x == p1.x && p0.y == p1.y)) {
    out.push_back({Segment::Line, {p1}});
    return;
  }
  rx = std::fabs(rx);
  ry = std::fabs(ry);
  double phi = angle * std::numbers::pi / 180, cs = std::cos(phi), sn = std::sin(phi);
  double dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
  double x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
  double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lambda > 1) {
    rx *= std::sqrt(lambda);
    ry *= std::sqrt(lambda);
  }
  double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  double coef = (large != sweep ? 1 : -1) * std::sqrt(std::max(0.0, num / den));
  double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
  double cx = cs * cx1 - sn * cy1 + (p0.x + p1.x) / 2, cy = sn * cx1 + cs * cy1 + (p0.y + p1.y) / 2;
  auto ang = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
  double t1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  double dt = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && dt > 0) dt -= 2 * std::numbers::pi;
  if (sweep && dt < 0) dt += 2 * std::numbers::pi;
  int n = static_cast<int>(std::ceil(std::fabs(dt) / (std::numbers::pi / 2) - 1e-9));
  n = std::max(n, 1);
  double step = dt / n;
  double k = 4.0 / 3.0 * std::tan(step / 4);
  for (int i = 0; i < n; ++i) {
    double a1 = t1 + i * step, a2 = a1 + step;
    auto on = [&](double t) {
      double x = rx * std::cos(t), y = ry * std::sin(t);
      return Point{cs * x - sn * y + cx, sn * x + cs * y + cy};
    };
    auto deriv = [&](double t) {
      double x = -rx * std::sin(t), y = ry * std::cos(t);
      return Point{cs * x - sn * y, sn * x + cs * y};
    };
    Point s = on(a1), e = on(a2), d1 = deriv(a1), d2 = deriv(a2);
    out.push_back({Segment::Cubic, {{s.x + k * d1.x, s.y + k * d1.y}, {e.x - k * d2.x, e.y - k * d2.y}, i == n - 1 ? p1 : e}});
  }
}

std::vector<Segment> parse_path(std::string_view d) {
  std::vector<Segment> out;
  Point cur, start, last_control;
  char last = 0;
  std::size_t i = 0;
  char cmd = 0;
  auto skip = [&] {
    while (i < d.size() && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\t' || d[i] == '\r')) ++i;
  };
  auto number = [&](double& v) -> bool {
    skip();
    if (i >= d.size()) return false;
    std::string tmp(d.substr(i, 64));
    char* end = nullptr;
    v = std::strtod(tmp.c_str(), &end);
    std::size_t used = static_cast<std::size_t>(end - tmp.c_str());
    if (used == 0) return false;
    i += used;
    return true;
  };
  auto flag = [&](bool& v) -> bool {
    skip();
    if (i >= d.size() || (d[i] != '0' && d[i] != '1')) return false;
    v = d[i++] == '1';
    return true;
  };
  while (true) {
    skip();
    if (i >= d.size()) break;
    if (std::isalpha(static_cast<unsigned char>(d[i]))) cmd = d[i++];
    else if (cmd == 0) break;
    bool rel = std::islower(static_cast<unsigned char>(cmd));
    char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
    auto abs_pt = [&](double x, double y) { return rel ? Point{cur.x + x, cur.y + y} : Point{x, y}; };
    double a, b, c, e, f, g;
    bool fa, fs;
    switch (up) {
      case 'Z':
        out.push_back({Segment::Close, {}});
        cur = start;
        last = 'Z';
        continue;
      case 'M':
        if (!number(a) || !number(b)) return out;
        cur = abs_pt(a, b);
        start = cur;
        out.push_back({Segment::Move, {cur}});
        cmd = rel ? 'l' : 'L';  // following pairs are lines
        last = 'M';
        continue;
      case 'L':
        if (!number(a) || !number(b)) return out;
        cur = abs_pt(a, b);
        out.push_back({Segment::Line, {cur}});
        break;
      case 'H':
        if (!number(a)) return out;
        cur = {rel ? cur.x + a : a, cur.y};
        out.push_back({Segment::Line, {cur}});
        break;
      case 'V':
        if (!number(a)) return out;
        cur = {cur.x, rel ? cur.y + a : a};
        out.push_back({Segment::Line, {cur}});
        break;
      case 'C': {
        if (!number(a) || !number(b) || !number(c) || !number(e) || !number(f) || !number(g)) return out;
        Point p1 = abs_pt(a, b), p2 = abs_pt(c, e), p3 = abs_pt(f, g);
        out.push_back({Segment::Cubic, {p1, p2, p3}});
        last_control = p2;
        cur = p3;
        last = 'C';
        continue;
      }
      case 'S': {
        if (!number(c) || !number(e) || !number(f) || !number(g)) return out;
        Point p1 = (last == 'C' || last == 'S') ? Point{2 * cur.x - last_control.x, 2 * cur.y - last_control.y} : cur;
        Point p2 = abs_pt(c, e), p3 = abs_pt(f, g);
        out.push_back({Segment::Cubic, {p1, p2, p3}});
        last_control = p2;
        cur = p3;
        last = 'S';
        continue;
      }
      case 'Q': {
        if (!number(a) || !number(b) || !number(f) || !number(g)) return out;
        Point q = abs_pt(a, b), p3 = abs_pt(f, g);
        out.push_back({Segment::Cubic, {{cur.x + 2.0 / 3 * (q.x - cur.x), cur.y + 2.0 / 3 * (q.y - cur.y)},
                             {p3.x + 2.0 / 3 * (q.x - p3.x), p3.y + 2.0 / 3 * (q.y - p3.y)},
                             p3}});
        last_control = q;
        cur = p3;
        last = 'Q';
        continue;
      }
      case 'T': {
        if (!number(f) || !number(g)) return out;
        Point q = (last == 'Q' || last == 'T') ? Point{2 * cur.x - last_control.x, 2 * cur.y - last_control.y} : cur;
        Point p3 = abs_pt(f, g);
        out.push_back({Segment::Cubic, {{cur.x + 2.0 / 3 * (q.x - cur.x), cur.y + 2.0 / 3 * (q.y - cur.y)},
                             {p3.x + 2.0 / 3 * (q.x - p3.x), p3.y + 2.0 / 3 * (q.y - p3.y)},
                             p3}});
        last_control = q;
        cur = p3;
        last = 'T';
        continue;
      }
      case 'A': {
        if (!number(a) || !number(b) || !number(c) || !flag(fa) || !flag(fs) || !number(f) || !number(g)) return out;
        Point p1 = abs_pt(f, g);
        arc_to_beziers(out, cur, a, b, c, fa, fs, p1);
        cur = p1;
        break;
      }
      default:
        return out;
    }
    last = up;
  }
  return out;
}

// ── Building the list ───────────────────────────────────────────────────────────────────────

struct Style {
  std::optional<Rgb> fill = Rgb{};
  std::optional<Rgb> stroke;
  Rgb color;
  double stroke_width = 1;
  double fill_opacity = 1, stroke_opacity = 1, opacity = 1;
  std::vector<double> dash;
  LineCap cap = LineCap::Butt;
  LineJoin join = LineJoin::Miter;
  double miter = 4;
  bool even_odd = false;
  std::string font_family;
  double font_size = 16;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strike = false;
  std::string anchor = "start";
  std::string baseline;
};

using Props = std::map<std::string, std::string, std::less<>>;

/// Attributes and `style="…"` declarations, the latter winning.
Props properties(const XmlNode& node) {
  Props out;
  for (const auto& [k, v] : node.attrs) out[k] = v;
  if (auto it = out.find("style"); it != out.end()) {
    std::string css = it->second;
    std::size_t start = 0;
    while (start < css.size()) {
      auto semi = css.find(';', start);
      std::string decl = css.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
      if (auto colon = decl.find(':'); colon != std::string::npos)
        out[std::string(js_trim(decl.substr(0, colon)))] = std::string(js_trim(decl.substr(colon + 1)));
      if (semi == std::string::npos) break;
      start = semi + 1;
    }
  }
  return out;
}

Style inherit(const Style& parent, const Props& p) {
  Style s = parent;
  s.opacity = 1;  // group opacity applies once, to the element that carries it
  auto get = [&](const char* k) -> const std::string* {
    auto it = p.find(k);
    return it == p.end() || it->second == "inherit" ? nullptr : &it->second;
  };
  if (auto v = get("color")) s.color = parse_color(*v, s.color).value_or(s.color);
  if (auto v = get("fill"))
    s.fill = v->rfind("url(", 0) == 0 ? std::optional<Rgb>(Rgb{0.8, 0.8, 0.8}) : parse_color(*v, s.color);
  if (auto v = get("stroke"))
    s.stroke = v->rfind("url(", 0) == 0 ? std::optional<Rgb>(Rgb{0.4, 0.4, 0.4}) : parse_color(*v, s.color);
  if (auto v = get("stroke-width")) s.stroke_width = parse_number(*v, 1);
  if (auto v = get("fill-opacity")) s.fill_opacity = parse_number(*v, 1);
  if (auto v = get("stroke-opacity")) s.stroke_opacity = parse_number(*v, 1);
  if (auto v = get("opacity")) s.opacity = parse_number(*v, 1);
  if (auto v = get("stroke-dasharray")) s.dash = *v == "none" ? std::vector<double>{} : numbers_in(*v);
  if (auto v = get("stroke-linecap")) s.cap = *v == "round" ? LineCap::Round : *v == "square" ? LineCap::Square : LineCap::Butt;
  if (auto v = get("stroke-linejoin")) s.join = *v == "round" ? LineJoin::Round : *v == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
  if (auto v = get("stroke-miterlimit")) s.miter = parse_number(*v, 4);
  if (auto v = get("fill-rule")) s.even_odd = *v == "evenodd";
  if (auto v = get("font-family")) s.font_family = *v;
  if (auto v = get("font-size")) s.font_size = parse_number(*v, s.font_size);
  if (auto v = get("font-weight")) s.bold = *v == "bold" || *v == "bolder" || parse_number(*v, 400) >= 600;
  if (auto v = get("font-style")) s.italic = *v == "italic" || *v == "oblique";
  // Not inherited in CSS, but drawn through a text's runs all the same.
  if (auto v = get("text-decoration")) {
    s.underline = v->find("underline") != std::string::npos;
    s.strike = v->find("line-through") != std::string::npos;
  }
  if (auto v = get("text-anchor")) s.anchor = *v;
  if (auto v = get("dominant-baseline")) s.baseline = *v;
  return s;
}

/// The transform a view box asks for, into a viewport of the given size.
Mat viewbox(const XmlNode& node, double width, double height) {
  const std::string* vb = node.attr("viewBox");
  if (!vb) return {};
  auto v = numbers_in(*vb);
  if (v.size() < 4 || v[2] <= 0 || v[3] <= 0) return {};
  double sx = width / v[2], sy = height / v[3];
  std::string par = node.attr("preserveAspectRatio") ? *node.attr("preserveAspectRatio") : "xMidYMid meet";
  if (par.find("none") != std::string::npos) return Mat::scale(sx, sy) * Mat::translate(-v[0], -v[1]);
  double s = par.find("slice") != std::string::npos ? std::max(sx, sy) : std::min(sx, sy);
  double tx = 0, ty = 0;
  double extra_x = width - v[2] * s, extra_y = height - v[3] * s;
  if (par.find("xMid") != std::string::npos) tx = extra_x / 2;
  else if (par.find("xMax") != std::string::npos) tx = extra_x;
  if (par.find("YMid") != std::string::npos) ty = extra_y / 2;
  else if (par.find("YMax") != std::string::npos) ty = extra_y;
  return Mat::translate(tx, ty) * Mat::scale(s, s) * Mat::translate(-v[0], -v[1]);
}

class Builder {
 public:
  std::vector<Item> items;

  void root(const XmlNode& svg) {
    collect_defs(svg);
    Style style;
    for (const auto& c : svg.children) draw(*c, style, "", "");
  }

 private:
  std::map<std::string, const XmlNode*, std::less<>> defs_;
  /// What a <marker> draws, read once however many lines it ends: how it turns, its scale, the
  /// transform from its view box to the vertex, and its content as items of no diagram item.
  struct MarkerArt {
    enum Orient { Fixed, Auto, AutoStartReverse } orient = Fixed;
    double angle = 0;  ///< for Fixed
    bool user_space = false;
    Mat to_ref;  ///< moves refX/refY (in view box units) to the vertex
    Mat view;
    std::vector<Item> items;
  };
  std::map<const XmlNode*, MarkerArt> marker_art_;

  void collect_defs(const XmlNode& node) {
    if (const std::string* id = node.attr("id")) defs_[*id] = &node;
    for (const auto& c : node.children) collect_defs(*c);
  }

  Item& push(Item::Kind kind, const std::string& owner, const std::string& owner_kind) {
    Item& item = items.emplace_back();
    item.kind = kind;
    item.owner = owner;
    item.owner_kind = owner_kind;
    return item;
  }

  void shape(std::vector<Segment> segments, const Style& s, bool fill, const std::string& owner,
             const std::string& kind) {
    if (segments.empty()) return;
    Paint paint;
    if (fill && s.fill) paint.fill = s.fill;
    if (s.stroke && s.stroke_width > 0) paint.stroke = s.stroke;
    if (!paint.fill && !paint.stroke) return;
    paint.stroke_width = s.stroke_width;
    paint.fill_alpha = s.fill_opacity * s.opacity;
    paint.stroke_alpha = s.stroke_opacity * s.opacity;
    paint.dash = s.dash;
    paint.cap = s.cap;
    paint.join = s.join;
    paint.miter = s.miter;
    paint.even_odd = s.even_odd;
    Item& item = push(Item::Path, owner, kind);
    item.path = std::move(segments);
    item.paint = std::move(paint);
  }

  /// Draws the marker `ref` at the end (or the start) of `segments`, turned along the line there.
  void marker(const std::vector<Segment>& segments, const std::string& ref, const Style& s, const std::string& owner,
              const std::string& kind, bool at_start) {
    auto open = ref.find('#'), close = ref.find(')');
    if (open == std::string::npos) return;
    auto it = defs_.find(ref.substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1));
    if (it == defs_.end() || segments.empty()) return;
    const XmlNode& def = *it->second;
    // The vertex and the direction of the line through it: of the last drawing segment at the
    // end, of the first one at the start.
    Point end, from;
    bool found = false;
    if (at_start) {
      for (std::size_t i = 1; i < segments.size() && !found; ++i) {
        const Segment& seg = segments[i];
        if (seg.op != Segment::Line && seg.op != Segment::Cubic) continue;
        const Segment& prev = segments[i - 1];
        if (prev.op != Segment::Move) break;
        end = prev.p[0];
        Point next = seg.p[0];
        // A curve leaves along its first control point that is not the vertex itself.
        if (seg.op == Segment::Cubic && next.x == end.x && next.y == end.y)
          next = seg.p[1].x != end.x || seg.p[1].y != end.y ? seg.p[1] : seg.p[2];
        // `from` sits behind the vertex, so the angle points along the path, into it.
        from = {2 * end.x - next.x, 2 * end.y - next.y};
        found = true;
      }
    } else {
      for (std::size_t i = segments.size(); i-- > 0 && !found;) {
        const Segment& seg = segments[i];
        if (seg.op == Segment::Line || seg.op == Segment::Cubic) {
          end = seg.op == Segment::Line ? seg.p[0] : seg.p[2];
          if (seg.op == Segment::Cubic)
            from = (seg.p[1].x != end.x || seg.p[1].y != end.y) ? seg.p[1] : seg.p[0];
          else if (i > 0)
            from = segments[i - 1].op == Segment::Cubic ? segments[i - 1].p[2] : segments[i - 1].p[0];
          else
            from = end;
          found = true;
        }
      }
    }
    if (!found) return;
    const MarkerArt& art = marker_art(def);
    double angle = art.angle;
    if (art.orient != MarkerArt::Fixed) {
      angle = std::atan2(end.y - from.y, end.x - from.x) * 180 / std::numbers::pi;
      // At the start, `auto` points along the path and `auto-start-reverse` back out of it.
      if (at_start && art.orient == MarkerArt::AutoStartReverse) angle += 180;
    }
    double unit = art.user_space ? 1 : s.stroke_width;
    push(Item::Save, owner, kind);
    // Left to right: floating-point products do not reassociate, and the list is pinned to the bit.
    push(Item::Transform, owner, kind).matrix =
        Mat::translate(end.x, end.y) * Mat::rotate(angle) * Mat::scale(unit, unit) * art.to_ref * art.view;
    for (const Item& drawn : art.items) {
      Item& item = items.emplace_back(drawn);
      // The art was drawn for no diagram item; it belongs to the line's.
      if (item.owner.empty() && item.owner_kind.empty()) {
        item.owner = owner;
        item.owner_kind = kind;
      }
    }
    push(Item::Restore, owner, kind);
  }

  const MarkerArt& marker_art(const XmlNode& def) {
    auto [it, added] = marker_art_.try_emplace(&def);
    MarkerArt& art = it->second;
    // A marker whose content ends in itself draws nothing the second time, instead of recursing.
    if (!added) return art;
    auto p = properties(def);
    auto num = [&](const char* k, double fallback) {
      auto i = p.find(k);
      return i == p.end() ? fallback : parse_number(i->second, fallback);
    };
    if (auto o = p.find("orient"); o != p.end()) {
      if (o->second == "auto") art.orient = MarkerArt::Auto;
      else if (o->second == "auto-start-reverse") art.orient = MarkerArt::AutoStartReverse;
      else art.angle = parse_number(o->second);
    }
    double w = num("markerWidth", 3), h = num("markerHeight", 3);
    art.user_space = p.count("markerUnits") && p.at("markerUnits") == "userSpaceOnUse";
    art.view = viewbox(def, w, h);
    Point ref_point = art.view.apply({num("refX", 0), num("refY", 0)});
    art.to_ref = Mat::translate(-ref_point.x, -ref_point.y);
    // Drawn into the list and moved out, so its content is drawn the way all content is.
    std::size_t first = items.size();
    Style base;
    for (const auto& c : def.children) draw(*c, base, "", "");
    auto from = items.begin() + static_cast<std::ptrdiff_t>(first);
    art.items.assign(std::make_move_iterator(from), std::make_move_iterator(items.end()));
    items.erase(from, items.end());
    return art;
  }

  void markers(const std::vector<Segment>& segments, const Props& p,
               const Style& s, const std::string& owner, const std::string& kind) {
    if (auto m = p.find("marker-start"); m != p.end()) marker(segments, m->second, s, owner, kind, true);
    if (auto m = p.find("marker-end"); m != p.end()) marker(segments, m->second, s, owner, kind, false);
  }

  void text(const XmlNode& node, const Style& s, const std::string& owner, const std::string& kind) {
    if (!s.fill) return;
    const bool has_runs = std::any_of(node.children.begin(), node.children.end(),
                                      [](const auto& c) { return c->name == "tspan" && !c->text.empty(); });
    std::string content;
    // The label's weight, slant and lines: its own, or its runs' when they all have the same.
    bool bold = s.bold, italic = s.italic, underline = s.underline, strike = s.strike;
    std::vector<TextSpan> kept;
    if (!has_runs) {
      // Every label but those with fontRuns: no runs to keep.
      content = js_trim(node.text);
    } else {
      std::vector<TextSpan> spans;
      auto add = [&](const std::string& text, const Style& st) {
        spans.push_back(TextSpan{text, st.bold, st.italic, st.underline, st.strike});
      };
      if (!node.text.empty()) add(node.text, s);
      for (const auto& c : node.children)
        if (c->name == "tspan" && !c->text.empty()) add(c->text, inherit(s, properties(*c)));
      std::string raw;
      for (const auto& sp : spans) raw += sp.content;
      const std::string_view trimmed = js_trim(raw);
      // The runs of the trimmed text: what the trim took off either end goes from the runs too.
      const auto from = static_cast<std::size_t>(trimmed.data() - raw.data());
      const std::size_t to = from + trimmed.size();
      std::size_t at = 0;
      for (auto& sp : spans) {
        const std::size_t start = at, end = at + sp.content.size();
        at = end;
        if (end <= from || start >= to) continue;
        if (end > to) sp.content.erase(to - start);
        if (start < from) sp.content.erase(0, from - start);
        kept.push_back(std::move(sp));
      }
      content = trimmed;
      if (!kept.empty()) {
        const TextSpan& f = kept.front();
        const bool uniform = std::all_of(kept.begin(), kept.end(), [&](const TextSpan& sp) {
          return sp.bold == f.bold && sp.italic == f.italic && sp.underline == f.underline && sp.strike == f.strike;
        });
        bold = f.bold;
        italic = f.italic;
        underline = f.underline;
        strike = f.strike;
        if (uniform) kept.clear();
      }
    }
    if (content.empty()) return;
    auto p = properties(node);
    auto num = [&](const char* k) {
      auto i = p.find(k);
      if (i == p.end()) return 0.0;
      auto v = numbers_in(i->second);
      return v.empty() ? 0.0 : v[0];
    };
    Item& item = push(Item::Label, owner, kind);
    item.text = Text{std::move(content), num("x") + num("dx"), num("y") + num("dy"), s.anchor, s.baseline,
                     s.font_family, s.font_size, bold, *s.fill, s.fill_opacity * s.opacity, italic, underline,
                     strike, std::move(kept)};
  }

  void draw(const XmlNode& node, const Style& parent, std::string owner, std::string kind) {
    const std::string& name = node.name;
    if (name == "defs" || name == "marker" || name == "title" || name == "desc" || name == "style" ||
        name == "metadata" || name == "clipPath" || name == "mask" || name == "linearGradient" ||
        name == "radialGradient" || name == "pattern" || name == "symbol")
      return;
    auto p = properties(node);
    auto num = [&](const char* k, double fallback = 0) {
      auto i = p.find(k);
      return i == p.end() ? fallback : parse_number(i->second, fallback);
    };
    if (auto d = p.find("display"); d != p.end() && d->second == "none") return;
    if (auto v = p.find("visibility"); v != p.end() && v->second == "hidden") return;
    // Diagram items are the renderer's groups with a data-id.
    if (auto id = p.find("data-id"); id != p.end()) {
      std::string cls = p.count("class") ? p.at("class") : "";
      std::string k = cls.find("dd-node") != std::string::npos   ? "node"
                      : cls.find("dd-edge") != std::string::npos ? "edge"
                      : cls.find("dd-group") != std::string::npos ? "group"
                                                                  : "";
      if (!k.empty()) {
        owner = id->second;
        kind = k;
      }
    }
    Style s = inherit(parent, p);
    Mat transform = p.count("transform") ? parse_transform(p.at("transform")) : Mat{};
    push(Item::Save, owner, kind);
    if (!transform.identity()) push(Item::Transform, owner, kind).matrix = transform;
    if (name == "svg") {
      double x = num("x"), y = num("y"), w = num("width", 0), h = num("height", 0);
      if (w > 0 && h > 0) {
        Item& clip = push(Item::Clip, owner, kind);
        clip.rect[0] = x;
        clip.rect[1] = y;
        clip.rect[2] = w;
        clip.rect[3] = h;
        Mat m = Mat::translate(x, y) * viewbox(node, w, h);
        if (!m.identity()) push(Item::Transform, owner, kind).matrix = m;
        for (const auto& c : node.children) draw(*c, s, owner, kind);
      }
    } else if (name == "g" || name == "a" || name == "switch") {
      for (const auto& c : node.children) draw(*c, s, owner, kind);
    } else if (name == "rect") {
      double x = num("x"), y = num("y"), w = num("width"), h = num("height");
      double rx = num("rx", -1), ry = num("ry", -1);
      if (rx < 0) rx = ry;
      if (ry < 0) ry = rx;
      rx = std::min(std::max(rx, 0.0), w / 2);
      ry = std::min(std::max(ry, 0.0), h / 2);
      if (w > 0 && h > 0) {
        std::vector<Segment> segs;
        if (rx > 0 && ry > 0) {
          std::string d = "M" + fmt(x + rx) + " " + fmt(y) + " H" + fmt(x + w - rx) + " A" + fmt(rx) + " " + fmt(ry) +
                          " 0 0 1 " + fmt(x + w) + " " + fmt(y + ry) + " V" + fmt(y + h - ry) + " A" + fmt(rx) + " " +
                          fmt(ry) + " 0 0 1 " + fmt(x + w - rx) + " " + fmt(y + h) + " H" + fmt(x + rx) + " A" +
                          fmt(rx) + " " + fmt(ry) + " 0 0 1 " + fmt(x) + " " + fmt(y + h - ry) + " V" + fmt(y + ry) +
                          " A" + fmt(rx) + " " + fmt(ry) + " 0 0 1 " + fmt(x + rx) + " " + fmt(y) + " Z";
          segs = parse_path(d);
        } else {
          segs = {{Segment::Move, {{x, y}}},
                  {Segment::Line, {{x + w, y}}},
                  {Segment::Line, {{x + w, y + h}}},
                  {Segment::Line, {{x, y + h}}},
                  {Segment::Close, {}}};
        }
        shape(std::move(segs), s, true, owner, kind);
      }
    } else if (name == "circle" || name == "ellipse") {
      double cx = num("cx"), cy = num("cy");
      double rx = name == "circle" ? num("r") : num("rx"), ry = name == "circle" ? num("r") : num("ry");
      if (rx > 0 && ry > 0)
        shape(parse_path("M" + fmt(cx - rx) + " " + fmt(cy) + " A" + fmt(rx) + " " + fmt(ry) + " 0 1 0 " + fmt(cx + rx) +
                         " " + fmt(cy) + " A" + fmt(rx) + " " + fmt(ry) + " 0 1 0 " + fmt(cx - rx) + " " + fmt(cy) + " Z"),
              s, true, owner, kind);
    } else if (name == "line") {
      std::vector<Segment> segs{{Segment::Move, {{num("x1"), num("y1")}}}, {Segment::Line, {{num("x2"), num("y2")}}}};
      shape(segs, s, false, owner, kind);
      markers(segs, p, s, owner, kind);
    } else if (name == "polyline" || name == "polygon") {
      auto v = numbers_in(p.count("points") ? p.at("points") : "");
      std::vector<Segment> segs;
      for (std::size_t i = 0; i + 1 < v.size(); i += 2)
        segs.push_back({segs.empty() ? Segment::Move : Segment::Line, {{v[i], v[i + 1]}}});
      if (name == "polygon" && !segs.empty()) segs.push_back({Segment::Close, {}});
      shape(segs, s, true, owner, kind);
      markers(segs, p, s, owner, kind);
    } else if (name == "path") {
      auto segs = parse_path(p.count("d") ? p.at("d") : "");
      shape(segs, s, true, owner, kind);
      markers(segs, p, s, owner, kind);
    } else if (name == "text") {
      text(node, s, owner, kind);
    }
    push(Item::Restore, owner, kind);
  }
};

}  // namespace

std::vector<Segment> parse_path_data(std::string_view d) { return parse_path(d); }

std::optional<DisplayList> display_list_from_svg(std::string_view svg) {
  auto doc = io::parse_xml(svg);
  const XmlNode* root = nullptr;
  for (const auto& c : doc->children)
    if (c->name == "svg") root = c.get();
  if (!root) return std::nullopt;
  DisplayList list;
  list.width = parse_number(root->attr("width") ? *root->attr("width") : "", 0);
  list.height = parse_number(root->attr("height") ? *root->attr("height") : "", 0);
  if (!(list.width > 0) || !(list.height > 0)) {
    auto v = numbers_in(root->attr("viewBox") ? *root->attr("viewBox") : "");
    if (v.size() < 4) return std::nullopt;
    list.width = v[2];
    list.height = v[3];
  }
  list.root = viewbox(*root, list.width, list.height);
  Builder b;
  b.root(*root);
  list.items = std::move(b.items);
  return list;
}

}  // namespace dd::draw
