// The in-house layered layout engine (milestone C4), replacing ELK.
//
// A Sugiyama-style pipeline, run per container from the innermost out:
//   1. cycle breaking (greedy feedback arc set),
//   2. layering by network simplex (edges with labels span two layers, so the label gets a layer
//      of its own; `pin` hints along the flow become layering constraints),
//   3. dummy nodes for long edges and labels,
//   4. crossing minimisation by layer sweeps with barycenters and transposition, honouring
//      `order` hints and `pin` hints across the flow,
//   5. coordinates across the flow by the priority method (long edges stay straight),
//   6. layers placed along the flow with room for one routing track per overlapping edge,
//   7. orthogonal routing in the channels between layers.
// Everything runs in an internal orientation where the flow goes down; the result is rotated or
// mirrored into the view's direction at the end. Edges that cross container boundaries, or whose
// ports sit on a side the flow cannot reach, are routed afterwards around the nodes. Hints the
// result still misses go through the constraint pass (constraints.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>

#include "diadraw/layout/layout.hpp"

namespace dd {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/// Sides in the internal orientation (flow goes down: along = y, across = x).
enum class ISide { AlongStart, AlongEnd, AcrossStart, AcrossEnd };

struct Spacing {
  double node = 20;             ///< between nodes in a layer
  double layer = 20;            ///< between layers with only straight edges
  double edge_edge = 10;        ///< between parallel edge segments
  double edge_node = 10;        ///< between an edge and a node in a layer
  double edge_node_layer = 20;  ///< between a layer and the first routing track
};

struct Padding {
  double across_start, across_end, along_start, along_end;
};

constexpr double kRootPadding = 12;

struct Orientation {
  Direction dir = Direction::Down;
  [[nodiscard]] bool horizontal() const { return dir == Direction::Right || dir == Direction::Left; }
  [[nodiscard]] bool mirrored() const { return dir == Direction::Up || dir == Direction::Left; }

  [[nodiscard]] ISide side(PortSide s) const {
    switch (dir) {
      case Direction::Down:
        return s == PortSide::North ? ISide::AlongStart : s == PortSide::South ? ISide::AlongEnd
               : s == PortSide::West ? ISide::AcrossStart : ISide::AcrossEnd;
      case Direction::Up:
        return s == PortSide::South ? ISide::AlongStart : s == PortSide::North ? ISide::AlongEnd
               : s == PortSide::West ? ISide::AcrossStart : ISide::AcrossEnd;
      case Direction::Right:
        return s == PortSide::West ? ISide::AlongStart : s == PortSide::East ? ISide::AlongEnd
               : s == PortSide::North ? ISide::AcrossStart : ISide::AcrossEnd;
      default:
        return s == PortSide::East ? ISide::AlongStart : s == PortSide::West ? ISide::AlongEnd
               : s == PortSide::North ? ISide::AcrossStart : ISide::AcrossEnd;
    }
  }

  /// Container padding (top 32, others 16 in the final picture) in internal terms.
  [[nodiscard]] Padding padding() const {
    switch (dir) {
      case Direction::Down: return {16, 16, 32, 16};
      case Direction::Up: return {16, 16, 16, 32};
      case Direction::Right: return {32, 16, 16, 16};
      default: return {32, 16, 16, 16};
    }
  }

  /// Internal size (across, along) of a final width and height.
  [[nodiscard]] Size internal(double width, double height) const {
    return horizontal() ? Size{height, width} : Size{width, height};
  }

  /// Final box of an internal box, given the total extent along the flow.
  [[nodiscard]] Box to_final(const Box& b, double along_total) const {
    double along = mirrored() ? along_total - b.y - b.height : b.y;
    if (horizontal()) return {along, b.x, b.height, b.width};
    return {b.x, along, b.width, b.height};
  }
  [[nodiscard]] Point to_final(const Point& p, double along_total) const {
    double along = mirrored() ? along_total - p.y : p.y;
    if (horizontal()) return {along, p.x};
    return {p.x, along};
  }
};

// ── Network simplex ──────────────────────────────────────────────────────────────────────────

struct RankEdge {
  int tail, head;
  int minlen;
};

/// Ranks for an acyclic graph minimising total weighted edge length (Gansner et al. 1993), per
/// connected component, each normalised to start at 0.
std::vector<int> network_simplex(int n, const std::vector<RankEdge>& edges) {
  std::vector<int> rank(static_cast<std::size_t>(n), 0);
  // Components by union-find.
  std::vector<int> comp(static_cast<std::size_t>(n));
  std::iota(comp.begin(), comp.end(), 0);
  std::function<int(int)> find = [&](int x) { return comp[x] == x ? x : comp[x] = find(comp[x]); };
  for (const auto& e : edges) comp[find(e.tail)] = find(e.head);
  std::map<int, std::vector<int>> groups;
  for (int v = 0; v < n; ++v) groups[find(v)].push_back(v);

  for (auto& [_, members] : groups) {
    const int m = static_cast<int>(members.size());
    if (m == 1) continue;
    std::map<int, int> local;
    for (int i = 0; i < m; ++i) local[members[i]] = i;
    std::vector<RankEdge> es;
    for (const auto& e : edges)
      if (local.count(e.tail) && local.count(e.head)) es.push_back({local[e.tail], local[e.head], e.minlen});
    const int ne = static_cast<int>(es.size());
    std::vector<int> r(static_cast<std::size_t>(m), 0);
    std::vector<std::vector<int>> incident(static_cast<std::size_t>(m));
    for (int i = 0; i < ne; ++i) {
      incident[es[i].tail].push_back(i);
      incident[es[i].head].push_back(i);
    }
    // Longest path from the sources.
    std::vector<int> indeg(static_cast<std::size_t>(m), 0);
    for (const auto& e : es) ++indeg[e.head];
    std::vector<int> queue;
    for (int v = 0; v < m; ++v)
      if (!indeg[v]) queue.push_back(v);
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
      int v = queue[qi];
      for (int ei : incident[v]) {
        if (es[ei].tail != v) continue;
        int h = es[ei].head;
        r[h] = std::max(r[h], r[v] + es[ei].minlen);
        if (--indeg[h] == 0) queue.push_back(h);
      }
    }
    auto slack = [&](int ei) { return r[es[ei].head] - r[es[ei].tail] - es[ei].minlen; };

    // Feasible tight spanning tree.
    std::vector<bool> in_tree(static_cast<std::size_t>(m), false), tree_edge(static_cast<std::size_t>(ne), false);
    in_tree[0] = true;
    int size = 1;
    while (true) {
      std::vector<int> stack;
      for (int v = 0; v < m; ++v)
        if (in_tree[v]) stack.push_back(v);
      while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        for (int ei : incident[v]) {
          int other = es[ei].tail == v ? es[ei].head : es[ei].tail;
          if (in_tree[other] || slack(ei) != 0) continue;
          in_tree[other] = true;
          tree_edge[ei] = true;
          ++size;
          stack.push_back(other);
        }
      }
      if (size == m) break;
      int best = -1;
      for (int ei = 0; ei < ne; ++ei) {
        if (in_tree[es[ei].tail] == in_tree[es[ei].head]) continue;
        if (best < 0 || slack(ei) < slack(best)) best = ei;
      }
      if (best < 0) break;  // cannot happen for a connected component
      int delta = in_tree[es[best].head] ? -slack(best) : slack(best);
      for (int v = 0; v < m; ++v)
        if (in_tree[v]) r[v] += delta;
    }

