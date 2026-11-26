#include "diadraw/canvas/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <unordered_set>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::canvas {

namespace {

constexpr double kDragThreshold = 4;  ///< view pixels before a press becomes a move
constexpr double kHandleRadius = 7;
constexpr double kGrip = 4.5;        ///< half the side of a resize handle, half a bend handle's width
constexpr double kMinSize = 10;      ///< the smallest box a resize leaves
constexpr double kStraighten = 6;    ///< view pixels: a bend this near its neighbours' line goes
constexpr double kEndRadius = 6;     ///< the handles on the ends of a selected line
constexpr double kEdgeTolerance = 5;  ///< view pixels
constexpr double kRotateReach = 17;   ///< from a node's top-right corner to its rotate handle (view pixels when zoomed out)
constexpr double kSnapStep = 15;      ///< degrees a turn snaps to…
constexpr double kSnapWithin = 5;     ///< …when it is this close
constexpr draw::Rgb kAccent{0.145, 0.388, 0.922};  // #2563eb
constexpr double kBezier = 0.5522847498;

bool inside(const Box& b, draw::Point p) {
  return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

Point mid(const Box& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

/// Whether `p` is in `b` turned `degrees` about its centre.
bool inside_turned(const Box& b, double degrees, draw::Point p) {
  Point q = dd::rotate_point({p.x, p.y}, mid(b), -degrees);
  return inside(b, {q.x, q.y});
}

double distance_to_segment(draw::Point p, Point a, Point b) {
  double dx = b.x - a.x, dy = b.y - a.y;
  double len = dx * dx + dy * dy;
  double t = len == 0 ? 0 : std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0, 1.0);
  double x = a.x + t * dx - p.x, y = a.y + t * dy - p.y;
  return std::sqrt(x * x + y * y);
}

std::string trim(const std::string& s) {
  auto space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
  std::size_t a = 0, b = s.size();
  while (a < b && space(s[a])) ++a;
  while (b > a && space(s[b - 1])) --b;
  return s.substr(a, b - a);
}

// ── Overlay shapes ──

std::vector<draw::Segment> rounded_rect(double x, double y, double w, double h, double r) {
  r = std::min({r, w / 2, h / 2});
  double k = r * kBezier;
  using S = draw::Segment;
  return {
      S{S::Move, {{x + r, y}}},
      S{S::Line, {{x + w - r, y}}},
      S{S::Cubic, {{x + w - r + k, y}, {x + w, y + r - k}, {x + w, y + r}}},
      S{S::Line, {{x + w, y + h - r}}},
      S{S::Cubic, {{x + w, y + h - r + k}, {x + w - r + k, y + h}, {x + w - r, y + h}}},
      S{S::Line, {{x + r, y + h}}},
      S{S::Cubic, {{x + r - k, y + h}, {x, y + h - r + k}, {x, y + h - r}}},
      S{S::Line, {{x, y + r}}},
      S{S::Cubic, {{x, y + r - k}, {x + r - k, y}, {x + r, y}}},
      S{S::Close, {}},
  };
}

/// A path turned `degrees` about `about` (an affine map keeps Bézier curves).
std::vector<draw::Segment> turn_path(std::vector<draw::Segment> path, Point about, double degrees) {
  if (degrees == 0) return path;
  for (auto& seg : path)
    for (auto& pt : seg.p) {
      Point q = dd::rotate_point({pt.x, pt.y}, about, degrees);
      pt = {q.x, q.y};
    }
  return path;
}

std::vector<draw::Segment> ellipse(const Box& b) {
  return rounded_rect(b.x, b.y, b.width, b.height, std::min(b.width, b.height) / 2);
}

std::vector<draw::Segment> polyline(const std::vector<Point>& points) {
  std::vector<draw::Segment> out;
  for (std::size_t i = 0; i < points.size(); ++i)
    out.push_back({i ? draw::Segment::Line : draw::Segment::Move, {{points[i].x, points[i].y}}});
  return out;
}

draw::Item stroke(std::vector<draw::Segment> path, double width, double alpha, std::vector<double> dash = {},
                  std::optional<draw::Rgb> fill = std::nullopt, double fill_alpha = 1) {
  draw::Item item;
  item.kind = draw::Item::Path;
  item.path = std::move(path);
  item.paint.stroke = kAccent;
  item.paint.stroke_width = width;
  item.paint.stroke_alpha = alpha;
  item.paint.dash = std::move(dash);
  item.paint.fill = fill;
  item.paint.fill_alpha = fill_alpha;
  item.paint.join = draw::LineJoin::Round;
  item.paint.cap = draw::LineCap::Round;
  return item;
}

/// `base` with the ids of `add` it lacks appended after its own (each holds every id once).
editor::Selection united(const editor::Selection& base, editor::Selection add) {
  editor::Selection out = base;
  // The lookups go over `base`, which nothing appends to, so the views stay valid.
  auto append = [](std::vector<std::string>& into, const std::vector<std::string>& have, std::vector<std::string>& from) {
    std::unordered_set<std::string_view> known(have.begin(), have.end());
    for (auto& id : from)
      if (!known.count(id)) into.push_back(std::move(id));
  };
  append(out.elements, base.elements, add.elements);
  append(out.relationships, base.relationships, add.relationships);
  return out;
}

}  // namespace

std::string describe_hint(const LayoutHint& hint, const std::function<std::string(const std::string&)>& name) {
  if (const auto* pin = std::get_if<PinHint>(&hint)) {
    const char* where = pin->placement == Placement::Below   ? "below"
                        : pin->placement == Placement::Above ? "above"
                        : pin->placement == Placement::LeftOf ? "left of"
                                                              : "right of";
    return std::string(where) + " " + name(pin->reference);
  }
  if (const auto* at = std::get_if<PinAtHint>(&hint)) return "at " + js_number_string(at->x) + ", " + js_number_string(at->y);
  if (const auto* align = std::get_if<AlignHint>(&hint))
    return std::string("align ") + (align->axis == Axis::Horizontal ? "with row of " : "with column of ") +
           (align->elements.empty() ? "" : name(align->elements[0]));
  if (const auto* order = std::get_if<OrderHint>(&hint)) {
    std::string out = "order ";
    for (std::size_t i = 0; i < order->elements.size(); ++i) out += (i ? ", " : "") + name(order->elements[i]);
    return out;
  }
  if (const auto* route = std::get_if<RouteHint>(&hint)) return "route " + route->relationship;
  return "";
}

// ── Content ──

void Canvas::show(const editor::ViewRender& render) {
  std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + js_number_string(render.scene.width) +
                    "\" height=\"" + js_number_string(render.scene.height) + "\" viewBox=\"" + render.scene.view_box +
                    "\">" + render.scene.content + "</svg>";
  list_ = draw::display_list_from_svg(svg);
  // The renderer paints the background first, a rect that belongs to no diagram item; with the
  // grid shown it goes under the grid instead.
  background_.reset();
  if (list_) {
    const auto& items = list_->items;
    auto first = std::find_if(items.begin(), items.end(), [](const draw::Item& i) { return i.kind == draw::Item::Path; });
    if (first != items.end() && first->owner.empty() && first->paint.fill) background_ = *first;
  }
  split_background();
  geometry_ = render.geometry;
  elements_.clear();
  containers_.clear();
  fixed_ = render.view.view.layout.algorithm == "fixed";
  sequence_ = render.view.view.layout.algorithm == "sequence";
  ends_.clear();
  for (const auto& e : render.view.elements) {
    elements_.push_back(e.id);
    if (e.parent) containers_.push_back(*e.parent);
  }
  for (const auto& r : render.view.relationships)
    if (!r.implied) ends_[r.id] = {r.source.element, r.target.element};
  editor::Selection kept;
  for (const auto& id : selection_.elements)
    if (geometry_.nodes.contains(id)) kept.elements.push_back(id);
  for (const auto& id : selection_.relationships)
    if (geometry_.edges.contains(id)) kept.relationships.push_back(id);
  bool changed = kept.elements.size() != selection_.elements.size() ||
                 kept.relationships.size() != selection_.relationships.size();
  selection_ = kept;
  gesture_ = {};
  if (changed && callbacks.selected) callbacks.selected(selection_);
}

void Canvas::clear() {
  list_.reset();
  bare_.reset();
  background_.reset();
  geometry_ = {};
  fixed_ = false;
  sequence_ = false;
  elements_.clear();
  containers_.clear();
  ends_.clear();
  gesture_ = {};
}

void Canvas::set_zoom(double zoom) { zoom_ = std::clamp(zoom, 0.1, 8.0); }
double Canvas::width() const { return list_ ? list_->width * zoom_ : 0; }
double Canvas::height() const { return list_ ? list_->height * zoom_ : 0; }

void Canvas::select(editor::Selection selection) {
  selection_ = std::move(selection);
  if (callbacks.selected) callbacks.selected(selection_);
}

// ── Geometry ──

draw::Point Canvas::to_user(double x, double y) const {
  if (!list_) return {};
  const auto& r = list_->root;
  return {(x / zoom_ - r.e) / r.a, (y / zoom_ - r.f) / r.d};
}

draw::Point Canvas::to_view(double x, double y) const {
  if (!list_) return {};
  const auto& r = list_->root;
  return {(x * r.a + r.e) * zoom_, (y * r.d + r.f) * zoom_};
}

double Canvas::snap(double v) const { return grid_ > 0 ? js_round(v / grid_) * grid_ : v; }

void Canvas::set_grid(double step) {
  grid_ = std::isfinite(step) && step > 0 ? step : 0;
  split_background();
}

void Canvas::split_background() {
  bare_.reset();
  if (!list_ || !background_ || grid_ <= 0) return;
  bare_ = list_;
  auto& items = bare_->items;
  // `show()` took the background from the list's first path.
  auto first = std::find_if(items.begin(), items.end(), [](const draw::Item& i) { return i.kind == draw::Item::Path; });
  if (first != items.end()) items.erase(first);
}

double Canvas::units_per_pixel() const { return list_ ? 1 / (list_->root.a * zoom_) : 1; }

std::optional<std::string> Canvas::node_at(draw::Point p, bool leaves_only) const {
  std::optional<std::string> best;
  double best_area = 0;
  bool best_leaf = false;
  for (const auto& id : elements_) {
    const Box* b = geometry_.nodes.find(id);
    if (!b || !inside_turned(*b, rotation_of(id), p)) continue;
    bool leaf = std::find(containers_.begin(), containers_.end(), id) == containers_.end();
    if (leaves_only && !leaf) continue;
    double area = b->width * b->height;
    // The smallest leaf, else the smallest container.
    if (!best || (leaf && !best_leaf) || (leaf == best_leaf && area < best_area)) {
      best = id;
      best_area = area;
      best_leaf = leaf;
    }
  }
  return best;
}

std::optional<std::string> Canvas::edge_at(draw::Point p) const {
  double tolerance = kEdgeTolerance * units_per_pixel();
  for (const auto& id : geometry_.edges.keys()) {
    const auto& pts = geometry_.edges.find(id)->points;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
      if (distance_to_segment(p, pts[i], pts[i + 1]) <= tolerance) return id;
  }
  return std::nullopt;
}

std::pair<std::optional<std::string>, std::optional<std::string>> Canvas::hit(draw::Point p) const {
  auto node = node_at(p, false);
  auto edge = edge_at(p);
  // A line drawn across a container is on top of it: the line wins, a leaf node still wins.
  bool container = node && std::find(containers_.begin(), containers_.end(), *node) != containers_.end();
  if (node && edge && !container) edge.reset();
  if (edge) node.reset();
  return {node, edge};
}

std::optional<Box> Canvas::handle_box() const {
  if (selection_.elements.size() != 1) return std::nullopt;
  const Box* found = geometry_.nodes.find(selection_.elements[0]);
  if (!found) return std::nullopt;
  // Just off the middle of the right edge, turned with the node, so it stays clear of the corner
  // grips and the rotate handle whatever the turn.
  const Box* b = found;
  Point c = dd::rotate_point({b->x + b->width + 10, b->y + b->height / 2}, mid(*b), rotation_of(selection_.elements[0]));
  return Box{c.x - kHandleRadius, c.y - kHandleRadius, 2 * kHandleRadius, 2 * kHandleRadius};
}

const EdgeRoute* Canvas::reconnectable() const {
  if (selection_.relationships.size() != 1 || !selection_.elements.empty()) return nullptr;
  const auto& id = selection_.relationships[0];
  if (!ends_.contains(id)) return nullptr;
  const EdgeRoute* route = geometry_.edges.find(id);
  return route && route->points.size() >= 2 ? route : nullptr;
}

std::optional<Box> Canvas::end_box(editor::End end) const {
  const EdgeRoute* route = reconnectable();
  if (!route) return std::nullopt;
  const Point& p = end == editor::End::Source ? route->points.front() : route->points.back();
  double r = kEndRadius;
  return Box{p.x - r, p.y - r, 2 * r, 2 * r};
}

std::optional<std::string> Canvas::reconnect_target(draw::Point p) const {
  auto found = ends_.find(gesture_.id);
  if (found == ends_.end()) return std::nullopt;
  const auto& [source, target] = found->second;
  auto node = node_at(p, false);
  if (!node || *node == source || *node == target) return std::nullopt;
  return node;
}

std::optional<Box> Canvas::moved_box() const {
  if (gesture_.kind != GestureKind::Move) return std::nullopt;
  const Box* b = geometry_.nodes.find(gesture_.id);
  if (!b) return std::nullopt;
  double x = b->x + gesture_.current.x - gesture_.start.x;
  double y = b->y + gesture_.current.y - gesture_.start.y;
  // A drop placed by its neighbours goes where the layout puts it, so only a plain one snaps.
  if (gesture_.freeform) {
    x = snap(x);
    y = snap(y);
  }
  return Box{x, y, b->width, b->height};
}

bool Canvas::grouped() const {
  if (gesture_.kind != GestureKind::Move || selection_.elements.size() < 2) return false;
  const auto& selected = selection_.elements;
  return std::find(selected.begin(), selected.end(), gesture_.id) != selected.end();
}

std::optional<Box> Canvas::band() const {
  if (gesture_.kind != GestureKind::Band || !gesture_.moved) return std::nullopt;
  const auto& a = gesture_.start;
  const auto& b = gesture_.current;
  return Box{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

editor::Selection Canvas::enclosed(draw::Point a, draw::Point b) const {
  Box area{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
  auto within = [&](const Point& p) { return inside(area, {p.x, p.y}); };
  editor::Selection out;
  for (const auto& id : elements_) {
    const Box* box = geometry_.nodes.find(id);
    if (!box) continue;
    Box r = rotated_bounds(*box, rotation_of(id));
    if (within({r.x, r.y}) && within({r.x + r.width, r.y + r.height})) out.elements.push_back(id);
  }
  for (const auto& entry : ends_) {
    const EdgeRoute* route = geometry_.edges.find(entry.first);
    if (route && !route->points.empty() && std::all_of(route->points.begin(), route->points.end(), within))
      out.relationships.push_back(entry.first);
  }
  return out;
}

double Canvas::rotation_of(const std::string& element) const {
  const double* d = geometry_.rotations.find(element);
  return d ? *d : 0;
}

std::optional<std::string> Canvas::resizable() const {
  if (selection_.elements.size() != 1 || !selection_.relationships.empty()) return std::nullopt;
  const auto& id = selection_.elements[0];
  if (!geometry_.nodes.contains(id)) return std::nullopt;
  // A layout sizes containers around their children; only a fixed view takes theirs as written.
  bool container = std::find(containers_.begin(), containers_.end(), id) != containers_.end();
  if (container && !fixed_) return std::nullopt;
  return id;
}

std::optional<std::string> Canvas::rotatable() const {
  if (sequence_ || selection_.elements.size() != 1 || !selection_.relationships.empty()) return std::nullopt;
  const auto& id = selection_.elements[0];
  if (!geometry_.nodes.contains(id)) return std::nullopt;
  if (std::find(containers_.begin(), containers_.end(), id) != containers_.end()) return std::nullopt;
  return id;
}

std::optional<Point> Canvas::rotate_handle() const {
  auto id = rotatable();
  if (!id) return std::nullopt;
  const Box& b = *geometry_.nodes.find(*id);
  // Off the top-right corner, diagonally: clear of the corner's drawn resize handle and of lines
  // that come in at the middle of an edge. The two press areas touch at their far corners, outside
  // the drawn circle; `press` tests the corners first, so there a resize wins.
  double reach = kRotateReach * std::max(1.0, units_per_pixel()) / std::sqrt(2.0);
  return dd::rotate_point({b.x + b.width + reach, b.y - reach}, mid(b), rotation_of(*id));
}

std::optional<double> Canvas::turned() const {
  if (gesture_.kind != GestureKind::Rotate || !gesture_.moved) return std::nullopt;
  const Box* b = geometry_.nodes.find(gesture_.id);
  if (!b) return std::nullopt;
  Point c = mid(*b);
  auto angle = [&](draw::Point p) { return std::atan2(p.y - c.y, p.x - c.x) * 180 / 3.14159265358979323846; };
  if (gesture_.current.x == c.x && gesture_.current.y == c.y) return std::nullopt;
  // The node turns as far as the pointer turned about its centre since the press.
  double d = std::fmod(rotation_of(gesture_.id) + angle(gesture_.current) - angle(gesture_.start), 360);
  if (d > 180) d -= 360;
  if (d <= -180) d += 360;
  double snapped = js_round(d / kSnapStep) * kSnapStep;
  d = std::abs(d - snapped) <= kSnapWithin ? snapped : js_round(d);
  if (d <= -180) d += 360;
  return d == 0 ? 0.0 : d;  // no -0
}

std::optional<std::string> Canvas::bendable() const {
  if (!fixed_ || selection_.relationships.size() != 1 || !selection_.elements.empty()) return std::nullopt;
  const EdgeRoute* route = geometry_.edges.find(selection_.relationships[0]);
  if (!route || route->points.size() < 2) return std::nullopt;
  return selection_.relationships[0];
}

namespace {

/// The corners of a box, clockwise from the top left.
std::vector<Point> corners(const Box& b) {
  return {{b.x, b.y}, {b.x + b.width, b.y}, {b.x + b.width, b.y + b.height}, {b.x, b.y + b.height}};
}

/// Bend points (the route's inner points), then the middle of each segment.
std::vector<Point> bend_handles(const std::vector<Point>& route) {
  std::vector<Point> out(route.begin() + 1, route.end() - 1);
  for (std::size_t i = 0; i + 1 < route.size(); ++i)
    out.push_back({(route[i].x + route[i + 1].x) / 2, (route[i].y + route[i + 1].y) / 2});
  return out;
}

}  // namespace

std::optional<Box> Canvas::resized_box() const {
  if (gesture_.kind != GestureKind::Resize || !gesture_.moved) return std::nullopt;
  const Box* b = geometry_.nodes.find(gesture_.id);
  if (!b) return std::nullopt;
  double degrees = rotation_of(gesture_.id);
  // The opposite corner stays; the held one follows the pointer. On a turned node this happens in
  // the node's own frame: the pointer's move is turned back first.
  Point fixed = corners(*b)[(gesture_.handle + 2) % 4];
  Point held = corners(*b)[gesture_.handle];
  Point delta = dd::rotate_point({gesture_.current.x - gesture_.start.x, gesture_.current.y - gesture_.start.y}, {0, 0},
                             -degrees);
  double x = held.x + delta.x;
  double y = held.y + delta.y;
  bool left = gesture_.handle == 0 || gesture_.handle == 3;
  bool top = gesture_.handle == 0 || gesture_.handle == 1;
  if (gesture_.keep_aspect && b->width > 0 && b->height > 0) {
    // Both sides scale by one factor: the larger of the two the pointer asks for, so the box
    // always reaches the pointer, and at least the smallest size. Off the grid's lines, since a
    // grid step rarely keeps a ratio.
    double sx = (left ? fixed.x - x : x - fixed.x) / b->width;
    double sy = (top ? fixed.y - y : y - fixed.y) / b->height;
    double s = std::max({sx, sy, kMinSize / b->width, kMinSize / b->height});
    x = fixed.x + (left ? -b->width : b->width) * s;
    y = fixed.y + (top ? -b->height : b->height) * s;
  } else if (grid_ > 0) {
    if (fixed_ && degrees == 0) {
      // The corner is written where it is: on a grid point, the nearest one at least the smallest
      // size away from the fixed corner (which may itself be off the grid).
      x = left ? std::min(snap(x), std::floor((fixed.x - kMinSize) / grid_) * grid_)
               : std::max(snap(x), std::ceil((fixed.x + kMinSize) / grid_) * grid_);
      y = top ? std::min(snap(y), std::floor((fixed.y - kMinSize) / grid_) * grid_)
              : std::max(snap(y), std::ceil((fixed.y + kMinSize) / grid_) * grid_);
    } else {
      // Only the size is written (a layout places the node), or the corner is turned off the
      // grid's lines: the sides become whole grid steps, at least the smallest size.
      double least = std::ceil(kMinSize / grid_) * grid_;
      double w = std::max(least, snap(left ? fixed.x - x : x - fixed.x));
      double h = std::max(least, snap(top ? fixed.y - y : y - fixed.y));
      x = fixed.x + (left ? -w : w);
      y = fixed.y + (top ? -h : h);
    }
  } else {
    x = left ? std::min(x, fixed.x - kMinSize) : std::max(x, fixed.x + kMinSize);
    y = top ? std::min(y, fixed.y - kMinSize) : std::max(y, fixed.y + kMinSize);
  }
  Box out{std::min(x, fixed.x), std::min(y, fixed.y), std::abs(x - fixed.x), std::abs(y - fixed.y)};
  if (degrees != 0) {
    // Turned about its new centre, the box must keep the fixed corner where it is on screen.
    Point was = dd::rotate_point(fixed, mid(*b), degrees);
    Point now = dd::rotate_point(fixed, mid(out), degrees);
    out.x += was.x - now.x;
    out.y += was.y - now.y;
  }
  return out;
}

std::optional<std::vector<Point>> Canvas::bent_route() const {
  if (gesture_.kind != GestureKind::Bend || !gesture_.moved) return std::nullopt;
  const EdgeRoute* route = geometry_.edges.find(gesture_.id);
  if (!route) return std::nullopt;
  std::vector<Point> points = route->points;
  Point at{snap(gesture_.current.x), snap(gesture_.current.y)};
  std::size_t i = gesture_.insert ? gesture_.handle + 1 : gesture_.handle;
  if (gesture_.insert) points.insert(points.begin() + static_cast<std::ptrdiff_t>(i), at);
  else if (i > 0 && i + 1 < points.size()) points[i] = at;
  else return std::nullopt;
  // A bend dragged onto the straight line between its neighbours is not a bend any more.
  if (distance_to_segment({at.x, at.y}, points[i - 1], points[i + 1]) <= kStraighten * units_per_pixel())
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
  return points;
}

std::vector<draw::Point> Canvas::resize_points() const {
  std::vector<draw::Point> out;
  if (auto id = resizable()) {
    const Box& b = *geometry_.nodes.find(*id);
    for (const auto& c : corners(b)) {
      Point q = dd::rotate_point(c, mid(b), rotation_of(*id));
      out.push_back(to_view(q.x, q.y));
    }
  }
  return out;
}

std::optional<draw::Point> Canvas::rotate_point() const {
  auto h = rotate_handle();
  if (!h) return std::nullopt;
  return to_view(h->x, h->y);
}

std::vector<draw::Point> Canvas::bend_points() const {
  std::vector<draw::Point> out;
  if (auto id = bendable())
    for (const auto& p : bend_handles(geometry_.edges.find(*id)->points)) out.push_back(to_view(p.x, p.y));
  return out;
}

std::optional<draw::Point> Canvas::center_of(const std::string& element) const {
  const Box* b = geometry_.nodes.find(element);
  if (!b || !list_) return std::nullopt;
  return to_view(b->x + b->width / 2, b->y + b->height / 2);
}

std::optional<draw::Point> Canvas::handle_point() const {
  auto h = handle_box();
  if (!h) return std::nullopt;
  return to_view(h->x + h->width / 2, h->y + h->height / 2);
}

std::optional<draw::Point> Canvas::end_point(editor::End end) const {
  auto b = end_box(end);
  if (!b) return std::nullopt;
  return to_view(b->x + b->width / 2, b->y + b->height / 2);
}

std::optional<draw::Point> Canvas::label_point(const std::string& relationship) const {
  const EdgeRoute* route = geometry_.edges.find(relationship);
  if (!route || route->points.size() < 2 || !list_) return std::nullopt;
  std::size_t i = (route->points.size() - 1) / 2;
  const auto &a = route->points[i], &b = route->points[i + 1];
  return to_view((a.x + b.x) / 2, (a.y + b.y) / 2);
}

// ── Events ──

std::optional<Edit> Canvas::edit_at(draw::Point p) const {
  Edit edit;
  Box anchor;
  auto [node, e] = hit(p);
  if (node) {
    anchor = *geometry_.nodes.find(*node);
    edit.id = *node;
    edit.value = callbacks.name_of ? callbacks.name_of(*node) : "";
  } else if (e) {
    const auto& route = *geometry_.edges.find(*e);
    if (route.label) {
      anchor = *route.label;
    } else if (route.points.size() >= 2) {
      std::size_t i = (route.points.size() - 1) / 2;
      const auto &a = route.points[i], &b = route.points[i + 1];
      anchor = {(a.x + b.x) / 2 - 40, (a.y + b.y) / 2 - 10, 80, 20};
    } else {
      return std::nullopt;
    }
    edit.id = *e;
    edit.relationship = true;
    edit.value = callbacks.label_of ? callbacks.label_of(*e) : "";
  } else {
    return std::nullopt;
  }
  auto from = to_view(anchor.x, anchor.y);
  auto to = to_view(anchor.x + anchor.width, anchor.y + anchor.height);
  edit.x = from.x;
  edit.y = from.y;
  edit.width = to.x - from.x;
  edit.height = to.y - from.y;
  return edit;
}

bool Canvas::press(double x, double y, int clicks, Modifiers modifiers, std::optional<Edit>* edit) {
  if (!list_) return false;
  draw::Point p = to_user(x, y);
  double grip = (kGrip + 2) * std::max(1.0, units_per_pixel());
  auto near = [&](const Point& c) { return std::abs(p.x - c.x) <= grip && std::abs(p.y - c.y) <= grip; };
  // The resize handle under the press, if any.
  std::optional<std::size_t> corner;
  auto resizing = resizable();
  if (resizing) {
    const Box& b = *geometry_.nodes.find(*resizing);
    auto cs = corners(b);
    for (std::size_t i = 0; i < cs.size() && !corner; ++i)
      if (near(dd::rotate_point(cs[i], mid(b), rotation_of(*resizing)))) corner = i;
  }
  if (clicks >= 2) {
    gesture_ = {};
    // A double-click on a resize handle sizes the node to fit its content. Only when its first
    // click took that handle: a double-click on an unselected node renames it, even near a corner.
    // The first click is the last single press, or the one before it when the shell sends the
    // second click as a single press too.
    const CornerPress first = std::move(double_click_repeats_press_ ? previous_corner_press_ : corner_press_);
    corner_press_.reset();
    previous_corner_press_.reset();
    if (corner && first && first->first == *resizing && first->second == *corner) {
      if (clicks == 2 && callbacks.fitted) callbacks.fitted(*resizing);
      return false;
    }
    // A third click on a handle it just fitted does nothing either.
    if (corner && clicks > 2) return false;
    if (edit) *edit = edit_at(p);
    return false;
  }
  previous_corner_press_ = std::exchange(corner_press_, std::nullopt);
  if (corner) {
    gesture_ = {GestureKind::Resize, *resizing, p, p, false, std::nullopt, *corner};
    corner_press_.emplace(*resizing, *corner);
    return true;
  }
  if (auto h = rotate_handle(); h && near(*h)) {
    gesture_ = {GestureKind::Rotate, selection_.elements[0], p, p, false, std::nullopt};
    return true;
  }
  if (auto id = bendable()) {
    const auto& route = geometry_.edges.find(*id)->points;
    auto handles = bend_handles(route);
    std::size_t bends = route.size() - 2;
    // Bend points first: a short segment's middle can sit on one.
    for (std::size_t i = 0; i < handles.size(); ++i)
      if (near(handles[i])) {
        bool insert = i >= bends;
        gesture_ = {GestureKind::Bend, *id, p, p, false, std::nullopt, insert ? i - bends : i + 1, insert};
        return true;
      }
  }
  if (auto h = handle_box(); h && inside(*h, p)) {
    gesture_ = {GestureKind::Connect, selection_.elements[0], p, p, false, std::nullopt};
    return true;
  }
  for (auto end : {editor::End::Target, editor::End::Source})
    if (auto b = end_box(end); b && inside(*b, p)) {
      gesture_ = {GestureKind::Reconnect, selection_.relationships[0], p, p, false, std::nullopt};
      gesture_.end = end;
      return true;
    }
  auto [node, edge] = hit(p);
  editor::Selection sel = selection_;
  if (node) {
    auto it = std::find(sel.elements.begin(), sel.elements.end(), *node);
    bool has = it != sel.elements.end();
    if (modifiers.shift || modifiers.toggle) {
      if (has) sel.elements.erase(it);
      else sel.elements.push_back(*node);
      select(sel);
    } else if (!has) {
      select({{*node}, {}});
    }
    bool several = sel.elements.size() > 1 || !sel.relationships.empty();
    gesture_ = {GestureKind::Press, *node, p, p, false, std::nullopt};
    gesture_.narrow = has && several && !modifiers.shift && !modifiers.toggle;
  } else if (edge) {
    if (modifiers.shift || modifiers.toggle) {
      auto it = std::find(sel.relationships.begin(), sel.relationships.end(), *edge);
      if (it != sel.relationships.end()) sel.relationships.erase(it);
      else sel.relationships.push_back(*edge);
    } else {
      sel = {{}, {*edge}};
    }
    select(sel);
  } else {
    // Empty canvas: a click clears the selection, a drag draws a band (with Shift or
    // Command/Ctrl, the band adds to what is selected).
    bool extend = modifiers.shift || modifiers.toggle;
    if (!extend) select({});
    gesture_ = {GestureKind::Band, "", p, p, false, std::nullopt};
    gesture_.extend = extend;
  }
  return true;
}

bool Canvas::drag(double x, double y, Modifiers modifiers) {
  draw::Point p = to_user(x, y);
  if (gesture_.kind == GestureKind::Press || gesture_.kind == GestureKind::Move) {
    if (gesture_.kind == GestureKind::Press &&
        std::hypot(p.x - gesture_.start.x, p.y - gesture_.start.y) < kDragThreshold * units_per_pixel())
      return false;
    gesture_.kind = GestureKind::Move;
    gesture_.current = p;
    // A plain drop stays where it is let go; Option/Alt asks for a placement by the neighbours.
    // A group always lands where it is let go: its nodes keep their places relative to each other.
    gesture_.freeform = !modifiers.snap || grouped();
    return true;
  }
  if (gesture_.kind == GestureKind::Band) {
    if (!gesture_.moved &&
        std::hypot(p.x - gesture_.start.x, p.y - gesture_.start.y) < kDragThreshold * units_per_pixel())
      return false;
    gesture_.moved = true;
    gesture_.current = p;
    return true;
  }
  if (gesture_.kind == GestureKind::Resize || gesture_.kind == GestureKind::Bend ||
      gesture_.kind == GestureKind::Rotate) {
    if (!gesture_.moved &&
        std::hypot(p.x - gesture_.start.x, p.y - gesture_.start.y) < kDragThreshold * units_per_pixel())
      return false;
    gesture_.moved = true;
    gesture_.current = p;
    if (gesture_.kind == GestureKind::Resize) gesture_.keep_aspect = modifiers.shift;
    return true;
  }
  if (gesture_.kind == GestureKind::Connect) {
    gesture_.current = p;
    auto target = node_at(p, true);
    gesture_.target = target && *target != gesture_.id ? target : std::nullopt;
    return true;
  }
  if (gesture_.kind == GestureKind::Reconnect) {
    gesture_.current = p;
    gesture_.target = reconnect_target(p);
    return true;
  }
  return false;
}

bool Canvas::release(double x, double y, Modifiers modifiers) {
  if (gesture_.kind == GestureKind::Idle) return false;
  // A move drops as its preview last showed it: Option/Alt let go a moment before the button
  // does not change what is written.
  Modifiers at_release = modifiers;
  if (gesture_.kind == GestureKind::Move) at_release.snap = !gesture_.freeform;
  // Likewise a resize writes the proportions its preview last showed, Shift or not at the end.
  if (gesture_.kind == GestureKind::Resize && gesture_.moved) at_release.shift = gesture_.keep_aspect;
  drag(x, y, at_release);
  Gesture g = gesture_;
  auto moved = moved_box();
  auto resized = resized_box();
  auto bent = bent_route();
  auto turn = turned();
  bool group = grouped();
  gesture_ = {};
  if (g.kind == GestureKind::Move && moved && group) {
    const Box* from = geometry_.nodes.find(g.id);
    Point offset{moved->x - from->x, moved->y - from->y};
    // A copy: applying the move renders the view again, which can change the selection.
    std::vector<std::string> members = selection_.elements;
    if ((offset.x != 0 || offset.y != 0) && callbacks.moved) callbacks.moved(members, offset);
  } else if (g.kind == GestureKind::Move && moved) {
    if (callbacks.dropped) callbacks.dropped(g.id, *moved, g.freeform);
  } else if (g.kind == GestureKind::Press && g.narrow) {
    select({{g.id}, {}});
  } else if (g.kind == GestureKind::Band) {
    if (!g.moved) return true;
    editor::Selection add = enclosed(g.start, g.current);
    select(g.extend ? united(selection_, std::move(add)) : std::move(add));
  } else if (resized) {
    // A click straight after a resize is not the start of a double-click that would undo it.
    corner_press_.reset();
    previous_corner_press_.reset();
    if (callbacks.resized) callbacks.resized(g.id, *resized);
  } else if (turn) {
    if (callbacks.rotated && *turn != rotation_of(g.id)) callbacks.rotated(g.id, *turn);
  } else if (bent) {
    if (callbacks.rerouted) callbacks.rerouted(g.id, std::vector<Point>(bent->begin() + 1, bent->end() - 1));
  } else if (g.kind == GestureKind::Connect && g.target) {
    if (callbacks.connected) callbacks.connected(g.id, *g.target);
  } else if (g.kind == GestureKind::Reconnect && g.target) {
    if (callbacks.reconnected) callbacks.reconnected(g.id, g.end, *g.target);
  }
  return true;
}

bool Canvas::escape() {
  gesture_ = {};
  select({});
  return true;
}

bool Canvas::select_all() {
  if (!list_) return false;
  editor::Selection all;
  for (const auto& id : elements_)
    if (geometry_.nodes.contains(id)) all.elements.push_back(id);
  for (const auto& entry : ends_)
    if (geometry_.edges.contains(entry.first)) all.relationships.push_back(entry.first);
  gesture_ = {};
  select(std::move(all));
  return true;
}

bool Canvas::delete_selection() {
  if (selection_.elements.empty() && selection_.relationships.empty()) return false;
  editor::Selection sel = selection_;
  select({});
  if (callbacks.deleted) callbacks.deleted(sel);
  return true;
}

void Canvas::commit_edit(const Edit& edit, const std::string& text) {
  std::string t = trim(normalize_line_breaks(text));
  if (t == edit.value) return;
  if (!edit.relationship) {
    if (!t.empty() && callbacks.renamed) callbacks.renamed(edit.id, t);
  } else if (callbacks.relabelled) {
    callbacks.relabelled(edit.id, t.empty() ? std::nullopt : std::optional(t));
  }
}

// ── Backdrop ──

draw::DisplayList Canvas::backdrop(double width, double height) const {
  draw::DisplayList out;
  if (!list_ || grid_ <= 0) return out;
  out.width = list_->width;
  out.height = list_->height;
  out.root = list_->root;
  draw::Point from = to_user(0, 0), to = to_user(width, height);
  if (background_) {
    // The drawing's background spreads over the whole area, so the grid looks the same past the
    // drawing's edges as on it.
    draw::Item page = *background_;
    double x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;
    for (const auto& seg : page.path) {
      int points = seg.op == draw::Segment::Close ? 0 : seg.op == draw::Segment::Cubic ? 3 : 1;
      for (int i = 0; i < points; ++i) {
        x0 = std::min(x0, seg.p[i].x), y0 = std::min(y0, seg.p[i].y);
        x1 = std::max(x1, seg.p[i].x), y1 = std::max(y1, seg.p[i].y);
      }
    }
    using S = draw::Segment;
    page.path = {S{S::Move, {{x0, y0}}}, S{S::Line, {{x1, y0}}}, S{S::Line, {{x1, y1}}}, S{S::Line, {{x0, y1}}},
                 S{S::Close, {}}};
    out.items.push_back(std::move(page));
  }
  // Lines a pixel wide, every step where they are at least 8 pixels apart, darker every fifth step;
  // zoomed far out only the darker ones stay, or none.
  constexpr int kMajor = 5;
  constexpr double kMinGap = 8;
  constexpr double kMaxLines = 100000;  ///< more than any screen holds at kMinGap pixels apart
  double px = units_per_pixel();
  double step = grid_ / px >= kMinGap ? grid_ : grid_ * kMajor / px >= kMinGap ? grid_ * kMajor : 0;
  if (step == 0) return out;
  // On a dark background the lines are light. A translucent background is seen over the shells'
  // canvas, which is near white (#f8fafc in all three).
  bool dark = false;
  if (background_) {
    const auto& c = *background_->paint.fill;
    double alpha = std::clamp(background_->paint.fill_alpha, 0.0, 1.0);
    dark = (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) * alpha + (1 - alpha) < 0.5;
  }
  draw::Rgb ink = dark ? draw::Rgb{1, 1, 1} : draw::Rgb{0.392, 0.455, 0.545};  // #64748b
  std::vector<draw::Segment> minor, major;
  auto line = [&](double v, bool vertical) {
    bool strong = std::fmod(std::abs(js_round(v / grid_)), kMajor) == 0;
    auto& path = strong ? major : minor;
    if (vertical) {
      path.push_back({draw::Segment::Move, {{v, from.y}}});
      path.push_back({draw::Segment::Line, {{v, to.y}}});
    } else {
      path.push_back({draw::Segment::Move, {{from.x, v}}});
      path.push_back({draw::Segment::Line, {{to.x, v}}});
    }
  };
  // Lines are counted by an integer index, not by adding `step`: far from the origin `x += step`
  // can leave `x` unchanged (its ulp is larger than the step) and never end, and it drifts on
  // fractional steps. Past 2^53 steps from the origin consecutive indices are no longer distinct
  // doubles, so lines would repeat: nothing is drawn there. At least kMinGap pixels apart, the
  // area holds far fewer than kMaxLines.
  auto lines = [&](double lo, double hi, bool vertical) {
    constexpr double kExact = 0x1p53;
    double first = std::ceil(lo / step), last = std::floor(hi / step);
    if (!std::isfinite(first) || !std::isfinite(last) || last < first || last - first > kMaxLines ||
        std::max(std::abs(first), std::abs(last)) > kExact)
      return;
    auto count = static_cast<std::int64_t>(last - first);
    for (std::int64_t i = 0; i <= count; ++i) line((first + static_cast<double>(i)) * step, vertical);
  };
  lines(from.x, to.x, true);
  lines(from.y, to.y, false);
  for (auto* path : {&minor, &major}) {
    if (path->empty()) continue;
    draw::Item item;
    item.kind = draw::Item::Path;
    item.path = std::move(*path);
    item.paint.stroke = ink;
    item.paint.stroke_width = px;
    item.paint.stroke_alpha = (path == &major ? 0.28 : 0.14) * (dark ? 0.6 : 1);
    out.items.push_back(std::move(item));
  }
  return out;
}

// ── Overlay ──

draw::DisplayList Canvas::overlay() const {
  draw::DisplayList out;
  if (!list_) return out;
  out.width = list_->width;
  out.height = list_->height;
  out.root = list_->root;
  // What is selected, and what a band being dragged would select.
  auto area = band();
  std::optional<editor::Selection> banded;
  if (area) {
    editor::Selection add = enclosed(gesture_.start, gesture_.current);
    banded = gesture_.extend ? united(selection_, std::move(add)) : std::move(add);
  }
  const editor::Selection& shown = banded ? *banded : selection_;
  for (const auto& id : shown.relationships)
    if (const EdgeRoute* route = geometry_.edges.find(id); route && !route->points.empty())
      out.items.push_back(stroke(polyline(route->points), 4, 0.45));
  for (const auto& id : shown.elements)
    if (const Box* b = geometry_.nodes.find(id)) {
      // While it turns, the outline turns with the pointer.
      double degrees = gesture_.kind == GestureKind::Rotate && gesture_.id == id && turned() ? *turned() : rotation_of(id);
      out.items.push_back(stroke(turn_path(rounded_rect(b->x - 4, b->y - 4, b->width + 8, b->height + 8, 8), mid(*b), degrees),
                                 2, 1, {5, 3}));
    }
  auto ghost = [&](const Box& b, double degrees = 0) {
    out.items.push_back(
        stroke(turn_path(rounded_rect(b.x, b.y, b.width, b.height, 6), mid(b), degrees), 1.5, 1, {4, 3}, kAccent, 0.12));
  };
  if (area)
    out.items.push_back(stroke(rounded_rect(area->x, area->y, area->width, area->height, 0), 1, 0.8, {}, kAccent, 0.08));
  if (auto box = moved_box(); box && grouped()) {
    // The whole group, each node offset as the grabbed one is.
    const Box& from = *geometry_.nodes.find(gesture_.id);
    double dx = box->x - from.x, dy = box->y - from.y;
    std::size_t drawn = 0;
    for (const auto& id : selection_.elements) {
      if (const Box* b = geometry_.nodes.find(id)) {
        ghost({b->x + dx, b->y + dy, b->width, b->height}, rotation_of(id));
        ++drawn;
      }
    }
    draw::Item label;
    label.kind = draw::Item::Label;
    label.text = {std::to_string(drawn) + " elements", box->x, box->y - 10, "start", "", "system-ui", 12, true,
                  {0.114, 0.306, 0.847}, 1};
    out.items.push_back(std::move(label));
  } else if (box) {
    ghost(*box, rotation_of(gesture_.id));
    if (callbacks.preview_drop)
      if (auto hint = callbacks.preview_drop(gesture_.id, *box, gesture_.freeform)) {
        auto name = [&](const std::string& id) { return callbacks.name_of ? callbacks.name_of(id) : id; };
        draw::Item label;
        label.kind = draw::Item::Label;
        label.text = {describe_hint(*hint, name), box->x, box->y - 10, "start", "", "system-ui", 12, true,
                      {0.114, 0.306, 0.847}, 1};
        out.items.push_back(std::move(label));
      }
  } else if (gesture_.kind == GestureKind::Connect) {
    if (const Box* from = geometry_.nodes.find(gesture_.id))
      out.items.push_back(
          stroke(polyline({dd::rotate_point({from->x + from->width, from->y + from->height / 2}, mid(*from),
                                            rotation_of(gesture_.id)),
                           {gesture_.current.x, gesture_.current.y}}),
                 1.5, 1, {4, 3}));
    if (gesture_.target)
      if (const Box* target = geometry_.nodes.find(*gesture_.target)) ghost(*target);
  } else if (gesture_.kind == GestureKind::Reconnect) {
    // The line as it would be: from the end that stays to the pointer.
    if (const EdgeRoute* route = reconnectable()) {
      const Point& fixed = gesture_.end == editor::End::Source ? route->points.back() : route->points.front();
      out.items.push_back(stroke(polyline({fixed, {gesture_.current.x, gesture_.current.y}}), 1.5, 1, {4, 3}));
    }
    if (gesture_.target)
      if (const Box* target = geometry_.nodes.find(*gesture_.target)) ghost(*target);
  }
  if (auto box = resized_box()) {
    ghost(*box, rotation_of(gesture_.id));
    draw::Item label;
    label.kind = draw::Item::Label;
    std::string size = js_number_string(js_round(box->width)) + " \u00d7 " + js_number_string(js_round(box->height));
    label.text = {"size " + size,
                  box->x, box->y - 10, "start", "", "system-ui", 12, true, {0.114, 0.306, 0.847}, 1};
    out.items.push_back(std::move(label));
  }
  if (auto degrees = turned())
    if (const Box* b = geometry_.nodes.find(gesture_.id)) {
      ghost(*b, *degrees);
      draw::Item label;
      label.kind = draw::Item::Label;
      Box bounds = rotated_bounds(*b, *degrees);
      label.text = {js_number_string(*degrees) + "\u00b0", bounds.x, bounds.y - 10, "start", "", "system-ui", 12, true,
                    {0.114, 0.306, 0.847}, 1};
      out.items.push_back(std::move(label));
    }
  if (auto route = bent_route()) out.items.push_back(stroke(polyline(*route), 1.5, 1, {4, 3}));
  bool busy = gesture_.kind == GestureKind::Move || gesture_.kind == GestureKind::Resize ||
              gesture_.kind == GestureKind::Bend || gesture_.kind == GestureKind::Rotate || area;
  if (auto h = handle_box(); h && !busy)
    out.items.push_back(stroke(ellipse(*h), 2, 1, {}, draw::Rgb{1, 1, 1}, 1));
  if (auto id = resizable(); id && !busy) {
    const Box& b = *geometry_.nodes.find(*id);
    double degrees = rotation_of(*id);
    for (const auto& c : corners(b)) {
      Point q = dd::rotate_point(c, mid(b), degrees);
      out.items.push_back(stroke(turn_path(rounded_rect(q.x - kGrip, q.y - kGrip, 2 * kGrip, 2 * kGrip, 1), q, degrees),
                                 1.5, 1, {}, draw::Rgb{1, 1, 1}, 1));
    }
  }
  if (auto h = rotate_handle(); h && !busy) {
    // A stalk from the top-right corner to a round handle.
    const Box& b = *geometry_.nodes.find(selection_.elements[0]);
    Point top = dd::rotate_point({b.x + b.width, b.y}, mid(b), rotation_of(selection_.elements[0]));
    out.items.push_back(stroke(polyline({top, *h}), 1.5, 1));
    out.items.push_back(stroke(ellipse({h->x - kGrip, h->y - kGrip, 2 * kGrip, 2 * kGrip}), 1.5, 1, {},
                               draw::Rgb{1, 1, 1}, 1));
  }
  if (auto id = bendable(); id && !busy) {
    const auto& route = geometry_.edges.find(*id)->points;
    auto handles = bend_handles(route);
    for (std::size_t i = 0; i < handles.size(); ++i) {
      bool middle = i >= route.size() - 2;  // past the bend points: a segment's middle
      double r = middle ? kGrip * 0.7 : kGrip;
      out.items.push_back(stroke(ellipse({handles[i].x - r, handles[i].y - r, 2 * r, 2 * r}), 1.5, middle ? 0.6 : 1,
                                 {}, draw::Rgb{1, 1, 1}, middle ? 0.6 : 1));
    }
  }
  for (auto end : {editor::End::Source, editor::End::Target}) {
    if (busy || (gesture_.kind == GestureKind::Reconnect && gesture_.end == end)) continue;
    if (auto b = end_box(end)) out.items.push_back(stroke(ellipse(*b), 2, 1, {}, draw::Rgb{1, 1, 1}, 1));
  }
  return out;
}

}  // namespace dd::canvas
