// Ports of packages/layout/src/graph.ts, hints.ts, route.ts, fixed.ts and the `layout` entry point.
// Arithmetic follows the TypeScript expression by expression, so results match to the last bit.

#include "diadraw/layout/layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/render/svg.hpp"

namespace dd {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

Point center(const Box& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

/// Math.max(...values) / Math.min(...values) over a possibly empty list.
template <class It, class F>
double max_of(It first, It last, F f, double start = -kInf) {
  double m = start;
  for (; first != last; ++first) m = std::max(m, f(*first));
  return m;
}
template <class It, class F>
double min_of(It first, It last, F f, double start = kInf) {
  double m = start;
  for (; first != last; ++first) m = std::min(m, f(*first));
  return m;
}

}  // namespace

// ── graph.ts ──────────────────────────────────────────────────────────────────────────────────

Measure default_measure() {
  return {
      [](const Element&) { return kDefaultNodeSize; },
      [](const Relationship& r) -> std::optional<Size> {
        if (!r.label) return std::nullopt;
        return Size{std::max(20.0, static_cast<double>(utf16_length(*r.label)) * 7), 16};
      },
      {},
  };
}

std::string group_node_id(std::string_view group_id) { return "group:" + std::string(group_id); }

namespace {

/// "X,Y" as two numbers no larger than `limit`, both given.
std::optional<Point2> number_pair(const std::string* text, double limit) {
  if (!text) return std::nullopt;
  auto comma = text->find(',');
  if (comma == std::string::npos || text->find(',', comma + 1) != std::string::npos) return std::nullopt;
  std::string_view xs = js_trim(std::string_view(*text).substr(0, comma));
  std::string_view ys = js_trim(std::string_view(*text).substr(comma + 1));
  if (xs.empty() || ys.empty()) return std::nullopt;
  double x = js_to_number(xs), y = js_to_number(ys);
  if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > limit || std::fabs(y) > limit) return std::nullopt;
  return Point2{x, y};
}

}  // namespace

std::optional<Anchor> anchor_of(const Element& element) {
  // In a group, an element is laid out with the group's other members.
  if (!element.parent || element.group || !element.style) return std::nullopt;
  // Fractions of the parent's box and pixels, within bounds that keep every box finite.
  auto at = number_pair(element.style->find("anchor"), 1e3);
  if (!at) return std::nullopt;
  return Anchor{*at, number_pair(element.style->find("anchorOffset"), 1e7).value_or(Point2{0, 0})};
}

std::optional<LabelAnchor> label_anchor_of(const std::optional<Style>& style) {
  if (!style) return std::nullopt;
  const std::string* at = style->find("labelAt");
  const std::string* offset = style->find("labelOffset");
  const std::string* align = style->find("align");
  const std::string* vertical = style->find("verticalAlign");
  if (!at && !offset && !align && !vertical) return std::nullopt;
  LabelAnchor a;
  if (at) {
    // ALONG or ALONG,AWAY; along is clamped to the route, away kept within bounds that stay finite.
    const auto pair = number_pair(at, 1e7);
    const double along = pair ? pair->x : js_to_number(js_trim(*at));
    if (std::isfinite(along)) a.along = std::clamp(along, -1.0, 1.0);
    if (pair) a.away = pair->y;
  }
  a.offset = number_pair(offset, 1e7).value_or(Point2{0, 0});
  if (align) a.side.x = *align == "left" ? 0 : *align == "right" ? 1 : 0.5;
  if (vertical) a.side.y = *vertical == "top" ? 0 : *vertical == "bottom" ? 1 : 0.5;
  return a;
}

Box label_at(const std::vector<Point>& points, Size size, const LabelAnchor& anchor) {
  Point p = points.empty() ? Point{0, 0} : points.front();
  double total = 0;
  for (std::size_t i = 1; i < points.size(); ++i)
    total += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
  // The point `along` the route by length, then `away` along the left-hand normal of its segment
  // (draw.io's mxGraphView.getPoint).
  double distance = (std::clamp(anchor.along, -1.0, 1.0) / 2 + 0.5) * total;
  for (std::size_t i = 1; i < points.size(); ++i) {
    const Point& a = points[i - 1];
    const Point& b = points[i];
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    if (distance > length && i + 1 < points.size()) {
      distance -= length;
      continue;
    }
    const double f = length == 0 ? 0 : std::min(1.0, distance / length);
    const double nx = length == 0 ? 0 : (b.y - a.y) / length;
    const double ny = length == 0 ? 0 : (b.x - a.x) / length;
    p = {a.x + (b.x - a.x) * f + nx * anchor.away, a.y + (b.y - a.y) * f - ny * anchor.away};
    break;
  }
  // A label beside the point (not centred on it) keeps draw.io's 2 px spacing from it.
  constexpr double kSpacing = 2;
  const double gap_x = (1 - 2 * anchor.side.x) * kSpacing;
  const double gap_y = (1 - 2 * anchor.side.y) * kSpacing;
  return {p.x + anchor.offset.x + gap_x - anchor.side.x * size.width,
          p.y + anchor.offset.y + gap_y - anchor.side.y * size.height, size.width, size.height};
}

Box anchored_box(const Anchor& anchor, const Box& parent, Size size) {
  return {parent.x + anchor.at.x * parent.width + anchor.offset.x,
          parent.y + anchor.at.y * parent.height + anchor.offset.y, size.width, size.height};
}

namespace {

/// draw.io-style edge styles that draw right angles.
bool is_orthogonal(const std::optional<Style>& style) {
  if (!style) return false;
  const std::string* edge_style = style->find("edgeStyle");
  if (!edge_style) return false;
  // /orthogonal|elbow|entityRelation|segment|isometric/i
  std::string lower;
  for (char c : *edge_style) lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  for (std::string_view word : {"orthogonal", "elbow", "entityrelation", "segment", "isometric"})
    if (lower.find(word) != std::string::npos) return true;
  return false;
}

}  // namespace

