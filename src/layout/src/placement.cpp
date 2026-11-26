// The tree, grid, radial and force engines (`layout tree|grid|radial|force`).
//
// They share one frame: every container is laid out on its own, innermost first, with edges
// between its children (edges reaching deeper are lifted to the child that holds the endpoint);
// the container is then sized around its children, as the layered engine does. An engine only
// places the boxes of one level; edges are routed here afterwards, straight or orthogonal, and
// the constraint pass applies the hints an engine missed.
//
// Everything is deterministic: no randomness, fixed iteration counts, and coordinates that go
// through trigonometry are rounded to hundredths so every platform draws the same picture.

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>

#include "diadraw/base/json.hpp"
#include "diadraw/layout/layout.hpp"

namespace dd {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.14159265358979323846;
constexpr double kRootPadding = 12;
// Container padding, as in the layered engine's final picture: room for the title on top.
constexpr double kPadTop = 32, kPadSide = 16, kPadBottom = 16;

double round2(double v) { return js_round(v * 100) / 100; }

/// An edge between two members of a level, after lifting.
struct LevelEdge {
  int s = 0;
  int t = 0;
  /// The largest label among the graph edges it stands for (0×0 without one).
  Size label;
};

/// What an engine returns for one level: the top-left corner of each member (any origin), and
/// optionally its own route (same coordinates) for some of the level's edges.
struct Arrangement {
  std::vector<Point> at;
  std::map<int, std::vector<Point>> routes;  ///< by index into the level's edges
};

using Engine =
    std::function<Arrangement(const std::vector<Size>&, const std::vector<LevelEdge>&, const LayoutOptions&)>;

enum class Routing { Straight, Orthogonal };

// ── Shared frame ─────────────────────────────────────────────────────────────────────────────

class Frame {
 public:
  Frame(const LayoutGraph& graph, Engine engine, Routing routing)
      : g_(graph), engine_(std::move(engine)), routing_(routing) {
    const int n = static_cast<int>(g_.nodes.size());
    for (int i = 0; i < n; ++i) index_[g_.nodes[i].id] = i;
    parent_.assign(static_cast<std::size_t>(n), -1);
    kids_.assign(static_cast<std::size_t>(n), {});
    for (int i = 0; i < n; ++i) {
      const auto& p = g_.nodes[i].parent;
      if (p && index_.count(*p) && index_.at(*p) != i) {
        parent_[i] = index_.at(*p);
        kids_[parent_[i]].push_back(i);
      } else {
        roots_.push_back(i);
      }
    }
    rel_.resize(static_cast<std::size_t>(n));
  }

