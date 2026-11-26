#pragma once

// Layout entry points: a port of packages/layout/src (graph.ts, hints.ts, route.ts, fixed.ts and
// sequence.ts, constraints.ts), plus the in-house layered engine that replaces ELK.

#include <map>

#include "diadraw/base/result.hpp"
#include "diadraw/layout/types.hpp"
#include "diadraw/model/views.hpp"

namespace dd {

inline constexpr Size kDefaultNodeSize{160, 60};

/// Every node 160×60; labels 7 px a character, 16 px high.
Measure default_measure();

/// Layout node id of a group, kept apart from element ids.
std::string group_node_id(std::string_view group_id);

/// The engine-neutral graph for a resolved view; visible parents and groups become compound nodes.
LayoutGraph to_layout_graph(const ResolvedView& view, const Measure& measure);

/// An element's anchor on its parent, from its style: `anchor=X,Y` (fractions of the parent's box)
/// and `anchorOffset=DX,DY` (pixels, 0,0 when missing). Nothing without a parent or a valid anchor.
std::optional<Anchor> anchor_of(const Element& element);
/// The box of an adornment anchored on `parent`, `size` big.
Box anchored_box(const Anchor& anchor, const Box& parent, Size size);

/// A relationship's label anchor, from its style: `labelAt=ALONG[,AWAY]`, `labelOffset=DX,DY`,
/// `align` (left, center, right: the side of the point the label starts on) and `verticalAlign`
/// (top, middle, bottom). Nothing when the style sets none of them.
[[nodiscard]] std::optional<LabelAnchor> label_anchor_of(const std::optional<Style>& style);
/// The label box for `anchor` on the route through `points`.
[[nodiscard]] Box label_at(const std::vector<Point>& points, Size size, const LabelAnchor& anchor);

/// Whether a hint holds in a finished layout. Hints naming nodes not in the layout count as unmet.
bool hint_satisfied(const LayoutHint& hint, const OrderedMap<Box>& nodes, Direction direction);

/// Drops points that lie on a straight line between their neighbours.
std::vector<Point> simplify(const std::vector<Point>& points);
/// Shortest orthogonal path between two points avoiding the obstacles; nullopt when none exists.
std::optional<std::vector<Point>> shortest_orthogonal(Point from, Point to, const std::vector<Box>& obstacles);
/// Orthogonal route between two boxes around obstacles.
std::vector<Point> route_edge(const Box& source, const Box& target, const std::vector<Box>& obstacles);
/// The same, leaving and entering on the given sides where they are set (ports with a side).
std::vector<Point> route_edge(const Box& source, const Box& target, const std::vector<Box>& obstacles,
                              std::optional<PortSide> source_side, std::optional<PortSide> target_side);
/// A label box on the longest route segment that keeps it clear of the obstacles.
Box label_on_route(const std::vector<Point>& points, Size size, const std::vector<Box>& obstacles = {});
/// Where the line from the box centre towards `toward` leaves the box.
Point boundary_point(const Box& b, Point toward);
/// The same for the box turned `degrees` clockwise about its centre.
Point boundary_point(const Box& b, Point toward, double degrees);

/// `p` turned `degrees` clockwise about `about` (y down, as on screen).
Point rotate_point(Point p, Point about, double degrees);
/// The axis-aligned box around `b` turned `degrees` clockwise about its centre.
Box rotated_bounds(const Box& b, double degrees);
/// The turn of each element a view rotates (its last `rotate`), leaving out 0° and elements that
/// contain others in the view, which are not turned.
std::map<std::string, double, std::less<>> view_rotations(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);

/// Nodes where `pin … at` puts them, edges along their `route` hints (imported drawings).
Geometry fixed_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);
/// Participants side by side and the steps of the sequence one below the other.
Geometry sequence_layout(const ResolvedView& view, const Measure& measure);

/// The in-house layered engine (replaces ELK). With `constraints`, hints the layering misses are
/// applied by the constraint pass afterwards. With `previous` (the last layout of this view),
/// node order starts from it and is kept unless changing it removes crossings.
Geometry layered_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints, bool constraints = true,
                        const Geometry* previous = nullptr);
/// Members of each container as a tidy tree along the view's direction, parents centred over
/// their children; tree edges drawn org-chart style, the rest routed orthogonally.
Geometry tree_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);
/// Members of each container in view order, in rows (`down`, `up`) or columns (`right`, `left`)
/// of ⌈√n⌉; edges routed orthogonally.
Geometry grid_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);
/// Members of each container on rings around the best-connected one; straight edges.
Geometry radial_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);
/// Force-directed placement of each container's members (deterministic); straight edges.
Geometry force_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints);
/// Lays out a graph with the engine its options name: `tree`, `grid`, `radial`, `force`, and
/// `layered` (with `previous`) for every other name. `fixed` and `sequence` are not graph engines.
Geometry graph_layout(const LayoutGraph& graph, const std::vector<LayoutHint>& hints,
                      const Geometry* previous = nullptr);
/// The constraint pass: applies hints a layout did not honour and reroutes what moved.
Geometry apply_constraints(const LayoutGraph& graph, const Geometry& geometry, const std::vector<LayoutHint>& hints);

/// Moves the ends of a layered layout's edges from the nodes' boxes onto their outlines, for
/// shapes smaller than their box (diamonds, ellipses, circles). `layout` does this already.
void clip_to_outlines(Geometry& geometry, const LayoutGraph& graph, const ResolvedView& view);

/// Places the view's notes, text and boxes around a finished layout: beside the element they are
/// attached to, around the elements they frame, at their coordinates, or (unanchored) notes in a
/// column to the right and text above the diagram. `layout` does this already.
void place_decorations(Geometry& geometry, const ResolvedView& view, const Measure& measure);
/// Lays out a view with the algorithm it names.
Result<Geometry> layout(const ResolvedView& view, const Measure& measure);
/// The same without the decorations (`place_decorations` adds them). Sizes and turns are applied
/// here; `previous` is the view's last layout, for the layered engine.
Geometry layout_nodes(const ResolvedView& view, const Measure& measure, const Geometry* previous = nullptr);

}  // namespace dd