LayoutGraph to_layout_graph(const ResolvedView& view, const Measure& measure) {
  std::set<std::string, std::less<>> visible;
  for (const auto& e : view.elements) visible.insert(e.id);
  std::set<std::string, std::less<>> group_ids;
  for (const auto& g : view.groups) group_ids.insert(g.id);
  std::map<std::string, std::string, std::less<>> group_parent;

  // `size a W, H`: the last one for an element wins.
  std::map<std::string, Size, std::less<>> sized;
  for (const auto& h : view.view.hints)
    if (const auto* size = std::get_if<SizeHint>(&h)) sized[size->element] = {size->width, size->height};

  LayoutGraph graph;
  for (const auto& element : view.elements) {
    auto given = sized.find(element.id);
    Size size = given != sized.end() ? given->second : measure.node(element);
    std::optional<std::string> element_parent;
    if (element.parent && visible.count(*element.parent)) element_parent = element.parent;
    std::optional<std::string> parent = element_parent;
    if (element.group && group_ids.count(*element.group)) {
      parent = group_node_id(*element.group);
      if (element_parent && !group_parent.count(*element.group))
        group_parent.emplace(*element.group, *element_parent);
    }
    // An adornment hangs on its own parent; in a group, it is laid out as any other node.
    std::optional<Anchor> anchor = element_parent ? anchor_of(element) : std::nullopt;
    graph.nodes.push_back({element.id, size.width, size.height, parent, element.ports, anchor});
  }
  // Groups go in front, each one unshifted, so they end up in reverse order.
  std::vector<LayoutNode> groups;
  for (const auto& group : view.groups) {
    std::optional<std::string> parent;
    if (group.parent && group_ids.count(*group.parent))
      parent = group_node_id(*group.parent);
    else if (auto it = group_parent.find(group.id); it != group_parent.end())
      parent = it->second;
    groups.push_back({group_node_id(group.id), 0, 0, parent, {}, std::nullopt});
  }
  std::reverse(groups.begin(), groups.end());
  graph.nodes.insert(graph.nodes.begin(), groups.begin(), groups.end());

  for (const auto& r : view.relationships) {
    LayoutEdge e;
    e.id = r.id;
    e.source = r.source.element;
    e.target = r.target.element;
    e.source_port = r.source.port;
    e.target_port = r.target.port;
    e.label = measure.edge_label(r);
    if (is_orthogonal(r.style)) e.routing = EdgeRouting::Orthogonal;
    // Only the fixed engine places labels where the style says.
    if (view.view.layout.algorithm == "fixed") e.label_anchor = label_anchor_of(r.style);
    graph.edges.push_back(std::move(e));
  }
  graph.options = view.view.layout;
  return graph;
}

// ── hints.ts ──────────────────────────────────────────────────────────────────────────────────

namespace {

constexpr double kEps = 1;

bool placed(const Box& el, const Box& ref, Placement placement) {
  switch (placement) {
    case Placement::Below: return el.y >= ref.y + ref.height - kEps;
    case Placement::Above: return el.y + el.height <= ref.y + kEps;
    case Placement::RightOf: return el.x >= ref.x + ref.width - kEps;
    default: return el.x + el.width <= ref.x + kEps;
  }
}

}  // namespace

bool hint_satisfied(const LayoutHint& hint, const OrderedMap<Box>& nodes, Direction direction) {
  // Routes, sizes and turns are applied as they are written, never weighed against the layout.
  if (std::holds_alternative<RouteHint>(hint) || std::holds_alternative<SizeHint>(hint) ||
      std::holds_alternative<RotateHint>(hint))
    return true;
  if (const auto* h = std::get_if<PinHint>(&hint)) {
    const Box* el = nodes.find(h->element);
    const Box* ref = nodes.find(h->reference);
    return el && ref && placed(*el, *ref, h->placement);
  }
  if (const auto* h = std::get_if<PinAtHint>(&hint)) {
    const Box* el = nodes.find(h->element);
    return el && std::fabs(el->x - h->x) <= kEps && std::fabs(el->y - h->y) <= kEps;
  }
  const std::vector<ElementId>& ids =
      std::holds_alternative<AlignHint>(hint) ? std::get<AlignHint>(hint).elements : std::get<OrderHint>(hint).elements;
  std::vector<const Box*> boxes;
  for (const auto& id : ids) {
    const Box* b = nodes.find(id);
    if (!b) return false;
    boxes.push_back(b);
  }
  if (const auto* h = std::get_if<AlignHint>(&hint)) {
    std::vector<double> values;
    for (const Box* b : boxes) values.push_back(h->axis == Axis::Horizontal ? center(*b).y : center(*b).x);
    double hi = max_of(values.begin(), values.end(), [](double v) { return v; });
    double lo = min_of(values.begin(), values.end(), [](double v) { return v; });
    return hi - lo <= kEps;
  }
  bool across_x = direction == Direction::Down || direction == Direction::Up;
  for (std::size_t i = 1; i < boxes.size(); ++i) {
    double prev = across_x ? center(*boxes[i - 1]).x : center(*boxes[i - 1]).y;
    double cur = across_x ? center(*boxes[i]).x : center(*boxes[i]).y;
    if (!(cur > prev)) return false;
  }
  return true;
}

// ── route.ts ──────────────────────────────────────────────────────────────────────────────────