    // Pivot on negative cut values.
    auto tail_side = [&](int leave) {
      std::vector<bool> side(static_cast<std::size_t>(m), false);
      std::vector<int> stack{es[leave].tail};
      side[es[leave].tail] = true;
      while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        for (int ei : incident[v]) {
          if (!tree_edge[ei] || ei == leave) continue;
          int other = es[ei].tail == v ? es[ei].head : es[ei].tail;
          if (!side[other]) {
            side[other] = true;
            stack.push_back(other);
          }
        }
      }
      return side;
    };
    auto rerank = [&] {
      std::vector<bool> seen(static_cast<std::size_t>(m), false);
      std::vector<int> stack{0};
      seen[0] = true;
      while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        for (int ei : incident[v]) {
          if (!tree_edge[ei]) continue;
          const auto& e = es[ei];
          int other = e.tail == v ? e.head : e.tail;
          if (seen[other]) continue;
          r[other] = e.tail == v ? r[v] + e.minlen : r[v] - e.minlen;
          seen[other] = true;
          stack.push_back(other);
        }
      }
    };
    for (int iteration = 0; iteration < 20 * m + 100; ++iteration) {
      int leave = -1;
      std::vector<bool> side;
      for (int ei = 0; ei < ne && leave < 0; ++ei) {
        if (!tree_edge[ei]) continue;
        auto t = tail_side(ei);
        int cut = 0;
        for (const auto& e : es) {
          if (t[e.tail] && !t[e.head]) ++cut;
          else if (!t[e.tail] && t[e.head]) --cut;
        }
        if (cut < 0) {
          leave = ei;
          side = std::move(t);
        }
      }
      if (leave < 0) break;
      int enter = -1;
      for (int ei = 0; ei < ne; ++ei) {
        if (tree_edge[ei] || side[es[ei].tail] || !side[es[ei].head]) continue;
        if (enter < 0 || slack(ei) < slack(enter)) enter = ei;
      }
      if (enter < 0) break;
      tree_edge[leave] = false;
      tree_edge[enter] = true;
      rerank();
    }
    int lo = *std::min_element(r.begin(), r.end());
    for (int i = 0; i < m; ++i) rank[members[i]] = r[i] - lo;
  }
  return rank;
}

// ── One level: the children of one container ──────────────────────────────────────────────────

struct FNode {
  double w = 0, h = 0;  ///< internal size
  int model = 0;        ///< position in the input, for stable tie-breaking
  /// Position across the flow in the previous layout, when there was one and it had this node.
  std::optional<double> prior;
};

struct FEdge {
  int s = 0, t = 0;
  int minlen = 1;
  std::optional<Size> label;  ///< internal size
  int id = -1;                ///< graph edge index; -1 for a hint edge (layering only)
  bool local = true;          ///< routed here, rather than afterwards around the nodes
  std::optional<ISide> s_side, t_side;
};

struct FResult {
  std::vector<Box> boxes;  ///< per node, relative to the level's content origin
  struct Route {
    int id;
    std::vector<Point> points;
    std::optional<Box> label;
  };
  std::vector<Route> routes;
  std::vector<int> unrouted;  ///< graph edges this level wanted routed elsewhere
  Size size;
};

/// Stable topological order: each item keeps its place unless a (before, after) pair moves it.
std::vector<int> constrained_order(const std::vector<int>& items, const std::vector<std::pair<int, int>>& pairs) {
  if (pairs.empty()) return items;
  std::map<int, std::set<int>> incoming;
  std::set<int> present(items.begin(), items.end());
  for (const auto& [b, a] : pairs)
    if (present.count(b) && present.count(a) && b != a) incoming[a].insert(b);
  if (incoming.empty()) return items;
  std::vector<int> result;
  std::set<int> done;
  while (result.size() < items.size()) {
    int pick = -1;
    for (int v : items) {
      if (done.count(v)) continue;
      bool ready = true;
      for (int b : incoming[v])
        if (!done.count(b)) ready = false;
      if (ready) {
        pick = v;
        break;
      }
    }
    if (pick < 0)
      for (int v : items)
        if (!done.count(v)) {
          pick = v;
          break;
        }
    done.insert(pick);
    result.push_back(pick);
  }
  return result;
}

class Level {
 public:
  Level(std::vector<FNode> nodes, std::vector<FEdge> edges, std::vector<std::pair<int, int>> before, Spacing sp)
      : nodes_(std::move(nodes)), edges_(std::move(edges)), before_(std::move(before)), sp_(sp) {}

  FResult run() {
    const int n = static_cast<int>(nodes_.size());
    FResult out;
    out.boxes.resize(static_cast<std::size_t>(n));
    if (n == 0) return out;
    break_cycles();
    rank_nodes();
    for (auto& e : edges_) {
      if (e.id < 0 || !e.local) continue;
      ISide natural_s = reversed(e) ? ISide::AlongStart : ISide::AlongEnd;
      ISide natural_t = reversed(e) ? ISide::AlongEnd : ISide::AlongStart;
      if ((e.s_side && *e.s_side != natural_s) || (e.t_side && *e.t_side != natural_t) || e.s == e.t) {
        if (e.s != e.t) {
          e.local = false;
          out.unrouted.push_back(e.id);
        }
      }
    }
    for (const auto& e : edges_)
      if (e.id >= 0 && !e.local && std::find(out.unrouted.begin(), out.unrouted.end(), e.id) == out.unrouted.end())
        out.unrouted.push_back(e.id);
    build_proper_graph();
    order_layers();
    drop_ghosts();
    place_across();
    place_along_and_route(out);
    return out;
  }

 private:
  struct PNode {
    double w = 0, h = 0;
    int layer = 0;
    int real = -1;  ///< index into nodes_, or -1 for a dummy
    bool ghost = false;
    bool label = false;
    double key = 0;  ///< initial ordering key
  };
  struct Seg {
    int upper, lower;
    int edge;  ///< index into edges_
  };

  bool reversed(const FEdge& e) const { return pos_[e.s] > pos_[e.t]; }

