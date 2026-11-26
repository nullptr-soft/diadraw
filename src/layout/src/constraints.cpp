// Port of packages/layout/src/constraints.ts: hints the engine did not honour are applied after
// layout. `pin … at` moves a node to its coordinates, `align` lines nodes up on the first node's
// centre line, `pin` puts a node beside its reference and `order` swaps nodes into the listed
// order. Nodes the moves land on are pushed aside across the flow, containers are refitted and
// every edge touching a moved node is rerouted around the others.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "diadraw/layout/layout.hpp"

namespace dd {

namespace {

constexpr double kGap = 40;  // between a pinned node and its reference, and pushed-apart nodes
constexpr double kPadTop = 32, kPadLeft = 16, kPadBottom = 16, kPadRight = 16;

Point center(const Box& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

bool overlaps(const Box& a, const Box& b, double gap) {
  return a.x < b.x + b.width + gap && b.x < a.x + a.width + gap && a.y < b.y + b.height + gap &&
         b.y < a.y + a.height + gap;
}

Box place_beside(Box el, const Box& ref, Placement placement) {
  Point c = center(ref);
  switch (placement) {
    case Placement::Below: el.x = c.x - el.width / 2; el.y = ref.y + ref.height + kGap; break;
    case Placement::Above: el.x = c.x - el.width / 2; el.y = ref.y - kGap - el.height; break;
    case Placement::RightOf: el.x = ref.x + ref.width + kGap; el.y = c.y - el.height / 2; break;
    case Placement::LeftOf: el.x = ref.x - kGap - el.width; el.y = c.y - el.height / 2; break;
  }
  return el;
}

}  // namespace

Geometry apply_constraints(const LayoutGraph& graph, const Geometry& geometry, const std::vector<LayoutHint>& hints) {
  const Direction direction = graph.options.direction;
  std::vector<const LayoutHint*> unmet;
  for (const auto& h : hints)
    if (!hint_satisfied(h, geometry.nodes, direction)) unmet.push_back(&h);
  if (unmet.empty()) return geometry;

  Geometry out = geometry;
  auto& nodes = out.nodes;
  std::map<std::string, std::vector<std::string>, std::less<>> children;
  std::vector<std::string> parent_order;
  std::map<std::string, std::string, std::less<>> parent_of;
  for (const auto& n : graph.nodes) {
    if (!n.parent) continue;
    parent_of[n.id] = *n.parent;
    if (!children.count(*n.parent)) parent_order.push_back(*n.parent);
    children[*n.parent].push_back(n.id);
  }
  auto is_leaf = [&](const std::string& id) { return !children.count(id); };
  std::function<void(const std::string&, std::vector<std::string>&)> descendants = [&](const std::string& id,
                                                                                         std::vector<std::string>& acc) {
    auto it = children.find(id);
    if (it == children.end()) return;
    for (const auto& c : it->second) {
      acc.push_back(c);
      descendants(c, acc);
    }
  };
  std::set<std::string, std::less<>> moved, fixed;

  auto move_to = [&](const std::string& id, const Box& box) {
    const Box* old = nodes.find(id);
    if (!old) return;
    double dx = box.x - old->x, dy = box.y - old->y;
    if (std::fabs(dx) < 0.01 && std::fabs(dy) < 0.01) return;
    std::vector<std::string> all{id};
    descendants(id, all);
    for (const auto& n : all) {
      const Box* b = nodes.find(n);
      if (!b) continue;
      nodes.set(n, Box{b->x + dx, b->y + dy, b->width, b->height});
      moved.insert(n);
    }
  };

  const bool across_x = direction == Direction::Down || direction == Direction::Up;
  for (const LayoutHint* hint : unmet) {
    if (const auto* h = std::get_if<PinAtHint>(hint)) {
      if (const Box* b = nodes.find(h->element)) move_to(h->element, Box{h->x, h->y, b->width, b->height});
      fixed.insert(h->element);
    } else if (const auto* pin = std::get_if<PinHint>(hint)) {
      const Box* el = nodes.find(pin->element);
      const Box* ref = nodes.find(pin->reference);
      if (el && ref) move_to(pin->element, place_beside(*el, *ref, pin->placement));
      fixed.insert(pin->element);
    } else if (const auto* align = std::get_if<AlignHint>(hint)) {
      if (align->elements.empty()) continue;
      const Box* anchor = nodes.find(align->elements.front());
      if (!anchor) continue;
      Point c = center(*anchor);
      for (std::size_t i = 1; i < align->elements.size(); ++i) {
        const std::string& id = align->elements[i];
        const Box* b = nodes.find(id);
        if (!b) continue;
        Box target = *b;
        if (align->axis == Axis::Horizontal)
          target.y = c.y - b->height / 2;
        else
          target.x = c.x - b->width / 2;
        move_to(id, target);
        fixed.insert(id);
      }
    } else if (const auto* order = std::get_if<OrderHint>(hint)) {
      // Reuse the nodes' own slots across the flow, assigned in the listed order.
      std::vector<Box> boxes;
      for (const auto& id : order->elements)
        if (const Box* b = nodes.find(id)) boxes.push_back(*b);
      if (boxes.size() != order->elements.size()) continue;
      std::vector<double> slots;
      for (const auto& b : boxes) slots.push_back(across_x ? center(b).x : center(b).y);
      std::stable_sort(slots.begin(), slots.end());
      for (std::size_t i = 0; i < order->elements.size(); ++i) {
        const std::string& id = order->elements[i];
        Box b = *nodes.find(id);
        if (across_x)
          b.x = slots[i] - b.width / 2;
        else
          b.y = slots[i] - b.height / 2;
        move_to(id, b);
        fixed.insert(id);
      }
    }
  }

  // Push nodes that a move landed on out of the way, across the flow.
  std::vector<std::string> leaves;
  for (const auto& n : graph.nodes)
    if (is_leaf(n.id) && nodes.contains(n.id)) leaves.push_back(n.id);
  for (int pass = 0; pass < 8; ++pass) {
    bool changed = false;
    for (const auto& a : leaves) {
      if (!moved.count(a)) continue;
      for (const auto& b : leaves) {
        if (a == b) continue;
        Box A = *nodes.find(a), B = *nodes.find(b);
        if (!overlaps(A, B, kGap / 4)) continue;
        // Move whichever node is not pinned; prefer moving the one that was not dragged.
        const std::string& victim = fixed.count(b) && !fixed.count(a) ? a : b;
        const Box& other = victim == a ? B : A;
        Box v = *nodes.find(victim);
        double vc = across_x ? center(v).x : center(v).y;
        double oc = across_x ? center(other).x : center(other).y;
        double v_pos = across_x ? v.x : v.y;
        double shift = vc >= oc ? (across_x ? other.x + other.width : other.y + other.height) + kGap / 2 - v_pos
                                : (across_x ? other.x - v.width : other.y - v.height) - kGap / 2 - v_pos;
        if (across_x)
          v.x += shift;
        else
          v.y += shift;
        move_to(victim, v);
        changed = true;
      }
    }
    if (!changed) break;
  }

  // Refit containers around their children, innermost first.
  std::function<int(const std::string&)> depth = [&](const std::string& id) {
    auto it = parent_of.find(id);
    return it == parent_of.end() ? 0 : 1 + depth(it->second);
  };
  std::vector<std::string> containers = parent_order;
  std::stable_sort(containers.begin(), containers.end(),
                   [&](const std::string& a, const std::string& b) { return depth(b) < depth(a); });
  for (const auto& id : containers) {
    std::vector<Box> kids;
    bool any_moved = false;
    for (const auto& k : children[id]) {
      if (const Box* b = nodes.find(k)) {
        kids.push_back(*b);
        if (moved.count(k)) any_moved = true;
      }
    }
    if (kids.empty() || !any_moved) continue;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& k : kids) {
      x0 = std::min(x0, k.x);
      y0 = std::min(y0, k.y);
      x1 = std::max(x1, k.x + k.width);
      y1 = std::max(y1, k.y + k.height);
    }
    x0 -= kPadLeft;
    y0 -= kPadTop;
    x1 += kPadRight;
    y1 += kPadBottom;
    nodes.set(id, Box{x0, y0, x1 - x0, y1 - y0});
    moved.insert(id);
  }