namespace {

constexpr double kRouteMargin = 12;
constexpr double kBendCost = 40;

enum class Side { North, South, East, West };

Point port_of(const Box& b, Side side) {
  switch (side) {
    case Side::North: return {b.x + b.width / 2, b.y};
    case Side::South: return {b.x + b.width / 2, b.y + b.height};
    case Side::East: return {b.x + b.width, b.y + b.height / 2};
    default: return {b.x, b.y + b.height / 2};
  }
}

Point stub(Point p, Side side, double d = kRouteMargin) {
  switch (side) {
    case Side::North: return {p.x, p.y - d};
    case Side::South: return {p.x, p.y + d};
    case Side::East: return {p.x + d, p.y};
    default: return {p.x - d, p.y};
  }
}

Box inflate(const Box& b, double m) { return {b.x - m, b.y - m, b.width + 2 * m, b.height + 2 * m}; }

bool crosses_box(Point a, Point b, const Box& box) {
  double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
  double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
  return x1 > box.x + 0.5 && x0 < box.x + box.width - 0.5 && y1 > box.y + 0.5 &&
         y0 < box.y + box.height - 0.5;
}

/// The binary heap of route.ts, reproduced exactly: ties pop in the same order as in TypeScript.
template <class T>
class MinHeap {
 public:
  [[nodiscard]] std::size_t size() const { return items_.size(); }
  void push(double key, T value) {
    items_.push_back({key, value});
    for (std::size_t i = items_.size() - 1; i > 0;) {
      std::size_t parent = (i - 1) >> 1;
      if (items_[parent].key <= items_[i].key) break;
      std::swap(items_[parent], items_[i]);
      i = parent;
    }
  }
  T pop() {
    T top = items_.front().value;
    Item last = items_.back();
    items_.pop_back();
    if (!items_.empty()) {
      items_[0] = last;
      for (std::size_t i = 0;;) {
        std::size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < items_.size() && items_[l].key < items_[m].key) m = l;
        if (r < items_.size() && items_[r].key < items_[m].key) m = r;
        if (m == i) break;
        std::swap(items_[m], items_[i]);
        i = m;
      }
    }
    return top;
  }

 private:
  struct Item {
    double key;
    T value;
  };
  std::vector<Item> items_;
};

/// Sides of `a` that face `b`, best first.
std::vector<Side> facing(const Box& a, const Box& b) {
  double dx = b.x + b.width / 2 - (a.x + a.width / 2);
  double dy = b.y + b.height / 2 - (a.y + a.height / 2);
  Side horizontal = dx >= 0 ? Side::East : Side::West;
  Side vertical = dy >= 0 ? Side::South : Side::North;
  std::vector<Side> out = std::fabs(dx) >= std::fabs(dy) ? std::vector<Side>{horizontal, vertical}
                                                         : std::vector<Side>{vertical, horizontal};
  for (Side s : {Side::North, Side::South, Side::East, Side::West})
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
  return out;
}

double path_cost(const std::vector<Point>& p) {
  double sum = 0;
  for (std::size_t i = 1; i < p.size(); ++i) sum += std::fabs(p[i].x - p[i - 1].x) + std::fabs(p[i].y - p[i - 1].y);
  return sum + kBendCost * std::max(0.0, static_cast<double>(p.size()) - 2);
}

}  // namespace

std::vector<Point> simplify(const std::vector<Point>& points) {
  std::vector<Point> out;
  for (const Point& p : points) {
    const Point* a = out.size() >= 2 ? &out[out.size() - 2] : nullptr;
    const Point* b = out.empty() ? nullptr : &out.back();
    if (b && std::fabs(b->x - p.x) < 0.01 && std::fabs(b->y - p.y) < 0.01) continue;
    if (a && b &&
        ((std::fabs(a->x - b->x) < 0.01 && std::fabs(b->x - p.x) < 0.01) ||
         (std::fabs(a->y - b->y) < 0.01 && std::fabs(b->y - p.y) < 0.01))) {
      out.back() = p;
    } else {
      out.push_back(p);
    }
  }
  return out;
}

std::optional<std::vector<Point>> shortest_orthogonal(Point from, Point to, const std::vector<Box>& obstacles) {
  std::vector<Box> blocks;
  for (const auto& o : obstacles) blocks.push_back(inflate(o, kRouteMargin - 1));
  std::set<double> xs{from.x, to.x};
  std::set<double> ys{from.y, to.y};
  for (const auto& o : obstacles) {
    Box b = inflate(o, kRouteMargin);
    xs.insert(b.x);
    xs.insert(b.x + b.width);
    ys.insert(b.y);
    ys.insert(b.y + b.height);
  }
  std::vector<double> X(xs.begin(), xs.end());
  std::vector<double> Y(ys.begin(), ys.end());
  auto index_of = [](const std::vector<double>& v, double value) {
    return static_cast<long>(std::lower_bound(v.begin(), v.end(), value) - v.begin());
  };
  auto is_free = [&](Point a, Point b) {
    return std::none_of(blocks.begin(), blocks.end(), [&](const Box& o) { return crosses_box(a, b, o); });
  };

  // State: grid cell plus the direction we arrived in (0 = none, 1 = horizontal, 2 = vertical).
  const long ny = static_cast<long>(Y.size());
  auto key = [&](long i, long j, long d) { return (i * ny + j) * 3 + d; };
  long start = key(index_of(X, from.x), index_of(Y, from.y), 0);
  long goal_i = index_of(X, to.x), goal_j = index_of(Y, to.y);
  std::unordered_map<long, double> dist{{start, 0}};
  std::unordered_map<long, long> prev;
  MinHeap<long> heap;
  auto h = [&](long i, long j) { return std::fabs(X[i] - to.x) + std::fabs(Y[j] - to.y); };
  heap.push(h(goal_i, goal_j), start);

  static constexpr long kMoves[4][3] = {{1, 0, 1}, {-1, 0, 1}, {0, 1, 2}, {0, -1, 2}};
  while (heap.size() > 0) {
    long s = heap.pop();
    long d = s % 3;
    long cell = (s - d) / 3;
    long j = cell % ny;
    long i = (cell - j) / ny;
    double cost = dist.at(s);
    if (i == goal_i && j == goal_j) {
      std::vector<Point> path;
      for (long cur = s;;) {
        long c = (cur - cur % 3) / 3;
        long cj = c % ny;
        path.push_back({X[(c - cj) / ny], Y[cj]});
        auto it = prev.find(cur);
        if (it == prev.end()) break;
        cur = it->second;
      }
      std::reverse(path.begin(), path.end());
      return simplify(path);
    }
    Point here{X[i], Y[j]};
    for (const auto& move : kMoves) {
      long ni = i + move[0], nj = j + move[1], dir = move[2];
      if (ni < 0 || nj < 0 || ni >= static_cast<long>(X.size()) || nj >= ny) continue;
      Point there{X[ni], Y[nj]};
      if (!is_free(here, there)) continue;
      double step = std::fabs(there.x - here.x) + std::fabs(there.y - here.y) +
                    (d != 0 && d != dir ? kBendCost : 0);
      long next = key(ni, nj, dir);
      double nc = cost + step;
      auto known = dist.find(next);
      if (nc < (known == dist.end() ? kInf : known->second)) {
        dist[next] = nc;
        prev[next] = s;
        heap.push(nc + h(ni, nj), next);
      }
    }
  }
  return std::nullopt;
}