  Geometry run() {
    Size root = level(-1);
    std::vector<Box> abs(g_.nodes.size());
    std::function<void(int, Point)> place = [&](int v, Point origin) {
      abs[v] = Box{origin.x + rel_[v].x, origin.y + rel_[v].y, rel_[v].width, rel_[v].height};
      for (int k : kids_[v]) place(k, {abs[v].x + kPadSide, abs[v].y + kPadTop});
    };
    for (int r : roots_) place(r, {kRootPadding, kRootPadding});

    Geometry g;
    for (std::size_t v = 0; v < g_.nodes.size(); ++v) g.nodes.set(g_.nodes[v].id, abs[v]);

    std::vector<int> leaves;
    for (std::size_t v = 0; v < g_.nodes.size(); ++v)
      if (kids_[v].empty()) leaves.push_back(static_cast<int>(v));
    std::set<int> engine_routed;
    for (const auto& e : g_.edges) {
      if (!index_.count(e.source) || !index_.count(e.target)) continue;
      int s = index_.at(e.source), t = index_.at(e.target);
      const Box& sb = abs[s];
      const Box& tb = abs[t];
      std::set<int> skip{s, t};
      for (int p = parent_[s]; p >= 0; p = parent_[p]) skip.insert(p);
      for (int p = parent_[t]; p >= 0; p = parent_[p]) skip.insert(p);
      std::vector<Box> obstacles;
      for (int v : leaves)
        if (!skip.count(v)) obstacles.push_back(abs[v]);

      EdgeRoute r;
      bool label_on_last = false;
      auto own = own_routes_.find({s, t});
      if (s == t) {
        r.points = self_loop(sb);
      } else if (own != own_routes_.end() && !e.source_port && !e.target_port &&
                 !engine_routed.count(own->second.first)) {
        engine_routed.insert(own->second.first);
        Point origin = parent_[s] < 0 ? Point{kRootPadding, kRootPadding}
                                      : Point{abs[parent_[s]].x + kPadSide, abs[parent_[s]].y + kPadTop};
        for (const auto& p : own->second.second) r.points.push_back({origin.x + p.x, origin.y + p.y});
        label_on_last = true;
      } else if (routing_ == Routing::Orthogonal || e.routing == EdgeRouting::Orthogonal || e.source_port ||
                 e.target_port) {
        r.points = route_edge(sb, tb, obstacles, port_side(s, e.source_port), port_side(t, e.target_port));
      } else {
        Point sc{sb.x + sb.width / 2, sb.y + sb.height / 2};
        Point tc{tb.x + tb.width / 2, tb.y + tb.height / 2};
        Point a = boundary_point(sb, tc), b = boundary_point(tb, sc);
        r.points = {{round2(a.x), round2(a.y)}, {round2(b.x), round2(b.y)}};
      }
      if (e.label && label_on_last && r.points.size() >= 2) {
        const Point& a = r.points[r.points.size() - 2];
        const Point& b = r.points.back();
        r.label = Box{(a.x + b.x) / 2 - e.label->width / 2, (a.y + b.y) / 2 - e.label->height / 2, e.label->width,
                      e.label->height};
      } else if (e.label) {
        auto around = obstacles;
        around.push_back(sb);
        around.push_back(tb);
        r.label = label_on_route(r.points, *e.label, around);
      }
      g.edges.set(e.id, std::move(r));
    }

    g.width = root.width + 2 * kRootPadding;
    g.height = root.height + 2 * kRootPadding;
    for (const auto& b : g.nodes) {
      g.width = std::max(g.width, b.x + b.width + kRootPadding);
      g.height = std::max(g.height, b.y + b.height + kRootPadding);
    }
    for (const auto& r : g.edges) {
      for (const auto& p : r.points) {
        g.width = std::max(g.width, p.x + kRootPadding);
        g.height = std::max(g.height, p.y + kRootPadding);
      }
      if (r.label) {
        g.width = std::max(g.width, r.label->x + r.label->width + kRootPadding);
        g.height = std::max(g.height, r.label->y + r.label->height + kRootPadding);
      }
    }
    return g;
  }

 private:
  /// The member of `container`'s children (or of the roots, for -1) that holds node `v`.
  int member_of(int container, int v) const {
    for (int cur = v; cur >= 0; cur = parent_[cur])
      if (parent_[cur] == container) return cur;
    return -1;
  }

  /// Lays out the children of `container` (the roots for -1) and returns the size they take.
  Size level(int container) {
    const std::vector<int>& members = container < 0 ? roots_ : kids_[container];
    std::map<int, int> local;
    std::vector<Size> sizes;
    for (int m : members) {
      local[m] = static_cast<int>(sizes.size());
      if (kids_[m].empty()) {
        sizes.push_back({g_.nodes[m].width, g_.nodes[m].height});
      } else {
        Size inner = level(m);
        sizes.push_back({inner.width + 2 * kPadSide, inner.height + kPadTop + kPadBottom});
      }
    }
    if (members.empty()) return {0, 0};

    std::vector<LevelEdge> edges;
    std::vector<std::pair<int, int>> direct;  ///< graph node pair of each level edge, when not lifted
    std::set<std::pair<int, int>> seen;
    for (const auto& e : g_.edges) {
      if (!index_.count(e.source) || !index_.count(e.target)) continue;
      int s = index_.at(e.source), t = index_.at(e.target);
      int a = member_of(container, s), b = member_of(container, t);
      if (a < 0 || b < 0 || a == b) continue;
      Size label = e.label.value_or(Size{0, 0});
      if (!seen.insert({a, b}).second) {
        for (auto& le : edges)
          if (le.s == local[a] && le.t == local[b])
            le.label = {std::max(le.label.width, label.width), std::max(le.label.height, label.height)};
        continue;
      }
      edges.push_back({local[a], local[b], label});
      direct.push_back(a == s && b == t ? std::pair{s, t} : std::pair{-1, -1});
    }

    Arrangement p = engine_(sizes, edges, g_.options);
    double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
    for (std::size_t k = 0; k < members.size(); ++k) {
      x0 = std::min(x0, p.at[k].x);
      y0 = std::min(y0, p.at[k].y);
      x1 = std::max(x1, p.at[k].x + sizes[k].width);
      y1 = std::max(y1, p.at[k].y + sizes[k].height);
    }
    for (std::size_t k = 0; k < members.size(); ++k)
      rel_[members[k]] = Box{p.at[k].x - x0, p.at[k].y - y0, sizes[k].width, sizes[k].height};
    for (auto& [ei, points] : p.routes) {
      if (direct[ei].first < 0) continue;
      std::vector<Point> shifted;
      for (const auto& pt : points) shifted.push_back({pt.x - x0, pt.y - y0});
      own_routes_[direct[ei]] = {static_cast<int>(own_routes_.size()), std::move(shifted)};
    }
    return {x1 - x0, y1 - y0};
  }