  // 1. Cycle breaking by depth-first search from the sources, in input order: the edges that
  // close a cycle are the ones a reader sees as going back (a loop back to the queue), so they
  // are the ones reversed. Nodes get reverse post-order positions; an edge is reversed exactly
  // when its source comes after its target.
  void break_cycles() {
    const int n = static_cast<int>(nodes_.size());
    std::vector<std::vector<int>> out(static_cast<std::size_t>(n));
    std::vector<int> indegree(static_cast<std::size_t>(n), 0);
    for (const auto& e : edges_) {
      if (e.s == e.t) continue;
      out[e.s].push_back(e.t);
      ++indegree[e.t];
    }
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return nodes_[a].model < nodes_[b].model; });
    std::vector<int> state(static_cast<std::size_t>(n), 0);  // 0 new, 1 on the stack, 2 done
    std::vector<int> postorder;
    std::function<void(int)> dfs = [&](int v) {
      state[v] = 1;
      for (int w : out[v])
        if (state[w] == 0) dfs(w);
      state[v] = 2;
      postorder.push_back(v);
    };
    for (int v : order)
      if (indegree[v] == 0 && state[v] == 0) dfs(v);
    for (int v : order)
      if (state[v] == 0) dfs(v);
    pos_.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) pos_[postorder[static_cast<std::size_t>(n - 1 - i)]] = i;
  }

  // 2. Layering.
  void rank_nodes() {
    std::vector<RankEdge> re;
    for (const auto& e : edges_) {
      if (e.s == e.t) continue;
      int tail = reversed(e) ? e.t : e.s;
      int head = reversed(e) ? e.s : e.t;
      re.push_back({tail, head, e.minlen});
    }
    rank_ = network_simplex(static_cast<int>(nodes_.size()), re);
  }

  // 3. Dummy nodes.
  void build_proper_graph() {
    for (std::size_t v = 0; v < nodes_.size(); ++v) {
      PNode p;
      p.w = nodes_[v].w;
      p.h = nodes_[v].h;
      p.layer = rank_[v];
      p.real = static_cast<int>(v);
      p.key = nodes_[v].model;
      pnodes_.push_back(p);
    }
    chains_.assign(edges_.size(), {});
    for (std::size_t ei = 0; ei < edges_.size(); ++ei) {
      const auto& e = edges_[ei];
      if (e.id < 0 || e.s == e.t) continue;
      int upper = reversed(e) ? e.t : e.s;
      int lower = reversed(e) ? e.s : e.t;
      int r0 = rank_[upper], r1 = rank_[lower];
      int label_layer = e.local && e.label ? r0 + (r1 - r0) / 2 : -1;
      std::vector<int> chain{upper};
      for (int r = r0 + 1; r < r1; ++r) {
        PNode d;
        d.layer = r;
        d.ghost = !e.local;
        d.key = nodes_[upper].model + 0.5;
        if (r == label_layer) {
          d.label = true;
          d.w = e.label->width;
          d.h = e.label->height;
        }
        chain.push_back(static_cast<int>(pnodes_.size()));
        pnodes_.push_back(d);
      }
      chain.push_back(lower);
      for (std::size_t k = 1; k < chain.size(); ++k) segs_.push_back({chain[k - 1], chain[k], static_cast<int>(ei)});
      chains_[ei] = std::move(chain);
    }
    int layers = 0;
    for (const auto& p : pnodes_) layers = std::max(layers, p.layer + 1);
    layers_.assign(static_cast<std::size_t>(layers), {});
    for (std::size_t v = 0; v < pnodes_.size(); ++v) layers_[pnodes_[v].layer].push_back(static_cast<int>(v));
    for (auto& layer : layers_)
      std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return pnodes_[a].key < pnodes_[b].key; });
    up_.assign(pnodes_.size(), {});
    down_.assign(pnodes_.size(), {});
    for (const auto& s : segs_) {
      down_[s.upper].push_back(s.lower);
      up_[s.lower].push_back(s.upper);
    }
    // Order constraints between real nodes.
    for (const auto& [b, a] : before_) order_pairs_.push_back({b, a});
  }

  // 4. Crossing minimisation.
  std::vector<int> index_in_layer() const {
    std::vector<int> idx(pnodes_.size(), 0);
    for (const auto& layer : layers_)
      for (std::size_t i = 0; i < layer.size(); ++i) idx[layer[i]] = static_cast<int>(i);
    return idx;
  }

  long crossings_between(std::size_t layer, const std::vector<int>& idx) const {
    std::vector<std::pair<int, int>> pairs;
    for (int v : layers_[layer])
      for (int w : down_[v]) pairs.push_back({idx[v], idx[w]});
    std::sort(pairs.begin(), pairs.end());
    // Inversions in the lower positions, by merge sort.
    std::vector<int> a;
    for (const auto& p : pairs) a.push_back(p.second);
    std::vector<int> tmp(a.size());
    std::function<long(int, int)> count = [&](int lo, int hi) -> long {
      if (hi - lo < 2) return 0;
      int mid = (lo + hi) / 2;
      long c = count(lo, mid) + count(mid, hi);
      int i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        if (a[j] < a[i]) {
          c += mid - i;
          tmp[k++] = a[j++];
        } else {
          tmp[k++] = a[i++];
        }
      }
      while (i < mid) tmp[k++] = a[i++];
      while (j < hi) tmp[k++] = a[j++];
      for (int x = lo; x < hi; ++x) a[x] = tmp[x];
      return c;
    };
    return count(0, static_cast<int>(a.size()));
  }

  long total_crossings() const {
    auto idx = index_in_layer();
    long c = 0;
    for (std::size_t i = 0; i + 1 < layers_.size(); ++i) c += crossings_between(i, idx);
    return c;
  }

  std::vector<std::pair<int, int>> pairs_in(const std::vector<int>& layer) const {
    std::set<int> members(layer.begin(), layer.end());
    std::vector<std::pair<int, int>> out;
    for (const auto& [b, a] : order_pairs_)
      if (members.count(b) && members.count(a)) out.push_back({b, a});
    return out;
  }

  bool must_precede(int a, int b, const std::vector<std::pair<int, int>>& pairs) const {
    for (const auto& [x, y] : pairs)
      if (x == a && y == b) return true;
    return false;
  }

  /// Crossings between the edges of u and v (u left of v) towards one neighbouring layer.
  static long pair_crossings(const std::vector<int>& nu, const std::vector<int>& nv, const std::vector<int>& idx) {
    long c = 0;
    for (int a : nu)
      for (int b : nv)
        if (idx[a] > idx[b]) ++c;
    return c;
  }

  void sort_layer(std::size_t i, bool use_up, std::vector<int>& idx) {
    auto& layer = layers_[i];
    std::vector<std::pair<double, int>> keyed;
    for (int v : layer) {
      const auto& nb = use_up ? up_[v] : down_[v];
      double bary = idx[v];
      if (!nb.empty()) {
        double sum = 0;
        for (int w : nb) sum += idx[w];
        bary = sum / static_cast<double>(nb.size());
        // Scale to this layer's index range so nodes without neighbours keep their slots.
        const auto& other = layers_[use_up ? i - 1 : i + 1];
        if (other.size() > 1 && layer.size() > 1)
          bary = bary * static_cast<double>(layer.size() - 1) / static_cast<double>(other.size() - 1);
      }
      keyed.push_back({bary, v});
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> sorted;
    for (const auto& [_, v] : keyed) sorted.push_back(v);
    layer = constrained_order(sorted, pairs_in(sorted));
    for (std::size_t k = 0; k < layer.size(); ++k) idx[layer[k]] = static_cast<int>(k);
  }

  void transpose(std::size_t i, std::vector<int>& idx) {
    auto& layer = layers_[i];
    auto pairs = pairs_in(layer);
    bool improved = true;
    for (int round = 0; round < 4 && improved; ++round) {
      improved = false;
      for (std::size_t k = 0; k + 1 < layer.size(); ++k) {
        int u = layer[k], v = layer[k + 1];
        if (must_precede(u, v, pairs)) continue;
        long before = pair_crossings(up_[u], up_[v], idx) + pair_crossings(down_[u], down_[v], idx);
        long after = pair_crossings(up_[v], up_[u], idx) + pair_crossings(down_[v], down_[u], idx);
        if (after < before) {
          std::swap(layer[k], layer[k + 1]);
          idx[u] = static_cast<int>(k + 1);
          idx[v] = static_cast<int>(k);
          improved = true;
        }
      }
    }
  }

  /// Layer sweeps from the current order until they stop improving; returns the best crossings.
  long sweep(std::vector<std::vector<int>>& best_layers) {
    for (auto& layer : layers_) layer = constrained_order(layer, pairs_in(layer));
    auto idx = index_in_layer();
    best_layers = layers_;
    long best_c = total_crossings();
    int stale = 0;
    for (int it = 0; it < 32 && best_c > 0 && stale < 6; ++it) {
      if (it % 2 == 0) {
        for (std::size_t i = 1; i < layers_.size(); ++i) sort_layer(i, true, idx);
      } else {
        for (std::size_t i = layers_.size() - 1; i-- > 0;) sort_layer(i, false, idx);
      }
      for (std::size_t i = 0; i < layers_.size(); ++i) transpose(i, idx);
      long c = total_crossings();
      if (c < best_c) {
        best_c = c;
        best_layers = layers_;
        stale = 0;
      } else {
        ++stale;
      }
    }
    return best_c;
  }

  /// The previous position of the real node at the top of a dummy chain.
  double key_of_upper(int v) const {
    while (pnodes_[v].real < 0 && !up_[v].empty()) v = up_[v].front();
    int real = pnodes_[v].real;
    return real < 0 ? kInf : nodes_[real].prior.value_or(kInf);
  }

  /// Crossing minimisation from several starting orders: input order, depth-first order from the
  /// sources, and seeded shuffles. The best result wins; ties keep the earlier start.
  void order_layers() {
    const auto initial = layers_;
    std::vector<std::vector<std::vector<int>>> starts{initial};
    // Depth-first order along the flow.
    {
      std::vector<int> visit(pnodes_.size(), -1);
      int clock = 0;
      std::function<void(int)> dfs = [&](int v) {
        visit[v] = clock++;
        for (int w : down_[v])
          if (visit[w] < 0) dfs(w);
      };
      for (const auto& layer : initial)
        for (int v : layer)
          if (visit[v] < 0) dfs(v);
      auto dfs_layers = initial;
      for (auto& layer : dfs_layers)
        std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return visit[a] < visit[b]; });
      starts.push_back(std::move(dfs_layers));
    }
    std::uint32_t seed = 0x9E3779B9u;
    auto next = [&] {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      return seed;
    };
    for (int k = 0; k < 6; ++k) {
      auto shuffled = initial;
      for (auto& layer : shuffled)
        for (std::size_t i = layer.size(); i > 1; --i) std::swap(layer[i - 1], layer[next() % i]);
      starts.push_back(std::move(shuffled));
    }
    std::vector<std::vector<int>> best = initial;
    long best_c = std::numeric_limits<long>::max();
    // Stability: the previous layout's order goes first and wins ties, so a small edit does not
    // reshuffle the diagram unless reshuffling removes crossings.
    bool has_prior = std::any_of(nodes_.begin(), nodes_.end(), [](const FNode& f) { return f.prior.has_value(); });
    if (has_prior) {
      auto key = [&](int v) {
        int real = pnodes_[v].real;
        if (real < 0) {
          for (int w : up_[v]) return key_of_upper(w);
          return kInf;
        }
        return nodes_[real].prior.value_or(kInf);
      };
      auto prior_layers = initial;
      for (auto& layer : prior_layers)
        std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return key(a) < key(b); });
      starts.insert(starts.begin(), std::move(prior_layers));
    }
    for (auto& start : starts) {
      layers_ = start;
      std::vector<std::vector<int>> result;
      long c = sweep(result);
      if (c < best_c) {
        best_c = c;
        best = std::move(result);
      }
      if (best_c == 0) break;
    }
    layers_ = std::move(best);
  }

  void drop_ghosts() {
    for (auto& layer : layers_)
      layer.erase(std::remove_if(layer.begin(), layer.end(), [&](int v) { return pnodes_[v].ghost; }), layer.end());
    std::vector<Seg> kept;
    for (const auto& s : segs_)
      if (edges_[s.edge].local) kept.push_back(s);
    segs_ = std::move(kept);
    up_.assign(pnodes_.size(), {});
    down_.assign(pnodes_.size(), {});
    for (const auto& s : segs_) {
      down_[s.upper].push_back(s.lower);
      up_[s.lower].push_back(s.upper);
    }
  }

  // 5. Coordinates across the flow (priority method).
  double gap(int a, int b) const {
    bool ra = pnodes_[a].real >= 0 || pnodes_[a].label;
    bool rb = pnodes_[b].real >= 0 || pnodes_[b].label;
    if (ra && rb) return sp_.node;
    if (ra || rb) return sp_.edge_node;
    return sp_.edge_edge;
  }
  double sep(int a, int b) const { return pnodes_[a].w / 2 + pnodes_[b].w / 2 + gap(a, b); }

  /// One of the four Brandes–Köpf passes: vertical alignment to median neighbours (above when
  /// `downward`, else below), then horizontal compaction from the left (or the right).
  std::vector<double> bk_pass(bool downward, bool from_left, const std::set<std::pair<int, int>>& marked) const {
    auto L = layers_;
    if (!from_left)
      for (auto& layer : L) std::reverse(layer.begin(), layer.end());
    if (!downward) std::reverse(L.begin(), L.end());
    const std::size_t n = pnodes_.size();
    std::vector<int> pos(n, 0), layer_of(n, -1);
    for (std::size_t li = 0; li < L.size(); ++li)
      for (std::size_t k = 0; k < L[li].size(); ++k) {
        pos[L[li][k]] = static_cast<int>(k);
        layer_of[L[li][k]] = static_cast<int>(li);
      }
    const auto& towards = downward ? up_ : down_;
    std::vector<int> root(n), align(n);
    std::iota(root.begin(), root.end(), 0);
    std::iota(align.begin(), align.end(), 0);
    for (const auto& layer : L) {
      int r = -1;
      for (int v : layer) {
        std::vector<int> nb = towards[v];
        if (nb.empty()) continue;
        std::sort(nb.begin(), nb.end(), [&](int a, int b) { return pos[a] < pos[b]; });
        const int d = static_cast<int>(nb.size());
        for (int m : {(d - 1) / 2, d / 2}) {
          if (align[v] != v) break;
          int u = nb[m];
          if (marked.count({std::min(u, v), std::max(u, v)}) || r >= pos[u]) continue;
          align[u] = v;
          root[v] = root[u];
          align[v] = root[v];
          r = pos[u];
        }
      }
    }
    std::vector<int> sink(n);
    std::iota(sink.begin(), sink.end(), 0);
    std::vector<double> shift(n, kInf), x(n, 0);
    std::vector<bool> placed(n, false);
    std::function<void(int)> place_block = [&](int v) {
      if (placed[v]) return;
      placed[v] = true;
      x[v] = 0;
      int w = v;
      do {
        if (pos[w] > 0) {
          int u = L[layer_of[w]][pos[w] - 1];
          int ru = root[u];
          place_block(ru);
          if (sink[v] == v) sink[v] = sink[ru];
          if (sink[v] != sink[ru])
            shift[sink[ru]] = std::min(shift[sink[ru]], x[v] - x[ru] - sep(u, w));
          else
            x[v] = std::max(x[v], x[ru] + sep(u, w));
        }
        w = align[w];
      } while (w != v);
    };
    for (const auto& layer : L)
      for (int v : layer)
        if (root[v] == v) place_block(v);
    std::vector<double> out(n, 0);
    for (const auto& layer : L)
      for (int v : layer) {
        out[v] = x[root[v]];
        double s = shift[sink[root[v]]];
        if (s < kInf) out[v] += s;
      }
    if (!from_left)
      for (auto& value : out) value = -value;
    return out;
  }

  // 5. Coordinates across the flow (Brandes and Köpf 2001).
  void place_across() {
    // Type 1 conflicts: a segment between two dummies wins over a segment it crosses.
    std::set<std::pair<int, int>> marked;
    auto idx = index_in_layer();
    auto inner = [&](int u, int v) {
      return pnodes_[u].real < 0 && pnodes_[v].real < 0 && !pnodes_[u].label && !pnodes_[v].label;
    };
    for (std::size_t i = 0; i + 1 < layers_.size(); ++i) {
      std::vector<std::pair<int, int>> segs, inner_segs;
      for (int u : layers_[i])
        for (int v : down_[u]) (inner(u, v) ? inner_segs : segs).push_back({u, v});
      for (const auto& [a, b] : segs)
        for (const auto& [c, d] : inner_segs)
          if ((idx[a] < idx[c] && idx[b] > idx[d]) || (idx[a] > idx[c] && idx[b] < idx[d]))
            marked.insert({std::min(a, b), std::max(a, b)});
    }
    std::vector<std::vector<double>> runs;
    for (bool downward : {true, false})
      for (bool from_left : {true, false}) runs.push_back(bk_pass(downward, from_left, marked));
    // Align the four results to the narrowest one, then take the average of the two medians.
    auto extent = [&](const std::vector<double>& xs) {
      double lo = kInf, hi = -kInf;
      for (const auto& layer : layers_)
        for (int v : layer) {
          lo = std::min(lo, xs[v] - pnodes_[v].w / 2);
          hi = std::max(hi, xs[v] + pnodes_[v].w / 2);
        }
      return std::pair{lo, hi};
    };
    std::size_t narrowest = 0;
    double best_width = kInf;
    for (std::size_t k = 0; k < runs.size(); ++k) {
      auto [lo, hi] = extent(runs[k]);
      if (hi - lo < best_width) {
        best_width = hi - lo;
        narrowest = k;
      }
    }
    auto [ref_lo, ref_hi] = extent(runs[narrowest]);
    for (std::size_t k = 0; k < runs.size(); ++k) {
      auto [lo, hi] = extent(runs[k]);
      double delta = k % 2 == 0 ? ref_lo - lo : ref_hi - hi;  // even runs compact from the left
      for (auto& value : runs[k]) value += delta;
    }
    x_.assign(pnodes_.size(), 0);
    for (const auto& layer : layers_)
      for (int v : layer) {
        std::array<double, 4> xs{runs[0][v], runs[1][v], runs[2][v], runs[3][v]};
        std::sort(xs.begin(), xs.end());
        x_[v] = (xs[1] + xs[2]) / 2;
      }
    // Averaging can bring neighbours closer than they may be; push them apart again.
    for (const auto& layer : layers_)
      for (std::size_t k = 1; k < layer.size(); ++k)
        x_[layer[k]] = std::max(x_[layer[k]], x_[layer[k - 1]] + sep(layer[k - 1], layer[k]));
    straighten_chains();
    double lo = kInf;
    for (const auto& layer : layers_)
      for (int v : layer) lo = std::min(lo, x_[v] - pnodes_[v].w / 2);
    if (std::isfinite(lo))
      for (auto& x : x_) x -= lo;
  }

  /// Puts the dummies of each long edge in one column wherever their neighbours leave room, so
  /// long edges run straight instead of stepping sideways at every layer.
  void straighten_chains() {
    std::vector<int> slot(pnodes_.size(), -1);
    for (const auto& layer : layers_)
      for (std::size_t k = 0; k < layer.size(); ++k) slot[layer[k]] = static_cast<int>(k);
    for (std::size_t ei = 0; ei < chains_.size(); ++ei) {
      const auto& chain = chains_[ei];
      if (chain.size() < 3 || !edges_[ei].local) continue;
      std::vector<int> dummies(chain.begin() + 1, chain.end() - 1);
      std::vector<double> candidates;
      for (int d : dummies) candidates.push_back(x_[d]);
      candidates.push_back(x_[chain.front()]);
      candidates.push_back(x_[chain.back()]);
      auto cost = [&](double c) {
        double sum = 0;
        for (int d : dummies) sum += std::fabs(x_[d] - c);
        return sum;
      };
      std::stable_sort(candidates.begin(), candidates.end(), [&](double a, double b) { return cost(a) < cost(b); });
      for (double c : candidates) {
        bool fits = true;
        for (int d : dummies) {
          const auto& layer = layers_[pnodes_[d].layer];
          int k = slot[d];
          if (k > 0 && x_[layer[k - 1]] + sep(layer[k - 1], d) > c + 1e-6) fits = false;
          if (k + 1 < static_cast<int>(layer.size()) && c > x_[layer[k + 1]] - sep(d, layer[k + 1]) + 1e-6) fits = false;
          if (!fits) break;
        }
        if (fits) {
          for (int d : dummies) x_[d] = c;
          break;
        }
      }
    }
  }

  // 6 and 7. Layers along the flow, then routes in the channels between them.
  void place_along_and_route(FResult& out) {
    const std::size_t L = layers_.size();
    std::vector<double> thickness(L, 0);
    for (std::size_t i = 0; i < L; ++i)
      for (int v : layers_[i]) thickness[i] = std::max(thickness[i], pnodes_[v].h);

    // Attach points: edges leaving or entering a real node spread across its side, ordered by
    // where their other end is, so they do not cross at the node.
    std::vector<double> ax(segs_.size()), bx(segs_.size());
    std::map<int, std::vector<std::size_t>> outgoing, incoming;
    for (std::size_t k = 0; k < segs_.size(); ++k) {
      outgoing[segs_[k].upper].push_back(k);
      incoming[segs_[k].lower].push_back(k);
    }
    // Each attach point aims at the other end of its segment (so aligned edges run straight),
    // kept in order and at least `d` apart along the side.
    auto spread = [&](int v, std::vector<std::size_t>& list, std::vector<double>& at, bool from_upper) {
      const auto& p = pnodes_[v];
      if (p.real < 0) {
        for (std::size_t k : list) at[k] = x_[v];
        return;
      }
      auto other = [&](std::size_t k) { return from_upper ? segs_[k].lower : segs_[k].upper; };
      std::stable_sort(list.begin(), list.end(), [&](std::size_t a, std::size_t b) { return x_[other(a)] < x_[other(b)]; });
      const double n = static_cast<double>(list.size());
      const double left = x_[v] - p.w / 2, right = x_[v] + p.w / 2;
      const double d = std::min(sp_.edge_edge, p.w / (n + 1));
      const double inset = std::min(d, p.w / 2);
      if (list.size() == 1) {
        at[list[0]] = std::clamp(x_[other(list[0])], left + inset, right - inset);
        if (std::fabs(at[list[0]] - x_[other(list[0])]) > 0.5) at[list[0]] = x_[v];
        return;
      }
      std::vector<double> xs;
      for (std::size_t k : list) xs.push_back(std::clamp(x_[other(k)], left + inset, right - inset));
      for (std::size_t i = 1; i < xs.size(); ++i) xs[i] = std::max(xs[i], xs[i - 1] + d);
      if (xs.back() > right - inset) xs.back() = right - inset;
      for (std::size_t i = xs.size() - 1; i-- > 0;) xs[i] = std::min(xs[i], xs[i + 1] - d);
      for (std::size_t i = 0; i < list.size(); ++i) at[list[i]] = xs[i];
    };
    for (auto& [v, list] : outgoing) spread(v, list, ax, true);
    for (auto& [v, list] : incoming) spread(v, list, bx, false);
    // A bent segment whose other end lies within the node's side runs straight if its attach
    // point can slide there without passing or crowding the node's other attach points.
    auto try_slide = [&](std::size_t k, int v, std::vector<std::size_t>& list, std::vector<double>& at, double target) {
      const auto& p = pnodes_[v];
      if (p.real < 0) return false;
      const double d = std::min(sp_.edge_edge, p.w / (static_cast<double>(list.size()) + 1));
      const double inset = std::min(d, p.w / 2);
      if (target < x_[v] - p.w / 2 + inset || target > x_[v] + p.w / 2 - inset) return false;
      for (std::size_t other : list) {
        if (other == k) continue;
        bool was_left = at[other] < at[k];
        if (was_left ? at[other] > target - d : at[other] < target + d) return false;
      }
      at[k] = target;
      return true;
    };
    for (std::size_t k = 0; k < segs_.size(); ++k) {
      if (std::fabs(ax[k] - bx[k]) <= 0.5) continue;
      if (!try_slide(k, segs_[k].upper, outgoing[segs_[k].upper], ax, bx[k]))
        try_slide(k, segs_[k].lower, incoming[segs_[k].lower], bx, ax[k]);
    }

    // Tracks per channel. Where two bent segments overlap, the one whose horizontal run crosses
    // fewer of the other's vertical pieces goes above; tracks then follow that order, and
    // segments that do not overlap share a track.
    std::vector<int> track(segs_.size(), -1);
    std::vector<int> tracks_in(L, 0);
    for (std::size_t i = 0; i + 1 < L; ++i) {
      std::vector<std::size_t> bent;
      for (std::size_t k = 0; k < segs_.size(); ++k)
        if (pnodes_[segs_[k].upper].layer == static_cast<int>(i) && std::fabs(ax[k] - bx[k]) > 0.5) bent.push_back(k);
      const std::size_t m = bent.size();
      if (m == 0) continue;
      auto lo = [&](std::size_t k) { return std::min(ax[k], bx[k]); };
      auto hi = [&](std::size_t k) { return std::max(ax[k], bx[k]); };
      auto inside = [](double x, double a, double b) { return x > a + 0.5 && x < b - 0.5; };
      auto overlap = [&](std::size_t a, std::size_t b) {
        return lo(a) < hi(b) + sp_.edge_edge && lo(b) < hi(a) + sp_.edge_edge;
      };
      // above[p][q]: q should go below p.
      std::vector<std::vector<int>> prefer(m);
      std::vector<int> indegree(m, 0);
      for (std::size_t p = 0; p < m; ++p) {
        for (std::size_t q = p + 1; q < m; ++q) {
          std::size_t a = bent[p], b = bent[q];
          if (!overlap(a, b)) continue;
          int a_above = (inside(ax[b], lo(a), hi(a)) ? 1 : 0) + (inside(bx[a], lo(b), hi(b)) ? 1 : 0);
          int b_above = (inside(ax[a], lo(b), hi(b)) ? 1 : 0) + (inside(bx[b], lo(a), hi(a)) ? 1 : 0);
          // Ties: the segment that turns earlier (further left when going right) goes above.
          bool p_first = a_above < b_above || (a_above == b_above && lo(a) <= lo(b));
          if (p_first) prefer[p].push_back(static_cast<int>(q));
          else prefer[q].push_back(static_cast<int>(p));
        }
      }
      for (std::size_t p = 0; p < m; ++p)
        for (int q : prefer[p]) ++indegree[q];
      // Topological order; on a cycle take the waiting segment with the fewest predecessors left.
      std::vector<std::size_t> order;
      std::vector<bool> done(m, false);
      while (order.size() < m) {
        std::size_t pick = m;
        for (std::size_t p = 0; p < m; ++p)
          if (!done[p] && (pick == m || indegree[p] < indegree[pick])) pick = p;
        done[pick] = true;
        order.push_back(pick);
        for (int q : prefer[pick]) --indegree[q];
      }
      int used = 0;
      for (std::size_t oi = 0; oi < m; ++oi) {
        std::size_t k = bent[order[oi]];
        int t = 0;
        for (std::size_t oj = 0; oj < oi; ++oj) {
          std::size_t other = bent[order[oj]];
          if (overlap(k, other)) t = std::max(t, track[other] + 1);
        }
        track[k] = t;
        used = std::max(used, t + 1);
      }
      tracks_in[i] = used;
    }

    std::vector<double> top(L, 0);
    for (std::size_t i = 1; i < L; ++i) {
      int k = tracks_in[i - 1];
      double channel = k > 0 ? std::max(sp_.layer, 2 * sp_.edge_node_layer + (k - 1) * sp_.edge_edge) : sp_.layer;
      top[i] = top[i - 1] + thickness[i - 1] + channel;
    }
    auto node_top = [&](int v) { return top[pnodes_[v].layer] + (thickness[pnodes_[v].layer] - pnodes_[v].h) / 2; };

    double width = 0, height = L ? top[L - 1] + thickness[L - 1] : 0;
    for (std::size_t v = 0; v < pnodes_.size(); ++v) {
      const auto& p = pnodes_[v];
      if (p.ghost) continue;
      width = std::max(width, x_[v] + p.w / 2);
      if (p.real >= 0) out.boxes[p.real] = Box{x_[v] - p.w / 2, node_top(static_cast<int>(v)), p.w, p.h};
    }

    // Segment points, then whole routes along each chain.
    std::vector<std::vector<Point>> seg_points(segs_.size());
    for (std::size_t k = 0; k < segs_.size(); ++k) {
      int u = segs_[k].upper, w = segs_[k].lower;
      double ya = node_top(u) + pnodes_[u].h;
      double yb = node_top(w);
      if (track[k] < 0) {
        seg_points[k] = {{ax[k], ya}, {bx[k], yb}};
      } else {
        std::size_t i = static_cast<std::size_t>(pnodes_[u].layer);
        double ty = top[i] + thickness[i] + sp_.edge_node_layer + track[k] * sp_.edge_edge;
        seg_points[k] = {{ax[k], ya}, {ax[k], ty}, {bx[k], ty}, {bx[k], yb}};
      }
    }
    std::map<std::pair<int, int>, std::size_t> seg_of;
    for (std::size_t k = 0; k < segs_.size(); ++k) seg_of[{segs_[k].upper, segs_[k].lower}] = k;
    for (std::size_t ei = 0; ei < edges_.size(); ++ei) {
      const auto& e = edges_[ei];
      if (e.id < 0 || !e.local) continue;
      FResult::Route route{e.id, {}, std::nullopt};
      if (e.s == e.t) {
        const Box& b = out.boxes[e.s];
        double q = b.height / 4, cy = b.y + b.height / 2, x = b.x + b.width;
        route.points = {{x, cy - q}, {x + 20, cy - q}, {x + 20, cy + q}, {x, cy + q}};
        width = std::max(width, x + 20);
        if (e.label) route.label = Box{x + 24, cy - e.label->height / 2, e.label->width, e.label->height};
        if (route.label) width = std::max(width, route.label->x + route.label->width);
        out.routes.push_back(std::move(route));
        continue;
      }
      const auto& chain = chains_[ei];
      std::vector<Point> pts;
      for (std::size_t k = 1; k < chain.size(); ++k) {
        const auto& sp = seg_points[seg_of.at({chain[k - 1], chain[k]})];
        pts.insert(pts.end(), sp.begin(), sp.end());
        int mid = chain[k];
        if (k + 1 < chain.size() && pnodes_[mid].label)
          route.label = Box{x_[mid] - pnodes_[mid].w / 2, node_top(mid), pnodes_[mid].w, pnodes_[mid].h};
      }
      pts = simplify(pts);
      if (reversed(e)) std::reverse(pts.begin(), pts.end());
      route.points = std::move(pts);
      out.routes.push_back(std::move(route));
    }
    out.size = {width, height};
  }

  std::vector<FNode> nodes_;
  std::vector<FEdge> edges_;
  std::vector<std::pair<int, int>> before_;
  Spacing sp_;
  std::vector<int> pos_, rank_;
  std::vector<PNode> pnodes_;
  std::vector<Seg> segs_;
  std::vector<std::vector<int>> chains_;
  std::vector<std::vector<int>> layers_;
  std::vector<std::vector<int>> up_, down_;
  std::vector<std::pair<int, int>> order_pairs_;
  std::vector<double> x_;
};