std::vector<Point> route_edge(const Box& source, const Box& target, const std::vector<Box>& obstacles) {
  return route_edge(source, target, obstacles, std::nullopt, std::nullopt);
}

std::vector<Point> route_edge(const Box& source, const Box& target, const std::vector<Box>& obstacles,
                              std::optional<PortSide> source_side, std::optional<PortSide> target_side) {
  auto to_side = [](PortSide p) {
    return p == PortSide::North ? Side::North : p == PortSide::South ? Side::South : p == PortSide::East ? Side::East : Side::West;
  };
  std::optional<std::vector<Point>> best;
  auto fs = facing(source, target);
  auto ft = facing(target, source);
  std::vector<Side> ss = source_side ? std::vector<Side>{to_side(*source_side)} : std::vector<Side>{fs[0], fs[1]};
  std::vector<Side> ts = target_side ? std::vector<Side>{to_side(*target_side)} : std::vector<Side>{ft[0], ft[1]};
  for (Side s : ss) {
    for (Side t : ts) {
      Point a = port_of(source, s);
      Point b = port_of(target, t);
      auto middle = shortest_orthogonal(stub(a, s), stub(b, t), obstacles);
      if (!middle) continue;
      std::vector<Point> joined{a};
      joined.insert(joined.end(), middle->begin(), middle->end());
      joined.push_back(b);
      auto path = simplify(joined);
      if (!best || path_cost(path) < path_cost(*best)) best = std::move(path);
    }
  }
  if (best) return *best;
  Point a = port_of(source, ss.front());
  Point b = port_of(target, ts.front());
  return simplify({a, {a.x, b.y}, b});
}

Box label_on_route(const std::vector<Point>& points, Size size, const std::vector<Box>& obstacles) {
  struct Segment {
    Point a, b;
    double len;
  };
  std::vector<Segment> segments;
  for (std::size_t i = 1; i < points.size(); ++i)
    segments.push_back({points[i - 1], points[i], std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)});
  std::stable_sort(segments.begin(), segments.end(), [](const Segment& p, const Segment& q) { return q.len < p.len; });
  auto box_for = [&](const Segment& s) {
    return Box{(s.a.x + s.b.x) / 2 - size.width / 2, (s.a.y + s.b.y) / 2 - size.height / 2, size.width, size.height};
  };
  auto clear = [&](const Box& l) {
    return std::none_of(obstacles.begin(), obstacles.end(), [&](const Box& o) {
      return l.x < o.x + o.width && o.x < l.x + l.width && l.y < o.y + o.height && o.y < l.y + l.height;
    });
  };
  if (segments.empty()) {
    double px = points.empty() ? 0 : points[0].x;
    double py = points.empty() ? 0 : points[0].y;
    return {px - size.width / 2, py - size.height / 2, size.width, size.height};
  }
  for (const auto& s : segments) {
    Box b = box_for(s);
    if (clear(b)) return b;
  }
  return box_for(segments.front());
}

// ── fixed.ts ──────────────────────────────────────────────────────────────────────────────────

Point boundary_point(const Box& b, Point toward) {
  Point c = center(b);
  double dx = toward.x - c.x;
  double dy = toward.y - c.y;
  if (dx == 0 && dy == 0) return c;
  double sx = dx == 0 ? kInf : b.width / 2 / std::fabs(dx);
  double sy = dy == 0 ? kInf : b.height / 2 / std::fabs(dy);
  double s = std::min(sx, sy);
  return {c.x + dx * s, c.y + dy * s};
}

Point rotate_point(Point p, Point about, double degrees) {
  if (degrees == 0) return p;
  double r = degrees * 3.14159265358979323846 / 180;
  double c = std::cos(r), s = std::sin(r);
  double dx = p.x - about.x, dy = p.y - about.y;
  return {about.x + dx * c - dy * s, about.y + dx * s + dy * c};
}

Box rotated_bounds(const Box& b, double degrees) {
  if (degrees == 0) return b;
  double r = degrees * 3.14159265358979323846 / 180;
  double c = std::fabs(std::cos(r)), s = std::fabs(std::sin(r));
  double w = b.width * c + b.height * s, h = b.width * s + b.height * c;
  Point m = center(b);
  return {m.x - w / 2, m.y - h / 2, w, h};
}