  static std::vector<Point> self_loop(const Box& b) {
    double x = b.x + b.width, q = b.height / 4, cy = b.y + b.height / 2;
    return {{x, cy - q}, {x + 20, cy - q}, {x + 20, cy + q}, {x, cy + q}};
  }

  std::optional<PortSide> port_side(int v, const std::optional<std::string>& port) const {
    if (!port) return std::nullopt;
    for (const auto& p : g_.nodes[v].ports)
      if (p.id == *port) return p.side;
    return std::nullopt;
  }

  const LayoutGraph& g_;
  Engine engine_;
  Routing routing_;
  std::map<std::string, int, std::less<>> index_;
  std::vector<int> parent_;
  std::vector<std::vector<int>> kids_;
  std::vector<int> roots_;
  std::vector<Box> rel_;
  std::map<std::pair<int, int>, std::pair<int, std::vector<Point>>> own_routes_;
};

Geometry run(const LayoutGraph& graph, const std::vector<LayoutHint>& hints, Engine engine, Routing routing) {
  Geometry g = Frame(graph, std::move(engine), routing).run();
  for (const auto& h : hints)
    if (!hint_satisfied(h, g.nodes, graph.options.direction)) g.ignored_hints.push_back(h);
  if (!g.ignored_hints.empty()) return apply_constraints(graph, g, hints);
  return g;
}

/// Flow coordinates: `across` and `along` the direction, mapped back to x and y.
struct Flow {
  Direction dir = Direction::Down;
  [[nodiscard]] bool horizontal() const { return dir == Direction::Right || dir == Direction::Left; }
  [[nodiscard]] Size internal(Size s) const { return horizontal() ? Size{s.height, s.width} : s; }
  /// Final point of an internal one, given the total extent along the flow.
  [[nodiscard]] Point point(Point p, double along_total) const {
    switch (dir) {
      case Direction::Down:
        return p;
      case Direction::Up:
        return {p.x, along_total - p.y};
      case Direction::Right:
        return {p.y, p.x};
      default:
        return {along_total - p.y, p.x};
    }
  }
  /// Final top-left corner of an internal box.
  [[nodiscard]] Point corner(const Box& b, double along_total) const {
    Point a = point({b.x, b.y}, along_total), c = point({b.x + b.width, b.y + b.height}, along_total);
    return {std::min(a.x, c.x), std::min(a.y, c.y)};
  }
};

// ── Grid ─────────────────────────────────────────────────────────────────────────────────────

/// Members in view order, in lines of ⌈√n⌉: rows for `down`/`up`, columns for `right`/`left`.
/// Each column is as wide as its widest member, each row as tall as its tallest; members are
/// centred in their cells.
Arrangement grid(const std::vector<Size>& sizes, const std::vector<LevelEdge>&, const LayoutOptions& options) {
  const double gap = options.spacing.value_or(60);
  const std::size_t n = sizes.size();
  const auto per = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(n))));
  Flow f{options.direction};
  // Internally: lines go along the flow, positions in a line go across it.
  std::vector<double> across(per, 0), along((n + per - 1) / per, 0);
  for (std::size_t i = 0; i < n; ++i) {
    Size s = f.internal(sizes[i]);
    across[i % per] = std::max(across[i % per], s.width);
    along[i / per] = std::max(along[i / per], s.height);
  }
  std::vector<double> x0(per, 0), y0(along.size(), 0);
  for (std::size_t k = 1; k < per; ++k) x0[k] = x0[k - 1] + across[k - 1] + gap;
  for (std::size_t k = 1; k < along.size(); ++k) y0[k] = y0[k - 1] + along[k - 1] + gap;
  double total = along.empty() ? 0 : y0.back() + along.back();
  Arrangement p;
  for (std::size_t i = 0; i < n; ++i) {
    Size s = f.internal(sizes[i]);
    std::size_t c = i % per, r = i / per;
    Box b{x0[c] + (across[c] - s.width) / 2, y0[r] + (along[r] - s.height) / 2, s.width, s.height};
    p.at.push_back(f.corner(b, total));
  }
  return p;
}

