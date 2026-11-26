#pragma once

// The diagram canvas as an editor, without a window: what every native shell (AppKit, gtkmm,
// WinUI) shares. It began as a port of the interaction in src/apps/web/src/canvas.ts, which has
// the same gestures (its geometry is in canvas-geometry.ts). Select (Shift adds), drag to move
// (to the exact spot; with snap, placed by its neighbours), drag the handle to connect, drag an
// end of a selected line to reconnect it, double-click to rename or relabel, Delete to remove,
// Escape to clear.
// Shift-click (or Command-click on macOS, Ctrl-click elsewhere) adds an item to the selection or
// takes it out; dragging over empty canvas selects what the band encloses (with Shift or
// Command/Ctrl, adds it); `select_all` selects everything. Dragging one of several selected nodes
// moves them all by the same offset, as one change (each pinned where it lands); a click on one of
// them without a drag selects it alone. A selected node has corner handles to resize it
// (double-click one to fit the node to its content) and a round handle above it to turn it (in
// any view), and in a `layout fixed` view a selected relationship has handles on its bend points
// (drag one to move it, drag a segment's middle to add one, drag one onto the straight line
// between its neighbours to remove it).
//
// With a grid shown (`set_grid`), what a gesture writes lines up with it: a plain move drops the
// box's top-left corner on the nearest grid point, a resize in a fixed view puts the dragged corner
// on one (elsewhere, and on a turned node, the size becomes a whole number of grid steps), and a
// bend point lands on one. Option/Alt drops still go by the neighbours; turns keep their 15° steps.
// Holding Shift while dragging a corner keeps the node's proportions; that resize ignores the grid.
//
// A shell forwards pointer and key events in view coordinates (pixels, y down, origin at the
// canvas's top-left), paints `list()` and then `overlay()` with its own painter, and turns the
// callbacks into core calls. Inline editing is the shell's (a native text field): `press` returns
// what to edit and where, and `commit_edit` reports the result.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "diadraw/editor/editor.hpp"
#include "diadraw/render/display_list.hpp"

namespace dd::canvas {

struct Modifiers {
  bool shift = false;     ///< add to / remove from the selection; on a resize, keep the proportions
  bool snap = false;      ///< place a drop by its neighbours, not at the exact spot (Option on macOS, Alt elsewhere)
  bool toggle = false;    ///< add to / remove from the selection only (Command on macOS, Ctrl elsewhere)
};

/// What the canvas asks of its host. Unset callbacks are skipped.
struct Callbacks {
  std::function<void(const editor::Selection&)> selected;
  std::function<void(const std::string& element, const Box& box, bool freeform)> dropped;
  /// Several selected nodes dragged together: each moves by `offset` (diagram units) and is pinned
  /// where it lands. One node dragged alone is `dropped`.
  std::function<void(const std::vector<std::string>& elements, const Point& offset)> moved;
  std::function<void(const std::string& source, const std::string& target)> connected;
  /// An end of a relationship was dropped on another element.
  std::function<void(const std::string& relationship, editor::End end, const std::string& element)> reconnected;
  std::function<void(const std::string& element, const std::string& name)> renamed;
  std::function<void(const std::string& relationship, const std::optional<std::string>& label)> relabelled;
  std::function<void(const editor::Selection&)> deleted;
  /// A node resized: its new box (diagram units, unturned). Fixed views pin it there; others keep
  /// only the size.
  std::function<void(const std::string& element, const Box& box)> resized;
  /// A resize handle double-clicked: size the node to fit its content.
  std::function<void(const std::string& element)> fitted;
  /// A node turned with its rotate handle: degrees clockwise (0 is upright).
  std::function<void(const std::string& element, double degrees)> rotated;
  /// A relationship's bend points dragged in a fixed view: all of them, in order (none: a straight line).
  std::function<void(const std::string& relationship, const std::vector<Point>& points)> rerouted;
  /// The hint a drop would write, for the preview while dragging.
  std::function<std::optional<LayoutHint>(const std::string& element, const Box& box, bool freeform)> preview_drop;
  std::function<std::string(const std::string& element)> name_of;
  std::function<std::string(const std::string& relationship)> label_of;
};

/// An inline edit the host should open: a text field over `area` (view coordinates).
struct Edit {
  std::string id;
  bool relationship = false;
  std::string value;
  double x = 0, y = 0, width = 0, height = 0;

  /// An element's name may span lines (it is drawn with its line breaks), so its field takes
  /// several: Enter starts a new line and Ctrl/⌘+Enter commits. A relationship's label is one line.
  [[nodiscard]] bool multiline() const { return !relationship; }
};

class Canvas {
 public:
  Callbacks callbacks;

