#pragma once

// Layout input and output, a port of packages/layout/src/types.ts and the sequence geometry of
// sequence.ts. All coordinates are absolute, with the origin at the top-left of the diagram.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "diadraw/base/ordered_map.hpp"
#include "diadraw/model/model.hpp"

namespace dd {

struct Point {
  double x = 0;
  double y = 0;
  bool operator==(const Point&) const = default;
};

struct Size {
  double width = 0;
  double height = 0;
  bool operator==(const Size&) const = default;
};

struct Box {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  bool operator==(const Box&) const = default;
};

/// Where an adornment sits on its parent (draw.io's relative geometry): at `at`, a fraction of the
/// parent's box (1, 0 is its top right corner), moved by `offset` pixels.
struct Anchor {
  Point2 at;
  Point2 offset;
  bool operator==(const Anchor&) const = default;
};

struct LayoutNode {
  std::string id;
  double width = 0;
  double height = 0;
  /// Containing node, when the parent is also in the view.
  std::optional<std::string> parent;
  std::vector<Port> ports;
  /// An adornment of `parent`: a fixed layout keeps it on that spot of the parent's box.
  std::optional<Anchor> anchor = std::nullopt;
};

enum class EdgeRouting { Default, Straight, Orthogonal };

/// Where an edge's label sits on its route, as draw.io places it: at `along` (-1 the route's
/// start, 0 halfway along its length, 1 its end), `away` pixels off the line (to the left of its
/// direction), moved by `offset` pixels; `side` is how much of the label's width and height lie
/// before that point (0.5, 0.5 centres it; 0.5, 1 puts it above the point).
struct LabelAnchor {
  double along = 0;
  double away = 0;
  Point2 offset{0, 0};
  Point2 side{0.5, 0.5};
  bool operator==(const LabelAnchor&) const = default;
};

struct LayoutEdge {
  std::string id;
  std::string source;
  std::string target;
  std::optional<std::string> source_port;
  std::optional<std::string> target_port;
  /// Size of the edge label, so the engine leaves room for it.
  std::optional<Size> label;
  /// How the edge should be drawn where the engine has a choice (fixed layouts).
  EdgeRouting routing = EdgeRouting::Default;
  /// Where a fixed layout puts the label; elsewhere the engine picks a clear spot.
  std::optional<LabelAnchor> label_anchor = std::nullopt;
};

/// Engine-neutral input: what to lay out, without any model semantics.
struct LayoutGraph {
  std::vector<LayoutNode> nodes;
  std::vector<LayoutEdge> edges;
  LayoutOptions options;
};

struct EdgeRoute {
  /// Start point, bend points and end point.
  std::vector<Point> points;
  std::optional<Box> label;
};

// ── Sequence views ────────────────────────────────────────────────────────────────────────────

struct SequenceMessageGeometry {
  ElementId from;
  ElementId to;
  MessageStyle style = MessageStyle::Sync;
  std::vector<Point> points;
  std::optional<std::string> text;
  /// Where the label sits (above the arrow).
  std::optional<Box> label;
};

struct Lifeline {
  double x = 0;
  double top = 0;
  double bottom = 0;
};

struct LabelledY {
  double y = 0;
  std::optional<std::string> label;
};

struct FrameGeometry {
  FrameKind frame = FrameKind::Group;
  Box box;
  std::optional<std::string> label;
  /// Section separators after the first section.
  std::vector<LabelledY> separators;
};

struct DelayGeometry {
  double y = 0;
  double height = 0;
  std::optional<std::string> label;
};

struct SequenceGeometry {
  OrderedMap<Lifeline> lifelines;
  std::vector<SequenceMessageGeometry> messages;
  std::vector<std::pair<ElementId, Box>> activations;
  std::vector<FrameGeometry> frames;
  std::vector<std::pair<Box, std::string>> notes;  ///< box, text
  std::vector<LabelledY> dividers;
  std::vector<DelayGeometry> delays;
  std::vector<std::pair<Box, std::string>> refs;  ///< box, label
  /// Horizontal extent of the diagram, for dividers.
  double left = 0;
  double right = 0;
};

/// Where a view's note, text or box landed.
struct DecorationGeometry {
  /// Index into `view.decorations`.
  std::size_t index = 0;
  Box box;
  /// For a note attached to an element: a line from the note to the element's outline.
  std::vector<Point> connector;
};

/// Layout output.
struct Geometry {
  double width = 0;
  double height = 0;
  /// Boxes for elements (by element id) and groups (by `group_node_id(id)`).
  OrderedMap<Box> nodes;
  OrderedMap<EdgeRoute> edges;
  /// Hints the result does not satisfy, so callers can tell the user.
  std::vector<LayoutHint> ignored_hints;
  /// Lifelines, messages and the rest of a sequence view (`layout sequence`).
  std::optional<SequenceGeometry> sequence;
  /// The view's notes, text and boxes, in view order (decorations that cannot be placed are left out).
  std::vector<DecorationGeometry> decorations;
  /// Turned nodes (`rotate a 30`): degrees clockwise about the centre of the node's box, which is
  /// the unturned shape. Nodes that are not turned are left out.
  OrderedMap<double> rotations;
};

/// Sizes elements and edge labels before layout. The renderer supplies one that matches its theme.
struct Measure {
  std::function<Size(const Element&)> node;
  std::function<std::optional<Size>(const Relationship&)> edge_label;
  /// Size of a note or text decoration without an explicit size; unset falls back to an estimate.
  std::function<Size(const Decoration&)> decoration;
};

}  // namespace dd