// ── Tree ─────────────────────────────────────────────────────────────────────────────────────

/// A spanning forest, breadth first: roots are members nothing points to (then, for cycles, the
/// first member not reached yet), children follow their edges in order.
struct Forest {
  std::vector<int> roots;
  std::vector<std::vector<int>> children;
  std::vector<int> depth;
  std::vector<int> tree_edge;  ///< level edge that made each member a child, or -1
};

Forest span(std::size_t n, const std::vector<LevelEdge>& edges, bool directed) {
  Forest f;
  f.children.assign(n, {});
  f.depth.assign(n, -1);
  f.tree_edge.assign(n, -1);
  std::vector<std::vector<std::pair<int, int>>> out(n);  ///< (neighbour, edge)
  std::vector<int> indegree(n, 0);
  for (std::size_t k = 0; k < edges.size(); ++k) {
    out[edges[k].s].push_back({edges[k].t, static_cast<int>(k)});
    if (!directed) out[edges[k].t].push_back({edges[k].s, static_cast<int>(k)});
    ++indegree[edges[k].t];
  }
  auto grow = [&](int root) {
    f.roots.push_back(root);
    f.depth[root] = 0;
    std::vector<int> queue{root};
    for (std::size_t q = 0; q < queue.size(); ++q) {
      int v = queue[q];
      for (auto [w, k] : out[v]) {
        if (f.depth[w] >= 0) continue;
        f.depth[w] = f.depth[v] + 1;
        f.tree_edge[w] = k;
        f.children[v].push_back(w);
        queue.push_back(w);
      }
    }
  };
  if (directed)
    for (std::size_t v = 0; v < n; ++v)
      if (indegree[v] == 0 && f.depth[v] < 0) grow(static_cast<int>(v));
  for (std::size_t v = 0; v < n; ++v)
    if (f.depth[v] < 0) grow(static_cast<int>(v));
  return f;
}