  /// Shows a newly rendered view; the selection keeps what still exists (reported if it shrank).
  void show(const editor::ViewRender& render);
  void clear();
  /// The rendered view. With the grid shown its background is left out: `backdrop()` paints it,
  /// under the grid.
  [[nodiscard]] const std::optional<draw::DisplayList>& list() const { return bare_ ? bare_ : list_; }

  [[nodiscard]] double zoom() const { return zoom_; }
  void set_zoom(double zoom);
  /// The drawing's size in view pixels at the current zoom (0 × 0 when empty).
  [[nodiscard]] double width() const;
  [[nodiscard]] double height() const;

  /// The grid's step in diagram units; 0 hides it and stops snapping.
  [[nodiscard]] double grid() const { return grid_; }
  /// Shows the grid with a step in diagram units; 0, a negative step or one that is not finite hides it.
  void set_grid(double step);
  /// What to paint before `list()`: with the grid shown, the drawing's background and the grid,
  /// both over an area of `width` × `height` view pixels from the canvas's top-left (the shell's whole
  /// canvas, which can be larger than the drawing); empty with the grid hidden. Its root is the
  /// list's root, as with `overlay()`.
  [[nodiscard]] draw::DisplayList backdrop(double width, double height) const;

  [[nodiscard]] const editor::Selection& selection() const { return selection_; }
  /// Sets the selection without reporting it (the host already knows).
  void set_selection(editor::Selection selection) { selection_ = std::move(selection); }

  /// How the shell reports a double-click. By default (AppKit) each physical press is one `press`
  /// carrying its click count. Toolkits whose double-click event comes on top of the second
  /// press's own single `press` (gtkmm, WinUI) set this, so the canvas knows which press began
  /// the double-click.
  void set_double_click_repeats_press(bool repeats) { double_click_repeats_press_ = repeats; }

  // Events, in view coordinates. Each returns true when the canvas must be repainted.
  /// A button press; `clicks` 2 asks for an inline edit, returned in `edit`. A double-click on a
  /// resize handle whose first click took that handle reports `fitted` instead.
  bool press(double x, double y, int clicks, Modifiers modifiers, std::optional<Edit>* edit = nullptr);
  bool drag(double x, double y, Modifiers modifiers);
  bool release(double x, double y, Modifiers modifiers);
  bool escape();
  bool delete_selection();
  /// Selects every node and every relationship drawn in the view (implied lines excepted); reported.
  bool select_all();
  /// Finishes an inline edit with the text the person typed. Line breaks may be `\r\n` or `\r` (as
  /// native multi-line fields give them); they become `\n`.
  void commit_edit(const Edit& edit, const std::string& text);

  /// Selection, drag ghost and hint, connector and handle: to paint over `list()`. Its root is the
  /// list's root, so it paints in the same space.
  [[nodiscard]] draw::DisplayList overlay() const;

  // Geometry, also for scripted tests.
  [[nodiscard]] draw::Point to_user(double x, double y) const;
  [[nodiscard]] draw::Point to_view(double x, double y) const;
  [[nodiscard]] std::optional<draw::Point> center_of(const std::string& element) const;
  [[nodiscard]] std::optional<draw::Point> handle_point() const;
  /// Where the handle on an end of the selected relationship is (one relationship selected).
  [[nodiscard]] std::optional<draw::Point> end_point(editor::End end) const;
  [[nodiscard]] std::optional<draw::Point> label_point(const std::string& relationship) const;
  /// Diagram units per view pixel.
  [[nodiscard]] double units_per_pixel() const;
  [[nodiscard]] std::optional<std::string> node_at(draw::Point user, bool leaves_only) const;
  [[nodiscard]] std::optional<std::string> edge_at(draw::Point user) const;
  /// True when the view is laid out `fixed`, so nodes move exactly and bend points move.
  [[nodiscard]] bool fixed() const { return fixed_; }
  /// Resize handles of the one selected node: top-left, top-right, bottom-right, bottom-left of
  /// its (possibly turned) box, in view coordinates. In views laid out automatically only leaf
  /// nodes resize, and not in sequence views' frames.
  [[nodiscard]] std::vector<draw::Point> resize_points() const;
  /// The rotate handle of the one selected leaf node, above its top edge (view coordinates); none
  /// in sequence views, which do not turn nodes.
  [[nodiscard]] std::optional<draw::Point> rotate_point() const;
  /// The selected node's turn, in degrees (0 when it is upright).
  [[nodiscard]] double rotation_of(const std::string& element) const;
  /// What a band dragged from `a` to `b` (diagram units, any two opposite corners) selects: the
  /// nodes whose (turned) boxes lie wholly inside it, and the relationships, implied ones excepted,
  /// whose whole lines do.
  [[nodiscard]] editor::Selection enclosed(draw::Point a, draw::Point b) const;
  /// Bend handles of the one selected relationship in a fixed view (view coordinates): its bend
  /// points, then the middle of each segment, where dragging adds a bend point.
  [[nodiscard]] std::vector<draw::Point> bend_points() const;
  /// What a click at `user` is on: a node or a line, at most one of them. Leaf nodes win over
  /// lines, and lines over the containers they cross.
  [[nodiscard]] std::pair<std::optional<std::string>, std::optional<std::string>> hit(draw::Point user) const;

