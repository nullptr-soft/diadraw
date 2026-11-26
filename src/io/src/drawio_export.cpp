// Port of packages/import/src/drawio-export.ts.

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/layout/layout.hpp"

namespace dd::io {

namespace {

std::string attr(std::string_view v) {
  std::string out;
  for (char c : v) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '\n': out += "&#10;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string num(double v) { return js_number_string(js_round(v * 100) / 100); }

/// `k.replace(/[^\w.-]/g, '_')`, one `_` per UTF-16 unit as in JavaScript.
std::string data_key(std::string_view k) {
  std::string out;
  for (std::size_t i = 0; i < k.size();) {
    std::size_t start = i;
    std::uint32_t cp = next_code_point(k, i);
    bool keep = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_' ||
                cp == '.' || cp == '-';
    if (keep)
      out.append(k, start, i - start);
    else
      out.append(cp >= 0x10000 ? 2 : 1, '_');
  }
  return out;
}

std::string shape_style(std::string_view shape) {
  static const std::map<std::string, std::string, std::less<>> m{
      {"rect", "rounded=0"},
      {"rounded", "rounded=1"},
      {"stadium", "rounded=1;arcSize=50"},
      {"ellipse", "ellipse"},
      {"circle", "ellipse;aspect=fixed"},
      {"interface", "ellipse;aspect=fixed"},
      {"rhombus", "rhombus"},
      {"triangle", "triangle"},
      {"hexagon", "shape=hexagon;perimeter=hexagonPerimeter2"},
      {"octagon", "shape=mxgraph.basic.octagon2"},
      {"cylinder", "shape=cylinder3;boundedLbl=1"},
      {"queue", "shape=cylinder3;direction=south"},
      {"parallelogram", "shape=parallelogram;perimeter=parallelogramPerimeter"},
      {"trapezoid", "shape=trapezoid;perimeter=trapezoidPerimeter"},
      {"document", "shape=document;boundedLbl=1"},
      {"cloud", "ellipse;shape=cloud"},
      {"actor", "shape=umlActor;verticalLabelPosition=bottom;verticalAlign=top"},
      {"person", "shape=umlActor;verticalLabelPosition=bottom;verticalAlign=top"},
      {"note", "shape=note"},
      {"card", "shape=card"},
      {"step", "shape=step;perimeter=stepPerimeter"},
      {"arrow", "shape=singleArrow"},
      {"process", "shape=process"},
      {"cube", "shape=cube"},
      {"swimlane", "swimlane;startSize=26"},
      {"folder", "shape=folder"},
      {"frame", "shape=umlFrame"},
      {"lifeline", "shape=umlLifeline;perimeter=lifelinePerimeter;collapsible=0;recursiveResize=0"},
      {"module", "shape=module"},
      {"text", "text;strokeColor=none;fillColor=none"},
      {"image", "rounded=0"},
      {"invisible", "group"},
  };
  auto it = m.find(shape);
  return it == m.end() ? "rounded=0" : it->second;
}

/// A draw.io style for an element that did not come from draw.io; `style` blocks under its own.
std::string element_style(const Element& e, bool container, const std::vector<StyleRule>& rules) {
  static const std::map<std::string, std::string, std::less<>> kind_shape{
      {"decision", "rhombus"}, {"database", "cylinder"}, {"person", "person"},
      {"start", "stadium"},    {"end", "stadium"},        {"note", "note"}};
  std::string shape;
  if (e.shape)
    shape = *e.shape;
  else if (auto it = kind_shape.find(e.kind); it != kind_shape.end())
    shape = it->second;
  else
    shape = container ? "rect" : "rounded";
  std::vector<std::string> parts{shape_style(shape), "whiteSpace=wrap", "html=1"};
  static const std::map<std::string, std::string, std::less<>> map{
      {"fill", "fillColor"},         {"stroke", "strokeColor"},        {"fontColor", "fontColor"},
      {"fontSize", "fontSize"},      {"fontStyle", "fontStyle"},       {"strokeWidth", "strokeWidth"},
      {"dashed", "dashed"},          {"dashPattern", "dashPattern"},   {"opacity", "opacity"},
      {"align", "align"},            {"verticalAlign", "verticalAlign"}, {"labelPosition", "labelPosition"},
      {"verticalLabelPosition", "verticalLabelPosition"}, {"size", "size"},
      {"direction", "direction"},   {"tabWidth", "tabWidth"},           {"tabHeight", "tabHeight"},
      {"tabPosition", "tabPosition"}, {"jettyWidth", "jettyWidth"},     {"jettyHeight", "jettyHeight"},
      {"spacingTop", "spacingTop"}, {"spacingLeft", "spacingLeft"},     {"spacingRight", "spacingRight"},
      {"spacingBottom", "spacingBottom"}, {"boundedLbl", "boundedLbl"}, {"participant", "participant"},
      {"lifelineDashed", "lifelineDashed"}};
  Style look = rule_style(rules, e);
  if (e.style)
    for (const auto& [k, v] : *e.style) look.set(k, v);
  for (const auto& [k, v] : look)
    if (auto it = map.find(k); it != map.end()) parts.push_back(it->second + "=" + v);
  if (container) {
    parts.emplace_back("container=1");
    parts.emplace_back("verticalAlign=top");
  }
  std::string out;
  for (const auto& p : parts) out += p + ";";
  return out;
}

std::string relationship_style(const Relationship& r, const std::vector<StyleRule>& rules) {
  const Style from_rules = rule_style(rules, r);
  auto st = [&](std::string_view k) -> const std::string* {
    const std::string* v = r.style ? r.style->find(k) : nullptr;
    return v ? v : from_rules.find(k);
  };
  const std::string* rounded = st("rounded");
  std::vector<std::string> parts{"edgeStyle=" + (st("edgeStyle") ? *st("edgeStyle") : std::string("orthogonalEdgeStyle")),
                                 std::string("rounded=") + (rounded && *rounded == "1" ? "1" : "0"),
                                 "html=1"};
  if (st("endArrow") && !st("endArrow")->empty()) parts.push_back("endArrow=" + *st("endArrow"));
  if (st("startArrow") && !st("startArrow")->empty())
    parts.push_back("startArrow=" + *st("startArrow"));
  else if (r.bidirectional)
    parts.emplace_back("startArrow=classic");
  // (st.dashed ?? (r.line === 'dashed' ? '1' : undefined)) === '1'
  bool dashed = st("dashed") ? *st("dashed") == "1" : r.line == LineStyle::Dashed;
  if (dashed) parts.emplace_back("dashed=1");
  const std::string* pattern = st("dashPattern");
  if (dashed && pattern && !pattern->empty()) parts.push_back("dashPattern=" + *pattern);
  const std::string* curved = st("curved");
  if (curved && *curved == "1") parts.emplace_back("curved=1");
  if (st("stroke") && !st("stroke")->empty()) parts.push_back("strokeColor=" + *st("stroke"));
  if (st("strokeWidth") && !st("strokeWidth")->empty()) parts.push_back("strokeWidth=" + *st("strokeWidth"));
  // The label's placement keys are the relationship's own, as the layout reads them.
  for (const char* k : {"align", "verticalAlign"})
    if (const std::string* v = r.style ? r.style->find(k) : nullptr; v && !v->empty())
      parts.push_back(std::string(k) + "=" + *v);
  std::string out;
  for (const auto& p : parts) out += p + ";";
  return out;
}

/// Whether a draw.io style routes the edge as a sequence message: its last edgeStyle, as draw.io
/// reads a repeated key, is sequenceEdgeStyle.
bool sequence_message(std::string_view style) {
  std::string_view last;
  for (std::size_t start = 0; start <= style.size();) {
    std::size_t end = style.find(';', start);
    if (end == std::string_view::npos) end = style.size();
    std::string_view part = style.substr(start, end - start);
    if (part.starts_with("edgeStyle=")) last = part.substr(10);
    start = end + 1;
  }
  return last == "sequenceEdgeStyle";
}

bool is_bookkeeping(std::string_view k) { return k.rfind("drawio.", 0) == 0; }

}  // namespace

std::string export_drawio(const Model& model, const std::vector<DrawioPage>& pages) {
  std::vector<std::string> out{"<mxfile host=\"DiaDraw\" type=\"device\">"};
  for (std::size_t index = 0; index < pages.size(); ++index) {
    const DrawioPage& page = pages[index];
    const View* view = model.views.find(page.view);
    out.push_back("  <diagram id=\"page-" + std::to_string(index + 1) + "\" name=\"" + attr(view ? view->id : page.view) +
                  "\">");
    out.emplace_back("    <mxGraphModel><root>");
    out.emplace_back("      <mxCell id=\"0\"/>");
    out.emplace_back("      <mxCell id=\"1\" parent=\"0\"/>");

    auto visible = [&](const std::string& id) { return page.nodes.contains(id) && model.elements.contains(id); };
    std::set<std::string, std::less<>> anchors;
    for (const auto& e : model.elements)
      if (const std::string* a = e.properties.find("drawio.anchor"); a && *a == "1") anchors.insert(e.id);
    std::map<std::string, std::string, std::less<>> cell_ids;
    std::set<std::string, std::less<>> taken{"0", "1"};
    auto cell_id = [&](const std::string& id, const std::string* preferred) {
      std::string c = preferred && !preferred->empty() && !taken.count(*preferred) ? *preferred : id;
      for (int n = 2; taken.count(c); ++n) c = id + "-" + std::to_string(n);
      taken.insert(c);
      cell_ids[id] = c;
      return c;
    };
    std::set<std::string, std::less<>> parents;
    for (const auto& e : model.elements)
      if (e.parent) parents.insert(*e.parent);

    // Parents before children, so draw.io sees containers first.
    std::function<int(const Element&, int)> depth = [&](const Element& e, int guard) -> int {
      if (!e.parent || !visible(*e.parent) || guard > 10000) return 0;
      return 1 + depth(*model.elements.find(*e.parent), guard + 1);
    };
    std::vector<const Element*> elements;
    for (const auto& e : model.elements)
      if (visible(e.id) && !anchors.count(e.id)) elements.push_back(&e);
    std::stable_sort(elements.begin(), elements.end(),
                     [&](const Element* a, const Element* b) { return depth(*a, 0) < depth(*b, 0); });
    for (const Element* ep : elements) {
      const Element& e = *ep;
      const Box& box = *page.nodes.find(e.id);
      std::optional<std::string> parent = e.parent && visible(*e.parent) ? e.parent : std::nullopt;
      Box origin = parent ? *page.nodes.find(*parent) : Box{};
      std::string id = cell_id(e.id, e.properties.find("drawio.id"));
      std::string parent_cell = parent ? cell_ids[*parent] : "1";
      const std::string* style_prop = e.properties.find("drawio.style");
      std::string style = style_prop ? *style_prop : element_style(e, parents.count(e.id) > 0, model.styles);
      const std::string* label_prop = e.properties.find("drawio.label");
      std::string label = label_prop ? *label_prop : e.name;
      std::string geometry = "<mxGeometry x=\"" + num(box.x - origin.x) + "\" y=\"" + num(box.y - origin.y) +
                             "\" width=\"" + num(box.width) + "\" height=\"" + num(box.height) + "\" as=\"geometry\"/>";
      // An adornment goes back as draw.io's relative geometry: a spot on its parent and an offset.
      if (auto anchor = parent ? anchor_of(e) : std::nullopt)
        geometry = "<mxGeometry x=\"" + num(anchor->at.x) + "\" y=\"" + num(anchor->at.y) + "\" width=\"" +
                   num(box.width) + "\" height=\"" + num(box.height) + "\" relative=\"1\" as=\"geometry\"><mxPoint x=\"" +
                   num(anchor->offset.x) + "\" y=\"" + num(anchor->offset.y) + "\" as=\"offset\"/></mxGeometry>";
      std::string extra;
      for (const auto& [k, v] : e.properties)
        if (!is_bookkeeping(k)) extra += " " + data_key(k) + "=\"" + attr(v) + "\"";
      if (!extra.empty())
        out.push_back("      <object label=\"" + attr(label) + "\" id=\"" + attr(id) + "\"" + extra + "><mxCell style=\"" +
                      attr(style) + "\" vertex=\"1\" parent=\"" + attr(parent_cell) + "\">" + geometry + "</mxCell></object>");
      else
        out.push_back("      <mxCell id=\"" + attr(id) + "\" value=\"" + attr(label) + "\" style=\"" + attr(style) +
                      "\" vertex=\"1\" parent=\"" + attr(parent_cell) + "\">" + geometry + "</mxCell>");
    }

    std::map<std::string, const std::vector<Point2>*, std::less<>> routes;
    if (view)
      for (const auto& h : view->hints)
        if (const auto* r = std::get_if<RouteHint>(&h)) routes[r->relationship] = &r->points;
    for (const auto& r : model.relationships) {
      const EdgeRoute* route = page.edges.find(r.id);
      if (!route) continue;
      if (!visible(r.source.element) || !visible(r.target.element)) continue;
      std::string id = cell_id(r.id, r.properties.find("drawio.id"));
      const std::string* style_prop = r.properties.find("drawio.style");
      std::string style = style_prop ? *style_prop : relationship_style(r, model.styles);
      std::string array;
      auto add_point = [&](double x, double y) { array += "<mxPoint x=\"" + num(x) + "\" y=\"" + num(y) + "\"/>"; };
      if (auto it = routes.find(r.id); it != routes.end()) {
        for (const auto& p : *it->second) add_point(p.x, p.y);
      } else if (route->points.size() > 2) {
        for (std::size_t i = 1; i + 1 < route->points.size(); ++i) add_point(route->points[i].x, route->points[i].y);
      }
      // draw.io keeps a straight sequence message's y in its one waypoint.
      if (array.empty() && route->points.size() == 2 && sequence_message(style))
        add_point((route->points[0].x + route->points[1].x) / 2, route->points[0].y);
      if (!array.empty()) array = "<Array as=\"points\">" + array + "</Array>";
      // Loose ends (anchors) are written back as points, not as connections.
      auto term = [&](const std::string& end, const char* as) -> std::string {
        if (anchors.count(end)) return "";
        auto it = cell_ids.find(end);
        return std::string(" ") + as + "=\"" + attr(it == cell_ids.end() ? end : it->second) + "\"";
      };
      auto loose = [&](const std::string& end, const char* as) -> std::string {
        if (!anchors.count(end)) return "";
        const Box& b = *page.nodes.find(end);
        return "<mxPoint x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" as=\"" + as + "\"/>";
      };
      // The label's spot on the line: draw.io's relative x and y and its offset.
      std::string label_at, label_offset;
      if (auto anchor = label_anchor_of(r.style)) {
        if (anchor->along != 0) label_at += " x=\"" + num(anchor->along) + "\"";
        if (anchor->away != 0) label_at += " y=\"" + num(anchor->away) + "\"";
        if (anchor->offset.x != 0 || anchor->offset.y != 0)
          label_offset = "<mxPoint x=\"" + num(anchor->offset.x) + "\" y=\"" + num(anchor->offset.y) + "\" as=\"offset\"/>";
      }
      std::string geometry = "<mxGeometry" + label_at + " relative=\"1\" as=\"geometry\">" + loose(r.source.element, "sourcePoint") +
                             loose(r.target.element, "targetPoint") + array + label_offset + "</mxGeometry>";
      std::string extra;
      for (const auto& [k, v] : r.properties)
        if (!is_bookkeeping(k)) extra += " " + data_key(k) + "=\"" + attr(v) + "\"";
      std::string head = "style=\"" + attr(style) + "\" edge=\"1\" parent=\"1\"" + term(r.source.element, "source") +
                         term(r.target.element, "target");
      std::string label = r.label.value_or("");
      if (!extra.empty())
        out.push_back("      <object label=\"" + attr(label) + "\" id=\"" + attr(id) + "\"" + extra + "><mxCell " + head + ">" +
                      geometry + "</mxCell></object>");
      else
        out.push_back("      <mxCell id=\"" + attr(id) + "\" value=\"" + attr(label) + "\" " + head + ">" + geometry +
                      "</mxCell>");
    }
    out.emplace_back("    </root></mxGraphModel>");
    out.emplace_back("  </diagram>");
  }
  out.emplace_back("</mxfile>");
  std::string text;
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (i) text.push_back('\n');
    text += out[i];
  }
  return text + "\n";
}

}  // namespace dd::io