/// A tidy tree: each subtree gets a band as wide as its members need, parents are centred over
/// their children, and every depth is a rank as deep as its deepest member. Tree edges are drawn
/// org-chart style: down from the parent to a bus just past its rank, across, and down to the
/// child, with the label on that last stretch.
Arrangement tree(const std::vector<Size>& sizes, const std::vector<LevelEdge>& edges, const LayoutOptions& options) {
  const double gap = options.spacing.value_or(30);
  const double rank_gap = options.spacing.value_or(50);
  const std::size_t n = sizes.size();
  Flow fl{options.direction};
  Forest f = span(n, edges, true);
  std::vector<Size> in(n);
  for (std::size_t v = 0; v < n; ++v) in[v] = fl.internal(sizes[v]);

  std::vector<double> band(n, 0);
  std::function<double(int)> measure = [&](int v) {
    double kids = 0;
    for (std::size_t k = 0; k < f.children[v].size(); ++k) kids += (k ? gap : 0) + measure(f.children[v][k]);
    return band[v] = std::max(in[v].width, kids);
  };
  std::vector<double> x(n, 0);  ///< centre across the flow
  std::function<void(int, double)> place = [&](int v, double left) {
    const auto& kids = f.children[v];
    double total = 0;
    for (std::size_t k = 0; k < kids.size(); ++k) total += (k ? gap : 0) + band[kids[k]];
    double cursor = left + (band[v] - total) / 2;
    for (int k : kids) {
      place(k, cursor);
      cursor += band[k] + gap;
    }
    x[v] = kids.empty() ? left + band[v] / 2 : (x[kids.front()] + x[kids.back()]) / 2;
  };
  double cursor = 0;
  for (int r : f.roots) {
    measure(r);
    place(r, cursor);
    cursor += band[r] + gap;
  }

  int ranks = 0;
  for (int d : f.depth) ranks = std::max(ranks, d + 1);
  // The bus runs `bus` past the parent's rank; a labelled tree edge gets a stub long enough for
  // its label between the bus and the child.
  const double bus = std::min(20.0, rank_gap / 2);
  std::vector<double> deep(ranks, 0), top(ranks, 0), after(ranks, rank_gap);
  for (std::size_t v = 0; v < n; ++v) {
    deep[f.depth[v]] = std::max(deep[f.depth[v]], in[v].height);
    if (f.tree_edge[v] < 0) continue;
    Size label = fl.internal(edges[f.tree_edge[v]].label);
    if (label.height > 0) after[f.depth[v] - 1] = std::max(after[f.depth[v] - 1], bus + label.height + 24);
  }
  for (int d = 1; d < ranks; ++d) top[d] = top[d - 1] + deep[d - 1] + after[d - 1];
  double total = ranks ? top[ranks - 1] + deep[ranks - 1] : 0;

  Arrangement p;
  std::vector<Box> box(n);
  for (std::size_t v = 0; v < n; ++v) {
    int d = f.depth[v];
    box[v] = Box{x[v] - in[v].width / 2, top[d] + (deep[d] - in[v].height) / 2, in[v].width, in[v].height};
    p.at.push_back(fl.corner(box[v], total));
  }
  for (std::size_t v = 0; v < n; ++v) {
    if (f.tree_edge[v] < 0) continue;
    int u = edges[f.tree_edge[v]].s;
    double mid = top[f.depth[u]] + deep[f.depth[u]] + bus;
    std::vector<Point> pts{{x[u], box[u].y + box[u].height}, {x[u], mid}, {x[v], mid}, {x[v], box[v].y}};
    std::vector<Point> out;
    for (const auto& pt : pts) out.push_back(fl.point(pt, total));
    p.routes[f.tree_edge[v]] = simplify(out);
  }
  return p;
}

// ── Radial ───────────────────────────────────────────────────────────────────────────────────