  // Reroute edges that touch anything that moved.
  for (const auto& e : graph.edges) {
    if (!moved.count(e.source) && !moved.count(e.target)) continue;
    const Box* s = nodes.find(e.source);
    const Box* t = nodes.find(e.target);
    if (!s || !t) continue;
    std::set<std::string, std::less<>> skip{e.source, e.target};
    for (const std::string* id : {&e.source, &e.target})
      for (auto it = parent_of.find(*id); it != parent_of.end(); it = parent_of.find(it->second)) skip.insert(it->second);
    std::vector<Box> obstacles;
    for (const auto& id : leaves)
      if (!skip.count(id)) obstacles.push_back(*nodes.find(id));
    EdgeRoute route;
    route.points = route_edge(*s, *t, obstacles);
    const EdgeRoute* old = geometry.edges.find(e.id);
    if (old && old->label) {
      auto around = obstacles;
      around.push_back(*s);
      around.push_back(*t);
      route.label = label_on_route(route.points, {old->label->width, old->label->height}, around);
    }
    out.edges.set(e.id, std::move(route));
  }

  for (const auto& b : nodes) {
    out.width = std::max(out.width, b.x + b.width);
    out.height = std::max(out.height, b.y + b.height);
  }
  for (const auto& r : out.edges)
    for (const auto& p : r.points) {
      out.width = std::max(out.width, p.x);
      out.height = std::max(out.height, p.y);
    }
  out.ignored_hints.clear();
  for (const auto& h : hints)
    if (!hint_satisfied(h, nodes, direction)) out.ignored_hints.push_back(h);
  return out;
}

}  // namespace dd