// ── The whole graph: containers from the innermost out ────────────────────────────────────────

bool along_flow(Placement p, Direction d) {
  bool vertical = p == Placement::Below || p == Placement::Above;
  return vertical == (d == Direction::Down || d == Direction::Up);
}

bool comes_after(Placement p, Direction d) {
  switch (d) {
    case Direction::Down: return p == Placement::Below;
    case Direction::Up: return p == Placement::Above;
    case Direction::Right: return p == Placement::RightOf;
    default: return p == Placement::LeftOf;
  }
}

class Engine {
 public:
  Engine(const LayoutGraph& graph, const std::vector<LayoutHint>& hints, const Geometry* previous)
      : g_(graph), hints_(hints), previous_(previous) {
    o_.dir = graph.options.direction;
    if (graph.options.spacing) {
      sp_.node = *graph.options.spacing;
      sp_.layer = *graph.options.spacing;
    }
    const int n = static_cast<int>(g_.nodes.size());
    for (int i = 0; i < n; ++i) index_[g_.nodes[i].id] = i;
    parent_.assign(static_cast<std::size_t>(n), -1);
    kids_.assign(static_cast<std::size_t>(n), {});
    for (int i = 0; i < n; ++i) {
      const auto& p = g_.nodes[i].parent;
      if (p && index_.count(*p)) {
        parent_[i] = index_.at(*p);
        kids_[parent_[i]].push_back(i);
      } else {
        roots_.push_back(i);
      }
    }
    size_.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) size_[i] = o_.internal(g_.nodes[i].width, g_.nodes[i].height);
    rel_.resize(static_cast<std::size_t>(n));
    for (const auto& h : hints_) {
      if (const auto* pin = std::get_if<PinHint>(&h)) {
        if (!index_.count(pin->element) || !index_.count(pin->reference)) continue;
        int el = index_.at(pin->element), ref = index_.at(pin->reference);
        if (along_flow(pin->placement, o_.dir)) {
          if (comes_after(pin->placement, o_.dir))
            along_.push_back({ref, el});
          else
            along_.push_back({el, ref});
        } else {
          bool after = pin->placement == Placement::Below || pin->placement == Placement::RightOf;
          before_.push_back(after ? std::pair{ref, el} : std::pair{el, ref});
        }
      } else if (const auto* order = std::get_if<OrderHint>(&h)) {
        for (std::size_t k = 1; k < order->elements.size(); ++k)
          if (index_.count(order->elements[k - 1]) && index_.count(order->elements[k]))
            before_.push_back({index_.at(order->elements[k - 1]), index_.at(order->elements[k])});
      }
    }
  }

  Geometry run() {
    Size root = layout_level(-1);
    // Absolute internal boxes.
    std::vector<Box> abs(g_.nodes.size());
    std::function<void(int, Point)> place = [&](int v, Point origin) {
      abs[v] = Box{origin.x + rel_[v].x, origin.y + rel_[v].y, rel_[v].width, rel_[v].height};
      Padding pad = o_.padding();
      for (int k : kids_[v]) place(k, {abs[v].x + pad.across_start, abs[v].y + pad.along_start});
    };
    for (int r : roots_) place(r, {kRootPadding, kRootPadding});
    const double along_total = root.height + 2 * kRootPadding;
    const double across_total = root.width + 2 * kRootPadding;

    Geometry g;
    for (std::size_t v = 0; v < g_.nodes.size(); ++v) g.nodes.set(g_.nodes[v].id, o_.to_final(abs[v], along_total));
    std::map<int, EdgeRoute> routes;
    for (auto& [level, route] : level_routes_) {
      Point origin{kRootPadding, kRootPadding};
      if (level >= 0) {
        Padding pad = o_.padding();
        origin = {abs[level].x + pad.across_start, abs[level].y + pad.along_start};
      }
      EdgeRoute r;
      for (const auto& p : route.points) r.points.push_back(o_.to_final(Point{p.x + origin.x, p.y + origin.y}, along_total));
      if (route.label) {
        Box b = *route.label;
        r.label = o_.to_final(Box{b.x + origin.x, b.y + origin.y, b.width, b.height}, along_total);
      }
      routes[route.id] = std::move(r);
    }
    // Edges no level could route: around the leaves, from and to the sides their ports name.
    std::vector<int> leaves;
    for (std::size_t v = 0; v < g_.nodes.size(); ++v)
      if (kids_[v].empty()) leaves.push_back(static_cast<int>(v));
    for (std::size_t ei = 0; ei < g_.edges.size(); ++ei) {
      if (routes.count(static_cast<int>(ei))) continue;
      const auto& e = g_.edges[ei];
      if (!index_.count(e.source) || !index_.count(e.target)) continue;
      int s = index_.at(e.source), t = index_.at(e.target);
      std::set<int> skip{s, t};
      for (int p = parent_[s]; p >= 0; p = parent_[p]) skip.insert(p);
      for (int p = parent_[t]; p >= 0; p = parent_[p]) skip.insert(p);
      std::vector<Box> obstacles;
      for (int v : leaves)
        if (!skip.count(v)) obstacles.push_back(*g.nodes.find(g_.nodes[v].id));
      const Box& sb = *g.nodes.find(e.source);
      const Box& tb = *g.nodes.find(e.target);
      EdgeRoute r;
      r.points = s == t ? self_loop(sb) : route_edge(sb, tb, obstacles, port_side(s, e.source_port), port_side(t, e.target_port));
      if (e.label) {
        auto around = obstacles;
        around.push_back(sb);
        around.push_back(tb);
        r.label = label_on_route(r.points, *e.label, around);
      }
      routes[static_cast<int>(ei)] = std::move(r);
    }
    for (std::size_t ei = 0; ei < g_.edges.size(); ++ei)
      if (routes.count(static_cast<int>(ei))) g.edges.set(g_.edges[ei].id, std::move(routes[static_cast<int>(ei)]));

    Box total = o_.to_final(Box{0, 0, across_total, along_total}, along_total);
    g.width = total.width;
    g.height = total.height;
    for (const auto& b : g.nodes) {
      g.width = std::max(g.width, b.x + b.width);
      g.height = std::max(g.height, b.y + b.height);
    }
    for (const auto& r : g.edges) {
      for (const auto& p : r.points) {
        g.width = std::max(g.width, p.x);
        g.height = std::max(g.height, p.y);
      }
      if (r.label) {
        g.width = std::max(g.width, r.label->x + r.label->width);
        g.height = std::max(g.height, r.label->y + r.label->height);
      }
    }
    return g;
  }

 private:
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

  std::optional<ISide> internal_side(int v, const std::optional<std::string>& port) const {
    auto s = port_side(v, port);
    if (!s) return std::nullopt;
    return o_.side(*s);
  }

  /// The direct child of `container` that holds `v`, or -1 when `v` is outside it.
  int child_of(int container, int v) const {
    for (int cur = v; cur >= 0; cur = parent_[cur])
      if (parent_[cur] == container) return cur;
    return -1;
  }

  Size layout_level(int container) {
    const std::vector<int>& members = container < 0 ? roots_ : kids_[container];
    Padding pad = o_.padding();
    for (int v : members) {
      if (kids_[v].empty()) continue;
      Size content = layout_level(v);
      size_[v] = {content.width + pad.across_start + pad.across_end, content.height + pad.along_start + pad.along_end};
    }
    std::map<int, int> local;
    std::vector<FNode> nodes;
    for (std::size_t k = 0; k < members.size(); ++k) {
      local[members[k]] = static_cast<int>(k);
      FNode f{size_[members[k]].width, size_[members[k]].height, members[k], std::nullopt};
      if (previous_)
        if (const Box* b = previous_->nodes.find(g_.nodes[members[k]].id))
          f.prior = o_.horizontal() ? b->y + b->height / 2 : b->x + b->width / 2;
      nodes.push_back(f);
    }
    std::vector<FEdge> edges;
    for (std::size_t ei = 0; ei < g_.edges.size(); ++ei) {
      const auto& e = g_.edges[ei];
      if (!index_.count(e.source) || !index_.count(e.target)) continue;
      int s = index_.at(e.source), t = index_.at(e.target);
      int a = child_of(container, s), b = child_of(container, t);
      if (a < 0 || b < 0) continue;
      if (a == b && !(s == t && a == s)) continue;
      FEdge fe;
      fe.s = local[a];
      fe.t = local[b];
      fe.id = static_cast<int>(ei);
      fe.local = a == s && b == t;
      if (fe.local) {
        if (e.label) fe.label = o_.internal(e.label->width, e.label->height);
        fe.minlen = e.label ? 2 : 1;
        fe.s_side = internal_side(s, e.source_port);
        fe.t_side = internal_side(t, e.target_port);
      }
      edges.push_back(fe);
    }
    for (const auto& [from, to] : along_) {
      int a = child_of(container, from), b = child_of(container, to);
      if (a < 0 || b < 0 || a == b) continue;
      FEdge fe;
      fe.s = local[a];
      fe.t = local[b];
      fe.id = -1;
      fe.local = false;
      edges.push_back(fe);
    }
    std::vector<std::pair<int, int>> before;
    for (const auto& [x, y] : before_) {
      int a = child_of(container, x), b = child_of(container, y);
      if (a < 0 || b < 0 || a == b) continue;
      before.push_back({local[a], local[b]});
    }
    FResult r = Level(std::move(nodes), std::move(edges), std::move(before), sp_).run();
    for (std::size_t k = 0; k < members.size(); ++k) rel_[members[k]] = r.boxes[k];
    for (auto& route : r.routes) level_routes_.push_back({container, std::move(route)});
    return r.size;
  }

  const LayoutGraph& g_;
  const std::vector<LayoutHint>& hints_;
  const Geometry* previous_;
  Orientation o_;
  Spacing sp_;
  std::map<std::string, int, std::less<>> index_;
  std::vector<int> parent_;
  std::vector<std::vector<int>> kids_;
  std::vector<int> roots_;
  std::vector<Size> size_;
  std::vector<Box> rel_;
  std::vector<std::pair<int, int>> along_, before_;
  std::vector<std::pair<int, FResult::Route>> level_routes_;
};

}  // namespace

Geometry layered_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints, bool constraints,
                        const Geometry* previous) {
  Geometry g = Engine(graph, hints, previous).run();
  for (const auto& h : hints)
    if (!hint_satisfied(h, g.nodes, graph.options.direction)) g.ignored_hints.push_back(h);
  if (constraints && !g.ignored_hints.empty()) return apply_constraints(graph, g, hints);
  return g;
}

}  // namespace dd