/// Members as a tree around the best-connected one: depth d on ring d, each subtree in a wedge
/// as wide as its share of leaves. Rings are as far apart as their members need. Separate parts
/// of the graph sit side by side, in view order.
Arrangement radial(const std::vector<Size>& sizes, const std::vector<LevelEdge>& edges, const LayoutOptions& options) {
  const double gap = options.spacing.value_or(40);
  const std::size_t n = sizes.size();
  std::vector<int> degree(n, 0);
  for (const auto& e : edges) {
    ++degree[e.s];
    ++degree[e.t];
  }
  // Components, each grown from its best-connected member.
  std::vector<int> component(n, -1);
  std::vector<std::vector<std::pair<int, int>>> adj(n);
  for (std::size_t k = 0; k < edges.size(); ++k) {
    adj[edges[k].s].push_back({edges[k].t, static_cast<int>(k)});
    adj[edges[k].t].push_back({edges[k].s, static_cast<int>(k)});
  }
  for (auto& a : adj) std::stable_sort(a.begin(), a.end(), [](auto p, auto q) { return p.first < q.first; });
  std::vector<std::vector<int>> parts;
  for (std::size_t v = 0; v < n; ++v) {
    if (component[v] >= 0) continue;
    std::vector<int> queue{static_cast<int>(v)};
    component[v] = static_cast<int>(parts.size());
    for (std::size_t q = 0; q < queue.size(); ++q)
      for (auto [w, k] : adj[queue[q]])
        if (component[w] < 0) {
          component[w] = component[v];
          queue.push_back(w);
        }
    std::sort(queue.begin(), queue.end());
    parts.push_back(std::move(queue));
  }

  auto diag = [&](int v) { return std::hypot(sizes[v].width, sizes[v].height); };
  std::vector<Point> centre(n);
  std::vector<int> depth(n, -1);
  std::vector<std::vector<int>> children(n);
  Arrangement p;
  p.at.resize(n);
  double cursor = 0;
  for (const auto& part : parts) {
    int root = part.front();
    for (int v : part)
      if (degree[v] > degree[root]) root = v;
    depth[root] = 0;
    std::vector<int> order{root};
    for (std::size_t q = 0; q < order.size(); ++q)
      for (auto [w, k] : adj[order[q]])
        if (depth[w] < 0) {
          depth[w] = depth[order[q]] + 1;
          children[order[q]].push_back(w);
          order.push_back(w);
        }
    std::vector<double> leaves(n, 0);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      int v = *it;
      if (children[v].empty()) leaves[v] = 1;
      for (int c : children[v]) leaves[v] += leaves[c];
    }
    // Wedges: [start, start + width), the member at its middle.
    std::vector<double> start(n, 0), width(n, 0);
    start[root] = -kPi / 2;
    width[root] = 2 * kPi;
    for (int v : order) {
      double s = start[v];
      for (int c : children[v]) {
        width[c] = width[v] * leaves[c] / leaves[v];
        start[c] = s;
        s += width[c];
      }
    }
    int rings = 0;
    for (int v : order) rings = std::max(rings, depth[v] + 1);
    std::vector<double> widest(rings, 0), radius(rings, 0), label(rings, 0);
    for (int v : order) widest[depth[v]] = std::max(widest[depth[v]], diag(v));
    for (const auto& e : edges) {
      int d = std::max(depth[e.s], depth[e.t]);
      if (d > 0 && component[e.s] == component[root])
        label[d] = std::max(label[d], std::max(e.label.width, e.label.height));
    }
    for (int d = 1; d < rings; ++d) {
      double r = radius[d - 1] + (widest[d - 1] + widest[d]) / 2 + gap + label[d];
      for (int v : order) {
        if (depth[v] != d) continue;
        // Neighbours on the ring are a wedge apart: the chord across it must fit the member.
        double w = width[v];
        if (w < kPi) r = std::max(r, (diag(v) + gap) / (2 * std::sin(w / 2)));
      }
      radius[d] = r;
    }
    double x0 = kInf, x1 = -kInf;
    for (int v : order) {
      double angle = start[v] + width[v] / 2;
      double r = radius[depth[v]];
      centre[v] = depth[v] == 0 ? Point{0, 0} : Point{round2(r * std::cos(angle)), round2(r * std::sin(angle))};
      x0 = std::min(x0, centre[v].x - sizes[v].width / 2);
      x1 = std::max(x1, centre[v].x + sizes[v].width / 2);
    }
    for (int v : order) p.at[v] = {cursor - x0 + centre[v].x - sizes[v].width / 2, centre[v].y - sizes[v].height / 2};
    cursor += x1 - x0 + gap;
  }
  return p;
}

// ── Force ────────────────────────────────────────────────────────────────────────────────────