Point boundary_point(const Box& b, Point toward, double degrees) {
  if (degrees == 0) return boundary_point(b, toward);
  Point c = center(b);
  return rotate_point(boundary_point(b, rotate_point(toward, c, -degrees)), c, degrees);
}

std::map<std::string, double, std::less<>> view_rotations(const LayoutGraph& graph,
                                                          const std::vector<LayoutHint>& hints) {
  std::set<std::string, std::less<>> parents;
  for (const auto& n : graph.nodes)
    if (n.parent) parents.insert(*n.parent);
  std::map<std::string, double, std::less<>> out;
  for (const auto& h : hints)
    if (const auto* turn = std::get_if<RotateHint>(&h)) {
      // Normalised to (-180, 180]; whole turns are no turn at all.
      double d = std::fmod(turn->degrees, 360);
      if (d > 180) d -= 360;
      if (d <= -180) d += 360;
      if (parents.count(turn->element)) continue;
      if (d == 0) out.erase(turn->element);
      else out[turn->element] = d;
    }
  for (auto it = out.begin(); it != out.end();) {
    bool known = std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const LayoutNode& n) { return n.id == it->first; });
    it = known ? std::next(it) : out.erase(it);
  }
  return out;
}

namespace {

constexpr double kPadTop = 32, kPadLeft = 16, kPadBottom = 16, kPadRight = 16;
constexpr double kGap = 40;

Point relative(const Box& b, const Point2& p) { return {b.x + p.x * b.width, b.y + p.y * b.height}; }

/// Inserts corners so consecutive points are joined by horizontal and vertical segments.
std::vector<Point> orthogonalize(const std::vector<Point>& points) {
  std::vector<Point> out;
  for (const Point& p : points) {
    if (!out.empty() && out.back().x != p.x && out.back().y != p.y) out.push_back({p.x, out.back().y});
    out.push_back(p);
  }
  return simplify(out);
}

}  // namespace

Geometry fixed_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints) {
  std::map<std::string, const PinAtHint*, std::less<>> pins;
  std::map<std::string, const RouteHint*, std::less<>> routes;
  for (const auto& h : hints) {
    if (const auto* p = std::get_if<PinAtHint>(&h)) pins[p->element] = p;
    else if (const auto* r = std::get_if<RouteHint>(&h)) routes[r->relationship] = r;
  }
  // Parents in the order their first child appears, as a JavaScript Map keeps them.
  std::vector<std::pair<std::string, std::vector<std::string>>> children;
  auto children_of = [&](std::string_view id) -> std::vector<std::string>* {
    for (auto& [k, v] : children)
      if (k == id) return &v;
    return nullptr;
  };
  // Adornments are placed on their parents last; they neither make a container nor size one.
  std::unordered_set<std::string_view> ids;
  for (const auto& n : graph.nodes) ids.insert(n.id);
  std::vector<bool> is_adornment(graph.nodes.size());
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto& n = graph.nodes[i];
    is_adornment[i] = n.anchor && n.parent && ids.count(*n.parent);
  }
  std::unordered_map<std::string_view, const LayoutNode*> adornments;
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto& n = graph.nodes[i];
    if (is_adornment[i]) {
      adornments.emplace(n.id, &n);
      continue;
    }
    if (!n.parent) continue;
    if (auto* kids = children_of(*n.parent))
      kids->push_back(n.id);
    else
      children.push_back({*n.parent, {n.id}});
  }

  Geometry g;
  auto& nodes = g.nodes;
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto& n = graph.nodes[i];
    if (is_adornment[i]) continue;
    auto pin = pins.find(n.id);
    if (pin != pins.end())
      nodes.set(n.id, Box{pin->second->x, pin->second->y, pin->second->width.value_or(n.width),
                          pin->second->height.value_or(n.height)});
  }
  // Unpinned leaves go in a row under everything that is pinned.
  double bottom = max_of(nodes.begin(), nodes.end(), [](const Box& b) { return b.y + b.height; }, 0);
  double cursor = 0;
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto& n = graph.nodes[i];
    if (nodes.contains(n.id) || children_of(n.id) || is_adornment[i]) continue;
    nodes.set(n.id, Box{cursor, bottom + kGap, n.width, n.height});
    cursor += n.width + kGap;
  }
  // Unpinned containers wrap their children, innermost first.
  std::function<int(const std::string&)> depth = [&](const std::string& id) {
    for (const auto& n : graph.nodes)
      if (n.id == id) return n.parent ? 1 + depth(*n.parent) : 0;
    return 0;
  };
  std::vector<std::string> parents;
  for (const auto& [k, _] : children) parents.push_back(k);
  std::stable_sort(parents.begin(), parents.end(),
                   [&](const std::string& a, const std::string& b) { return depth(b) < depth(a); });
  for (const auto& id : parents) {
    if (nodes.contains(id)) continue;
    std::vector<Box> kids;
    for (const auto& k : *children_of(id))
      if (const Box* b = nodes.find(k)) kids.push_back(*b);
    if (kids.empty()) continue;
    double x0 = min_of(kids.begin(), kids.end(), [](const Box& k) { return k.x; }) - kPadLeft;
    double y0 = min_of(kids.begin(), kids.end(), [](const Box& k) { return k.y; }) - kPadTop;
    double x1 = max_of(kids.begin(), kids.end(), [](const Box& k) { return k.x + k.width; }) + kPadRight;
    double y1 = max_of(kids.begin(), kids.end(), [](const Box& k) { return k.y + k.height; }) + kPadBottom;
    nodes.set(id, Box{x0, y0, x1 - x0, y1 - y0});
  }
  // Adornments on the spot of their parent's box, a carrier before what it carries (an adornment
  // can carry one). Its size is its own (`size`); a `pin … at` does not move it off its parent.
  // In graph order, so the boxes come out in the same order every time.
  auto place = [&](auto& self, const LayoutNode& n) -> void {
    if (!adornments.erase(n.id)) return;  // placed already
    if (auto it = adornments.find(*n.parent); it != adornments.end()) self(self, *it->second);
    if (const Box* parent = nodes.find(*n.parent))
      nodes.set(n.id, anchored_box(*n.anchor, *parent, Size{n.width, n.height}));
  };
  for (std::size_t i = 0; i < graph.nodes.size(); ++i)
    if (is_adornment[i]) place(place, graph.nodes[i]);

  const auto rotations = view_rotations(graph, hints);
  auto turn = [&](const std::string& id) {
    auto it = rotations.find(id);
    return it == rotations.end() ? 0.0 : it->second;
  };
  for (const auto& [id, degrees] : rotations) g.rotations.set(id, degrees);

  std::vector<std::string> leaves;
  for (const auto& n : graph.nodes)
    if (!children_of(n.id)) leaves.push_back(n.id);
  for (const auto& e : graph.edges) {
    const Box* s = nodes.find(e.source);
    const Box* t = nodes.find(e.target);
    if (!s || !t) continue;
    auto route_it = routes.find(e.id);
    const RouteHint* route = route_it == routes.end() ? nullptr : route_it->second;
    std::vector<Point> via;
    if (route)
      for (const auto& p : route->points) via.push_back({p.x, p.y});
    std::vector<Point> points;
    if (e.routing == EdgeRouting::Orthogonal && via.empty() && !(route && route->exit) && !(route && route->entry)) {
      std::vector<Box> obstacles;
      for (const auto& id : leaves)
        if (id != e.source && id != e.target) obstacles.push_back(rotated_bounds(*nodes.find(id), turn(id)));
      points = route_edge(rotated_bounds(*s, turn(e.source)), rotated_bounds(*t, turn(e.target)), obstacles);
    } else {
      // On a turned box, `from` and `to` turn with it, and lines meet its turned outline.
      double ts = turn(e.source), tt = turn(e.target);
      Point start = route && route->exit ? rotate_point(relative(*s, *route->exit), center(*s), ts)
                                         : boundary_point(*s, via.empty() ? center(*t) : via.front(), ts);
      Point end = route && route->entry ? rotate_point(relative(*t, *route->entry), center(*t), tt)
                                        : boundary_point(*t, via.empty() ? center(*s) : via.back(), tt);
      points.push_back(start);
      points.insert(points.end(), via.begin(), via.end());
      points.push_back(end);
      if (e.routing == EdgeRouting::Orthogonal) points = orthogonalize(points);
    }
    EdgeRoute r;
    if (e.label) r.label = e.label_anchor ? label_at(points, *e.label, *e.label_anchor) : label_on_route(points, *e.label);
    r.points = std::move(points);
    g.edges.set(e.id, std::move(r));
  }

  double width = 0, height = 0;
  for (const auto& id : nodes.keys()) {
    Box b = rotated_bounds(*nodes.find(id), turn(id));
    width = std::max(width, b.x + b.width);
    height = std::max(height, b.y + b.height);
  }
  for (const auto& r : g.edges)
    for (const auto& p : r.points) {
      width = std::max(width, p.x);
      height = std::max(height, p.y);
    }
  g.width = width;
  g.height = height;
  for (const auto& h : hints) {
    if (std::holds_alternative<RouteHint>(h) || std::holds_alternative<PinAtHint>(h)) continue;
    if (!hint_satisfied(h, nodes, graph.options.direction)) g.ignored_hints.push_back(h);
  }
  return g;
}

