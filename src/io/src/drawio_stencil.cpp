#include "diadraw/io/drawio_stencil.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/io/codec.hpp"
#include "diadraw/io/xml.hpp"

namespace dd::io {

namespace {

/// `String(Math.round(v * 100) / 100)`
std::string n(double v) { return js_number_string(js_round(v * 100) / 100); }

/// `Number(s)`, NaN when absent.
double number(const std::string* s) { return s ? js_to_number(*s) : std::nan(""); }

/// `Number(node.attrs[attr] ?? 0) || 0`
double num(const XmlNode& node, std::string_view attr) {
  double v = number(node.attr(attr));
  if (!node.attr(attr)) v = 0;
  return std::isnan(v) ? 0 : v;
}

/// `value || fallback` for numbers: NaN and 0 take the fallback.
double or_else(double v, double fallback) { return std::isnan(v) || v == 0 ? fallback : v; }

std::string attr_or(const XmlNode& node, std::string_view attr, std::string_view fallback) {
  const std::string* v = node.attr(attr);
  return v ? *v : std::string(fallback);
}

std::string shape_id(const std::string& name) {
  // name.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '')
  std::string lower = js::to_lower(name), id;
  bool gap = false;
  for (char ch : lower) {
    bool keep = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (keep) {
      if (gap) id += '_';
      gap = false;
      id += ch;
    } else {
      gap = true;
    }
  }
  if (gap && !id.empty()) id += '_';
  std::size_t a = id.find_first_not_of('_');
  id = a == std::string::npos ? "" : id.substr(a, id.find_last_not_of('_') - a + 1);
  if (!id.empty() && id[0] >= '0' && id[0] <= '9') return "_" + id;
  return id.empty() ? "shape" : id;
}

/// `s.replace(/\s+/g, '_')` with JavaScript's whitespace.
std::string underscore_spaces(std::string_view s) {
  std::string out;
  std::size_t i = 0;
  bool gap = false;
  while (i < s.size()) {
    std::size_t len = 0;
    if (is_js_space(s, i, &len)) {
      gap = true;
      i += len;
      continue;
    }
    if (gap) out += '_';
    gap = false;
    out += s[i++];
  }
  if (gap) out += '_';
  return out;
}

struct Drawn {
  std::string body;
  double width = 0, height = 0;
  std::string error;
};

struct State {
  std::string fill{kStencilFill}, stroke{kStencilStroke};
  double stroke_width = 1;
  bool dashed = false;
  std::optional<std::string> dash;
  double alpha = 1, fill_alpha = 1, stroke_alpha = 1;
  std::optional<std::string> join, cap;
  std::optional<double> miter;
};

Drawn stencil_to_svg(const XmlNode& shape) {
  double width = number(shape.attr("w")), height = number(shape.attr("h"));
  if (!shape.attr("w")) width = 0;
  if (!shape.attr("h")) height = 0;
  if (!(width > 0 && height > 0)) return {"", 0, 0, "no size"};
  State state;
  const std::string* sw = shape.attr("strokewidth");
  state.stroke_width = sw && *sw == "inherit" ? 1 : or_else(sw ? js_to_number(*sw) : 1, 1);
  std::vector<State> states;
  std::string d, out;
  auto colour = [](const std::string* c, std::string_view fallback) {
    if (!c || c->empty() || *c == "default") return std::string(fallback);
    return *c == "none" ? std::string("none") : *c;
  };
  auto paint = [&](bool fill, bool stroke) {
    if (d.empty()) return;
    std::string attrs = "d=\"" + std::string(js_trim(d)) + "\"";
    attrs += " fill=\"" + (fill ? state.fill : std::string("none")) + "\"";
    if (fill && state.fill != "none" && state.alpha * state.fill_alpha < 1)
      attrs += " fill-opacity=\"" + n(state.alpha * state.fill_alpha) + "\"";
    if (stroke && state.stroke != "none") {
      attrs += " stroke=\"" + state.stroke + "\" stroke-width=\"" + n(state.stroke_width) + "\"";
      if (state.alpha * state.stroke_alpha < 1)
        attrs += " stroke-opacity=\"" + n(state.alpha * state.stroke_alpha) + "\"";
      if (state.dashed) attrs += " stroke-dasharray=\"" + state.dash.value_or("3 3") + "\"";
      if (state.join) attrs += " stroke-linejoin=\"" + *state.join + "\"";
      if (state.cap) attrs += " stroke-linecap=\"" + (*state.cap == "flat" ? std::string("butt") : *state.cap) + "\"";
      if (state.miter && !std::isnan(*state.miter) && *state.miter != 0)
        attrs += " stroke-miterlimit=\"" + js_number_string(*state.miter) + "\"";
    } else {
      attrs += " stroke=\"none\"";
    }
    out += "<path " + attrs + "/>";
    d.clear();
  };
  std::vector<const XmlNode*> ops;
  for (const char* part : {"background", "foreground"})
    if (const XmlNode* p = shape.child(part))
      for (const auto& c : p->children) ops.push_back(c.get());
  // Glyphs (icons that only fill) take the cell's fill colour in draw.io; outlines take white.
  bool outlined = false;
  for (const XmlNode* op : ops) outlined = outlined || op->name == "fillstroke" || op->name == "stroke";
  if (!outlined) state.fill = std::string(kGlyphFill);
  for (const XmlNode* op_ptr : ops) {
    const XmlNode& op = *op_ptr;
    const std::string& name = op.name;
    if (name == "path") {
      for (const auto& cp : op.children) {
        const XmlNode& c = *cp;
        if (c.name == "move") d += "M" + n(num(c, "x")) + " " + n(num(c, "y")) + " ";
        else if (c.name == "line") d += "L" + n(num(c, "x")) + " " + n(num(c, "y")) + " ";
        else if (c.name == "quad")
          d += "Q" + n(num(c, "x1")) + " " + n(num(c, "y1")) + " " + n(num(c, "x2")) + " " + n(num(c, "y2")) + " ";
        else if (c.name == "curve")
          d += "C" + n(num(c, "x1")) + " " + n(num(c, "y1")) + " " + n(num(c, "x2")) + " " + n(num(c, "y2")) + " " +
               n(num(c, "x3")) + " " + n(num(c, "y3")) + " ";
        else if (c.name == "arc")
          d += "A" + n(num(c, "rx")) + " " + n(num(c, "ry")) + " " + n(num(c, "x-axis-rotation")) + " " +
               (num(c, "large-arc-flag") != 0 ? "1" : "0") + " " + (num(c, "sweep-flag") != 0 ? "1" : "0") + " " +
               n(num(c, "x")) + " " + n(num(c, "y")) + " ";
        else if (c.name == "close") d += "Z ";
      }
    } else if (name == "rect") {
      double x = num(op, "x"), y = num(op, "y"), w = num(op, "w"), h = num(op, "h");
      d += "M" + n(x) + " " + n(y) + " h" + n(w) + " v" + n(h) + " h" + n(-w) + " Z ";
    } else if (name == "roundrect") {
      double x = num(op, "x"), y = num(op, "y"), w = num(op, "w"), h = num(op, "h");
      double factor = or_else(number(op.attr("arcsize")), 15) / 100;
      double r = std::min(w * factor, h * factor);
      std::string R = n(r);
      d += "M" + n(x + r) + " " + n(y) + " H" + n(x + w - r) + " A" + R + " " + R + " 0 0 1 " + n(x + w) + " " +
           n(y + r) + " V" + n(y + h - r) + " A" + R + " " + R + " 0 0 1 " + n(x + w - r) + " " + n(y + h) + " H" +
           n(x + r) + " A" + R + " " + R + " 0 0 1 " + n(x) + " " + n(y + h - r) + " V" + n(y + r) + " A" + R + " " +
           R + " 0 0 1 " + n(x + r) + " " + n(y) + " Z ";
    } else if (name == "ellipse") {
      double x = num(op, "x"), y = num(op, "y"), w = num(op, "w"), h = num(op, "h");
      double rx = w / 2, ry = h / 2;
      d += "M" + n(x) + " " + n(y + ry) + " A" + n(rx) + " " + n(ry) + " 0 1 0 " + n(x + w) + " " + n(y + ry) + " A" +
           n(rx) + " " + n(ry) + " 0 1 0 " + n(x) + " " + n(y + ry) + " Z ";
    } else if (name == "fillstroke") {
      paint(true, true);
    } else if (name == "fill") {
      paint(true, false);
    } else if (name == "stroke") {
      paint(false, true);
    } else if (name == "fillcolor") {
      state.fill = colour(op.attr("color"), kStencilFill);
    } else if (name == "strokecolor") {
      state.stroke = colour(op.attr("color"), kStencilStroke);
    } else if (name == "strokewidth") {
      const std::string* w = op.attr("width");
      state.stroke_width = or_else(w ? js_to_number(*w) : 1, 1);
    } else if (name == "dashed") {
      const std::string* v = op.attr("dashed");
      state.dashed = v && *v == "1";
    } else if (name == "dashpattern") {
      if (const std::string* p = op.attr("pattern"); p && !p->empty()) state.dash = *p;
    } else if (name == "alpha" || name == "fillalpha" || name == "strokealpha") {
      const std::string* a = op.attr("alpha");
      double v = a ? js_to_number(*a) : 1;
      (name == "alpha" ? state.alpha : name == "fillalpha" ? state.fill_alpha : state.stroke_alpha) = v;
    } else if (name == "linejoin") {
      if (const std::string* j = op.attr("join"); j && !j->empty()) state.join = *j;
    } else if (name == "linecap") {
      if (const std::string* c = op.attr("cap"); c && !c->empty()) state.cap = *c;
    } else if (name == "miterlimit") {
      state.miter = number(op.attr("limit"));
    } else if (name == "save") {
      states.push_back(state);
    } else if (name == "restore") {
      if (!states.empty()) {
        state = states.back();
        states.pop_back();
      }
    }
    // text, image, include-shape, fontcolor…: not drawn.
  }
  if (out.empty()) return {"", 0, 0, "nothing drawn"};
  return {out, width, height, ""};
}

StencilSet convert_document(const XmlNode& doc, std::string_view file) {
  const XmlNode* root = doc.child("shapes");
  std::string name;
  if (const std::string* a = root ? root->attr("name") : nullptr) {
    name = *a;
  } else {
    // file.replace(/\.xml$/, '').replace(/\//g, '.')
    name = std::string(file);
    if (name.ends_with(".xml")) name.resize(name.size() - 4);
    for (char& ch : name)
      if (ch == '/') ch = '.';
  }
  StencilSet set;
  set.name = js::to_lower(name);
  set.file = std::string(file);
  std::vector<std::string> seen;
  if (!root) return set;
  for (const auto& sp : root->children) {
    const XmlNode& shape = *sp;
    if (shape.name != "shape") continue;
    std::string label = attr_or(shape, "name", "shape");
    Drawn svg = stencil_to_svg(shape);
    if (!svg.error.empty()) {
      set.skipped.push_back({label, svg.error});
      continue;
    }
    std::string id = shape_id(label);
    while (std::find(seen.begin(), seen.end(), id) != seen.end()) id += '_';
    seen.push_back(id);
    set.shapes.push_back(
        {set.name + "." + underscore_spaces(js::to_lower(label)), id, label, svg.width, svg.height, svg.body});
  }
  return set;
}

void set_attr(XmlNode& node, const std::string& key, std::string value) {
  for (auto& [k, v] : node.attrs)
    if (k == key) {
      v = std::move(value);
      return;
    }
  node.attrs.emplace_back(key, std::move(value));
}

bool is_draw_op(const std::string& name) {
  for (const char* d :
       {"move", "line", "quad", "curve", "arc", "rect", "roundrect", "ellipse", "text", "image", "include-shape"})
    if (name == d) return true;
  return false;
}

/// One set of `stencils.min.js`: raw deflate of five streams (element ops, x, y and other numbers
/// as zigzag varints delta-coded, and a string table), as draw.io's own loader reads them.
std::unique_ptr<XmlNode> decode_stencil_binary(std::string_view base64) {
  auto bytes = base64_decode(base64);
  if (!bytes) throw std::runtime_error("bad base64");
  auto inflated = inflate_raw(*bytes);
  if (!inflated) throw std::runtime_error("cannot inflate");
  const std::string& data = *inflated;
  auto u8 = [&](std::size_t i) -> unsigned {
    if (i >= data.size()) throw std::runtime_error("truncated");
    return static_cast<unsigned char>(data[i]);
  };
  std::string_view streams[5];
  for (std::size_t i = 0, o = 20; i < 5; ++i) {
    std::size_t len = (std::size_t(u8(4 * i)) << 24) | (u8(4 * i + 1) << 16) | (u8(4 * i + 2) << 8) | u8(4 * i + 3);
    if (o > data.size()) throw std::runtime_error("truncated");
    streams[i] = std::string_view(data).substr(o, len);  // subarray clamps, as in JavaScript
    o += len;
  }
  std::string_view ops = streams[0];
  std::vector<std::string> strs;
  {
    std::string_view table = streams[4];
    std::size_t start = 0;
    while (true) {
      std::size_t z = table.find('\0', start);
      strs.emplace_back(table.substr(start, z == std::string_view::npos ? std::string_view::npos : z - start));
      if (z == std::string_view::npos) break;
      start = z + 1;
    }
  }
  std::size_t pos[4] = {0, 0, 0, 0};
  std::size_t si = 0;
  auto next_str = [&]() -> const std::string& {
    if (si >= strs.size()) throw std::runtime_error("string table exhausted");
    return strs[si++];
  };
  auto op_at = [&](std::size_t i) -> unsigned {
    if (i >= ops.size()) throw std::runtime_error("truncated ops");
    return static_cast<unsigned char>(ops[i]);
  };
  std::vector<std::string> names, attr_names;
  auto root = std::make_unique<XmlNode>();
  root->name = "#document";
  std::vector<XmlNode*> parents{root.get()};
  double px = 0, py = 0;
  std::map<std::string, double> pn;
  auto read = [&](int k) {
    std::string_view stream = streams[k];
    double v = 0, m = 1;
    unsigned c;
    do {
      if (pos[k] >= stream.size()) throw std::runtime_error("truncated numbers");
      c = static_cast<unsigned char>(stream[pos[k]++]);
      v += (c & 127) * m;
      m *= 128;
    } while (c & 128);
    return std::fmod(v, 2) == 1 ? -(v + 1) / 2 : v / 2;
  };
  auto format = [](double v) { return js_number_string(v / 1000); };
  while (pos[0] < ops.size()) {
    unsigned op = op_at(pos[0]++);
    if (op == 255) {
      parents.pop_back();
      if (parents.empty()) throw std::runtime_error("unbalanced");
      continue;
    }
    if ((op >> 1) == names.size()) names.push_back(next_str());
    if ((op >> 1) >= names.size()) throw std::runtime_error("bad name");
    const std::string name = names[op >> 1];
    auto node = std::make_unique<XmlNode>();
    node->name = name;
    XmlNode* raw = node.get();
    parents.back()->children.push_back(std::move(node));
    unsigned count = op_at(pos[0]++);
    bool coords = is_draw_op(name);
    if (name == "shape") {
      px = py = 0;
      pn.clear();
    }
    for (unsigned i = 0; i < count; ++i) {
      unsigned id = op_at(pos[0]++);
      if (id == attr_names.size()) attr_names.push_back(next_str());
      if (id >= attr_names.size()) throw std::runtime_error("bad attribute");
      const std::string attr = attr_names[id];
      if (op_at(pos[0]++) == 0) {
        set_attr(*raw, attr, next_str());
      } else if (coords && (attr == "x" || attr == "x1" || attr == "x2" || attr == "x3")) {
        set_attr(*raw, attr, format(px += read(1)));
      } else if (coords && (attr == "y" || attr == "y1" || attr == "y2" || attr == "y3")) {
        set_attr(*raw, attr, format(py += read(2)));
      } else {
        double& next = pn[name + " " + attr];
        next += read(3);
        set_attr(*raw, attr, format(next));
      }
    }
    // An even op opens the element: what follows are its children until a 255.
    if (!(op & 1)) parents.push_back(raw);
  }
  return root;
}

}  // namespace

StencilSet convert_stencil_set(std::string_view xml, std::string_view file) {
  auto doc = parse_xml(xml);
  return convert_document(*doc, file);
}

std::vector<StencilSet> read_stencil_sets(std::string_view text, std::string_view file) {
  std::size_t i = 0, len = 0;
  while (i < text.size() && is_js_space(text, i, &len)) i += len;
  if (text.substr(i).starts_with("<")) return {convert_stencil_set(text, file)};
  std::vector<StencilSet> sets;
  static const js::Regex entry(R"(\['([^']+\.xml)'\]\s*=\s*'([^']*)')", "g");
  for (const auto& m : entry.match_all(text)) {
    std::string name = m.str(1), data = m.str(2);
    try {
      auto doc = decode_stencil_binary(data);
      sets.push_back(convert_document(*doc, name));
    } catch (const std::exception&) {
      sets.push_back({name, name, {}, {{name, "cannot inflate"}}});
    }
  }
  return sets;
}

}  // namespace dd::io