/// Fruchterman–Reingold from a grid start: members repel each other, edges pull their ends
/// together towards a length that fits both boxes, a weak pull keeps parts together, and the
/// movement cools to nothing over a fixed number of rounds. Overlaps left at the end are pushed
/// apart along the shorter way out.
Arrangement force(const std::vector<Size>& sizes, const std::vector<LevelEdge>& edges, const LayoutOptions& options) {
  const double gap = options.spacing.value_or(40);
  const std::size_t n = sizes.size();
  Arrangement p;
  if (n == 0) return p;
  // Start from the grid, with a small deterministic offset so symmetric starts can still move.
  LayoutOptions down = options;
  down.direction = Direction::Down;
  Arrangement start = grid(sizes, edges, down);
  std::vector<Point> c(n);
  for (std::size_t v = 0; v < n; ++v)
    c[v] = {start.at[v].x + sizes[v].width / 2 + static_cast<double>((v * 37) % 11) - 5,
            start.at[v].y + sizes[v].height / 2 + static_cast<double>((v * 53) % 7) - 3};
  std::vector<double> radius(n);
  double mean = 0;
  for (std::size_t v = 0; v < n; ++v) {
    radius[v] = std::hypot(sizes[v].width, sizes[v].height) / 2;
    mean += radius[v];
  }
  mean /= static_cast<double>(n);
  const double k = 2 * mean + gap;

  constexpr int kRounds = 300;
  constexpr double kRepulsion = 0.3;
  double temperature = k * 2;
  const double cooling = temperature / kRounds;
  std::vector<Point> push(n);
  for (int round = 0; round < kRounds; ++round) {
    std::fill(push.begin(), push.end(), Point{0, 0});
    for (std::size_t a = 0; a < n; ++a)
      for (std::size_t b = a + 1; b < n; ++b) {
        double dx = c[a].x - c[b].x, dy = c[a].y - c[b].y;
        double d = std::sqrt(dx * dx + dy * dy);
        if (d < 0.01) {
          dx = -1;
          dy = 0;
          d = 1;
        }
        // Distance between outlines (roughly), so big boxes keep further apart.
        double clear = std::max(d - radius[a] - radius[b] + 2 * mean, 1.0);
        double f = kRepulsion * k * k / clear;
        push[a].x += dx / d * f;
        push[a].y += dy / d * f;
        push[b].x -= dx / d * f;
        push[b].y -= dy / d * f;
      }
    for (const auto& e : edges) {
      double dx = c[e.t].x - c[e.s].x, dy = c[e.t].y - c[e.s].y;
      double d = std::sqrt(dx * dx + dy * dy);
      if (d < 0.01) continue;
      double ideal = radius[e.s] + radius[e.t] + gap + std::max(e.label.width, e.label.height);
      double f = d * d / ideal;
      push[e.s].x += dx / d * f;
      push[e.s].y += dy / d * f;
      push[e.t].x -= dx / d * f;
      push[e.t].y -= dy / d * f;
    }
    Point mid{0, 0};
    for (const auto& pt : c) {
      mid.x += pt.x / static_cast<double>(n);
      mid.y += pt.y / static_cast<double>(n);
    }
    for (std::size_t v = 0; v < n; ++v) {
      push[v].x += (mid.x - c[v].x) * 0.05 * k / (k + 1);
      push[v].y += (mid.y - c[v].y) * 0.05 * k / (k + 1);
      double len = std::sqrt(push[v].x * push[v].x + push[v].y * push[v].y);
      if (len < 1e-9) continue;
      double step = std::min(len, temperature);
      c[v].x += push[v].x / len * step;
      c[v].y += push[v].y / len * step;
    }
    temperature = std::max(temperature - cooling, 0.0);
  }

  // Overlap removal: keep `gap / 2` between boxes.
  const double margin = gap / 2;
  for (int pass = 0; pass < 200; ++pass) {
    bool moved = false;
    for (std::size_t a = 0; a < n; ++a)
      for (std::size_t b = a + 1; b < n; ++b) {
        double ox = (sizes[a].width + sizes[b].width) / 2 + margin - std::fabs(c[a].x - c[b].x);
        double oy = (sizes[a].height + sizes[b].height) / 2 + margin - std::fabs(c[a].y - c[b].y);
        if (ox <= 0 || oy <= 0) continue;
        moved = true;
        if (ox < oy) {
          double s = c[a].x < c[b].x || (c[a].x == c[b].x && a < b) ? -1 : 1;
          c[a].x += s * ox / 2;
          c[b].x -= s * ox / 2;
        } else {
          double s = c[a].y < c[b].y || (c[a].y == c[b].y && a < b) ? -1 : 1;
          c[a].y += s * oy / 2;
          c[b].y -= s * oy / 2;
        }
      }
    if (!moved) break;
  }
  for (std::size_t v = 0; v < n; ++v)
    p.at.push_back({round2(c[v].x - sizes[v].width / 2), round2(c[v].y - sizes[v].height / 2)});
  return p;
}

}  // namespace

Geometry tree_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints) {
  return run(graph, hints, tree, Routing::Orthogonal);
}

Geometry grid_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints) {
  return run(graph, hints, grid, Routing::Orthogonal);
}

Geometry radial_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints) {
  return run(graph, hints, radial, Routing::Straight);
}

Geometry force_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints) {
  return run(graph, hints, force, Routing::Straight);
}

Geometry graph_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints, const Geometry* previous) {
  const std::string& algorithm = graph.options.algorithm;
  if (algorithm == "tree") return tree_layout(graph, hints);
  if (algorithm == "grid") return grid_layout(graph, hints);
  if (algorithm == "radial") return radial_layout(graph, hints);
  if (algorithm == "force") return force_layout(graph, hints);
  return layered_layout(graph, hints, true, previous);
}

}  // namespace dd