namespace {

/// How far a point lies outside a node's outline: > 0 outside, <= 0 on or inside. Only shapes
/// whose outline leaves the box's edges are handled; others return nullopt.
std::optional<double> outside(const std::string& shape, const Box& b, Point p, double degrees = 0) {
  double cx = b.x + b.width / 2, cy = b.y + b.height / 2;
  if (degrees != 0) {
    // In the shape's own, unturned frame; a turned box's outline leaves its bounds whatever the shape.
    p = rotate_point(p, {cx, cy}, -degrees);
    if (auto o = outside(shape, b, p)) return o;
    return std::max(std::abs(p.x - cx) / (b.width / 2), std::abs(p.y - cy) / (b.height / 2)) - 1;
  }
  if (shape == "rhombus")
    return std::abs(p.x - cx) / (b.width / 2) + std::abs(p.y - cy) / (b.height / 2) - 1;
  if (shape == "ellipse") {
    double dx = (p.x - cx) / (b.width / 2), dy = (p.y - cy) / (b.height / 2);
    return dx * dx + dy * dy - 1;
  }
  if (shape == "circle" || shape == "interface") {
    double r = std::min(b.width, b.height) / 2;
    return std::hypot(p.x - cx, p.y - cy) / r - 1;
  }
  return std::nullopt;
}

/// Moves a route's end from the box's edge onto the outline: along the end segment, away from
/// its neighbour `from`, to where it first meets the shape. Leaves it when the line misses.
void clip_end(Point& end, Point from, const std::string& shape, const Box& b, double degrees = 0) {
  auto outside = [&](const std::string& sh, const Box& box, Point p) { return dd::outside(sh, box, p, degrees); };
  if (!outside(shape, b, end) || *outside(shape, b, end) <= 0) return;
  double dx = end.x - from.x, dy = end.y - from.y, len = std::hypot(dx, dy);
  if (len == 0) return;
  dx /= len;
  dy /= len;
  auto at = [&](double t) { return Point{end.x + dx * t, end.y + dy * t}; };
  double reach = b.width + b.height, step = 0.5, lo = 0, hi = -1;
  for (double t = step; t <= reach; t += step)
    if (*outside(shape, b, at(t)) <= 0) {
      hi = t;
      lo = t - step;
      break;
    }
  if (hi < 0) return;
  for (int i = 0; i < 30; ++i) {
    double mid = (lo + hi) / 2;
    (*outside(shape, b, at(mid)) <= 0 ? hi : lo) = mid;
  }
  Point p = at(hi);
  end = {js_round(p.x * 100) / 100, js_round(p.y * 100) / 100};
}

}  // namespace