 private:
  enum class GestureKind { Idle, Press, Move, Connect, Resize, Rotate, Bend, Reconnect, Band };
  struct Gesture {
    GestureKind kind = GestureKind::Idle;
    std::string id;
    draw::Point start, current;
    bool freeform = false;
    std::optional<std::string> target;
    /// Resize: the corner held (0 top-left, clockwise). Bend: the route point held, or with
    /// `insert`, the segment whose middle was held.
    std::size_t handle = 0;
    bool insert = false;
    bool moved = false;  ///< a resize, bend or band drag passed the drag threshold
    bool extend = false;  ///< Band: Shift or Command/Ctrl was held, so the band adds to the selection
    /// Press: a plain press on one of several selected items; let go without a drag, it selects
    /// that node alone (a drag moves them all).
    bool narrow = false;
    bool keep_aspect = false;  ///< Resize: Shift is held, so the width and height scale together
    editor::End end = editor::End::Target;  ///< the end a Reconnect moves
  };

  void select(editor::Selection selection);
  [[nodiscard]] std::optional<Box> handle_box() const;
  /// The relationship whose ends can be dragged: the only selected item, drawn, not implied.
  [[nodiscard]] const EdgeRoute* reconnectable() const;
  [[nodiscard]] std::optional<Box> end_box(editor::End end) const;
  /// The element an end may be dropped on at `p`: not the one it is on, nor the other end's.
  [[nodiscard]] std::optional<std::string> reconnect_target(draw::Point p) const;
  [[nodiscard]] std::optional<Box> moved_box() const;
  /// Whether a Move carries the selection along: the grabbed node is one of several selected
  /// (otherwise it moves alone).
  [[nodiscard]] bool grouped() const;
  /// The band's rectangle while one is dragged (diagram units).
  [[nodiscard]] std::optional<Box> band() const;
  [[nodiscard]] std::optional<std::string> resizable() const;
  [[nodiscard]] std::optional<std::string> rotatable() const;
  [[nodiscard]] std::optional<Point> rotate_handle() const;
  /// The turn a rotate drag gives, snapped to 15° steps within 5°.
  [[nodiscard]] std::optional<double> turned() const;
  [[nodiscard]] std::optional<std::string> bendable() const;
  [[nodiscard]] std::optional<Box> resized_box() const;
  [[nodiscard]] std::optional<std::vector<Point>> bent_route() const;
  [[nodiscard]] std::optional<Edit> edit_at(draw::Point user) const;

  /// `v` on the nearest grid line (unchanged without a grid).
  [[nodiscard]] double snap(double v) const;
  /// Fills `bare_` from `list_` while the grid shows and the list has a background; clears it
  /// otherwise, so a render without the grid copies nothing.
  void split_background();

  std::optional<draw::DisplayList> list_;
  /// The list's background (none when it has none), and with the grid shown, `list_` without it.
  std::optional<draw::Item> background_;
  std::optional<draw::DisplayList> bare_;
  double grid_ = 0;
  Geometry geometry_;
  std::vector<std::string> elements_;
  std::vector<std::string> containers_;
  bool fixed_ = false;
  bool sequence_ = false;
  /// Relationships of the view, with their ends; implied ones are left out (they are not in the
  /// text, so they cannot be reconnected).
  std::map<std::string, std::pair<std::string, std::string>, std::less<>> ends_;
  editor::Selection selection_;
  Gesture gesture_;
  /// The node and resize handle the last single press took (none when it took none), and the one
  /// before it, so that only a double-click whose first click took a handle fits the node.
  using CornerPress = std::optional<std::pair<std::string, std::size_t>>;
  CornerPress corner_press_;
  CornerPress previous_corner_press_;
  bool double_click_repeats_press_ = false;
  double zoom_ = 1;
};

/// A short phrase for a hint, shown while dragging ("below Shop", "align with row of API").
std::string describe_hint(const LayoutHint& hint, const std::function<std::string(const std::string&)>& name);

}  // namespace dd::canvas