/// Layered routes start and end on the nodes' boxes; diamonds, ellipses and circles are smaller
/// than their boxes, so their edges are carried on to the outline.
void clip_to_outlines(Geometry& g, const LayoutGraph& graph, const ResolvedView& view) {
  std::unordered_map<std::string, std::string> shapes;
  for (const auto& e : view.elements) shapes[e.id] = base_shape(e.shape, e.kind);
  auto shape_of = [&](const std::string& id) {
    auto it = shapes.find(id);
    return it == shapes.end() ? std::string() : it->second;
  };
  auto turn = [&](const std::string& id) {
    const double* d = g.rotations.find(id);
    return d ? *d : 0.0;
  };
  for (const auto& e : graph.edges) {
    const EdgeRoute* found = g.edges.find(e.id);
    if (!found || found->points.size() < 2 || e.source_port || e.target_port) continue;
    EdgeRoute route = *found;
    auto& pts = route.points;
    if (const Box* b = g.nodes.find(e.source)) clip_end(pts.front(), pts[1], shape_of(e.source), *b, turn(e.source));
    if (const Box* b = g.nodes.find(e.target))
      clip_end(pts.back(), pts[pts.size() - 2], shape_of(e.target), *b, turn(e.target));
    g.edges.set(e.id, std::move(route));
  }
}

namespace {

constexpr double kDecorationGap = 24;
constexpr double kBoxPadding = 16;
constexpr double kBoxHeader = 20;

bool overlaps(const Box& a, const Box& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

/// Rough size without a renderer: 7 px a character, 16 px a line.
Size estimate(const Decoration& d) {
  std::string_view text = d.text ? std::string_view(*d.text) : std::string_view();
  double widest = 0, lines = 1, current = 0;
  for (char c : text) {
    if (c == '\n') {
      ++lines;
      current = 0;
    } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      widest = std::max(widest, ++current);
    }
  }
  return {std::max(40.0, widest * 7 + 24), lines * 16 + 16};
}

}  // namespace

void place_decorations(Geometry& g, const ResolvedView& view, const Measure& measure) {
  g.decorations.clear();
  const auto& decorations = view.view.decorations;
  if (decorations.empty()) return;
  auto size_of = [&](const Decoration& d) {
    Size s = measure.decoration ? measure.decoration(d) : estimate(d);
    if (d.width) s.width = *d.width;
    if (d.height) s.height = *d.height;
    return s;
  };
  // Placed notes and text must not cover nodes, edge labels or each other; boxes may (that is
  // their job).
  std::vector<Box> taken;
  for (const auto& id : g.nodes.keys())
    if (id.rfind("group:", 0) != 0) taken.push_back(*g.nodes.find(id));
  for (const auto& e : g.edges)
    if (e.label) taken.push_back(*e.label);
  auto clear = [&](const Box& b, const Box* except) {
    for (const auto& t : taken)
      if (!(except && t == *except) && overlaps(b, t)) return false;
    return true;
  };
  auto anchored = [&](const Decoration& d) {
    return d.kind == DecorationKind::Box || d.at || (d.placement && d.reference && g.nodes.find(*d.reference));
  };

  // First everything with a place of its own: boxes, positioned items and attached ones.
  for (std::size_t i = 0; i < decorations.size(); ++i) {
    const Decoration& d = decorations[i];
    if (!anchored(d)) continue;
    DecorationGeometry out;
    out.index = i;
    if (d.kind == DecorationKind::Box) {
      if (!d.around.empty()) {
        double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
        for (const auto& id : d.around) {
          const Box* b = g.nodes.find(id);
          if (!b) continue;
          x0 = std::min(x0, b->x);
          y0 = std::min(y0, b->y);
          x1 = std::max(x1, b->x + b->width);
          y1 = std::max(y1, b->y + b->height);
        }
        if (x0 == kInf) continue;  // nothing it frames is in the view
        double header = d.text && !d.text->empty() ? kBoxHeader : 0;
        out.box = {x0 - kBoxPadding, y0 - kBoxPadding - header, x1 - x0 + 2 * kBoxPadding,
                   y1 - y0 + 2 * kBoxPadding + header};
      } else if (d.at && d.width && d.height) {
        out.box = {d.at->x, d.at->y, *d.width, *d.height};
      } else {
        continue;
      }
      g.decorations.push_back(std::move(out));
      continue;
    }
    Size size = size_of(d);
    if (d.at) {
      out.box = {d.at->x, d.at->y, size.width, size.height};
    } else {
      const Box r = *g.nodes.find(*d.reference);
      Box b{0, 0, size.width, size.height};
      switch (*d.placement) {
        case Placement::Below: b.x = r.x + r.width / 2 - size.width / 2; b.y = r.y + r.height + kDecorationGap; break;
        case Placement::Above: b.x = r.x + r.width / 2 - size.width / 2; b.y = r.y - kDecorationGap - size.height; break;
        case Placement::RightOf: b.x = r.x + r.width + kDecorationGap; b.y = r.y + r.height / 2 - size.height / 2; break;
        case Placement::LeftOf: b.x = r.x - kDecorationGap - size.width; b.y = r.y + r.height / 2 - size.height / 2; break;
      }
      // Step outwards, away from the element, until nothing is covered.
      for (int step = 0; step < 100 && !clear(b, &r); ++step) {
        switch (*d.placement) {
          case Placement::Below: b.y += 8; break;
          case Placement::Above: b.y -= 8; break;
          case Placement::RightOf: b.x += 8; break;
          case Placement::LeftOf: b.x -= 8; break;
        }
      }
      out.box = b;
      if (d.kind == DecorationKind::Note) {
        Point from = center(b), to = center(r);
        out.connector = {boundary_point(b, to), boundary_point(r, from)};
      }
    }
    taken.push_back(out.box);
    g.decorations.push_back(std::move(out));
  }

  // Then the rest, around the extent of everything so far: text above, notes in a column on the
  // right.
  double min_x = kInf, min_y = kInf, max_x = -kInf;
  auto extend = [&](const Box& b) {
    min_x = std::min(min_x, b.x);
    min_y = std::min(min_y, b.y);
    max_x = std::max(max_x, b.x + b.width);
  };
  for (const auto& b : g.nodes) extend(b);
  for (const auto& e : g.edges)
    if (e.label) extend(*e.label);
  for (const auto& d : g.decorations) extend(d.box);
  if (g.sequence) {
    min_x = std::min(min_x, g.sequence->left);
    max_x = std::max(max_x, g.sequence->right);
  }
  if (min_x == kInf) min_x = min_y = max_x = 0;
  double text_height = 0;
  for (const auto& d : decorations)
    if (d.kind == DecorationKind::Text && !anchored(d)) text_height += size_of(d).height + 8;
  double text_y = min_y - kDecorationGap - text_height + 8;
  double note_y = min_y;
  for (std::size_t i = 0; i < decorations.size(); ++i) {
    const Decoration& d = decorations[i];
    if (anchored(d)) continue;
    Size size = size_of(d);
    DecorationGeometry out;
    out.index = i;
    if (d.kind == DecorationKind::Text) {
      out.box = {min_x, text_y, size.width, size.height};
      text_y += size.height + 8;
    } else {
      Box b{max_x + kDecorationGap * 2, note_y, size.width, size.height};
      while (!clear(b, nullptr)) b.y += 8;
      out.box = b;
      note_y = b.y + size.height + 12;
    }
    taken.push_back(out.box);
    g.decorations.push_back(std::move(out));
  }
  std::stable_sort(g.decorations.begin(), g.decorations.end(),
                   [](const auto& a, const auto& b) { return a.index < b.index; });
}

Result<Geometry> layout(const ResolvedView& view, const Measure& measure) {
  Geometry g = layout_nodes(view, measure);
  place_decorations(g, view, measure);
  return g;
}

Geometry layout_nodes(const ResolvedView& view, const Measure& measure, const Geometry* previous) {
  const auto& algorithm = view.view.layout.algorithm;
  Geometry g;
  if (algorithm == "sequence") {
    // Participants take their `size`; they are not turned.
    Measure sized = measure;
    std::map<std::string, Size, std::less<>> sizes;
    for (const auto& h : view.view.hints)
      if (const auto* size = std::get_if<SizeHint>(&h)) sizes[size->element] = {size->width, size->height};
    if (!sizes.empty())
      sized.node = [&measure, sizes](const Element& e) {
        auto it = sizes.find(e.id);
        return it == sizes.end() ? measure.node(e) : it->second;
      };
    g = sequence_layout(view, sized);
  } else if (algorithm == "fixed") {
    g = fixed_layout(to_layout_graph(view, measure), view.view.hints);
  } else {
    // tree, grid, radial and force have engines of their own; every other name runs layered.
    LayoutGraph graph = to_layout_graph(view, measure);
    // A turned node takes the room of its turned shape: the engine places a box that size, and
    // the node sits in its middle. A `pin … at` is where the node goes, so its slot is moved by
    // as much as it is larger.
    const auto rotations = view_rotations(graph, view.view.hints);
    std::map<std::string, Size, std::less<>> unturned;
    for (auto& n : graph.nodes) {
      auto it = rotations.find(n.id);
      if (it == rotations.end()) continue;
      unturned[n.id] = {n.width, n.height};
      Box slot = rotated_bounds({0, 0, n.width, n.height}, it->second);
      n.width = slot.width;
      n.height = slot.height;
    }
    std::vector<LayoutHint> hints = view.view.hints;
    for (auto& h : hints)
      if (auto* at = std::get_if<PinAtHint>(&h); at && unturned.count(at->element)) {
        const Size& size = unturned.at(at->element);
        Box slot = rotated_bounds({at->x, at->y, size.width, size.height}, rotations.at(at->element));
        at->x = slot.x;
        at->y = slot.y;
      }
    g = graph_layout(graph, hints, previous);
    for (const auto& [id, size] : unturned) {
      const Box* slot = g.nodes.find(id);
      if (!slot) continue;
      Point c = center(*slot);
      g.nodes.set(id, Box{c.x - size.width / 2, c.y - size.height / 2, size.width, size.height});
      g.rotations.set(id, rotations.at(id));
    }
    // Hints the engine reported were weighed against the slots; report them as written.
    for (auto& h : g.ignored_hints)
      if (auto* at = std::get_if<PinAtHint>(&h))
        for (const auto& written : view.view.hints)
          if (const auto* w = std::get_if<PinAtHint>(&written); w && w->element == at->element) *at = *w;
    clip_to_outlines(g, graph, view);
  }
  // A turn the drawing does not show (a container's, or in a sequence view) is reported.
  for (const auto& h : view.view.hints)
    if (const auto* turn = std::get_if<RotateHint>(&h);
        turn && std::fmod(turn->degrees, 360) != 0 && !g.rotations.contains(turn->element))
      g.ignored_hints.push_back(h);
  return g;
}

}  // namespace dd
