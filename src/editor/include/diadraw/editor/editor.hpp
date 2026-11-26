#pragma once

// Canvas gestures as operations (a port of packages/editor/src/index.ts), and `Session`: the
// document facade every UI drives. A session holds the source text, its parse and the last
// layout of each view; canvas gestures become operations, and operations become minimal text
// patches, so the text stays the source of truth whichever pane made the change.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::editor {

struct Selection {
  std::vector<ElementId> elements;
  std::vector<RelationshipId> relationships;
};

/// Distance (px) within which a dropped node snaps into alignment with another node.
inline constexpr double kAlignTolerance = 14;

/// True when the hint positions `id` itself (so a new drag of `id` replaces it).
bool hint_places(const LayoutHint& hint, std::string_view id);

/// Layout intent for a drop: alignment when the node lines up with another one, otherwise a pin
/// beside its nearest neighbour on the side it was dropped; with `freeform`, a pin at the spot.
std::optional<LayoutHint> hint_for_drop(const Geometry& geometry, const ElementId& id, const Box& dropped,
                                        const std::vector<ElementId>& candidates, bool freeform = false);

/// An adornment (an element anchored on its parent, see `anchor_of`) moved so its box's corner is
/// at `to`, in a fixed view that `geometry` draws.
struct AdornmentMove {
  /// Whether the element is an adornment drawn on its parent there; when not, it moves as usual.
  bool adornment = false;
  /// The change to its `anchorOffset` that keeps it at `to` on its parent; nothing when it is
  /// there already (within half a pixel, so a resize from the far corner keeps its offset).
  std::optional<Operation> change;
};
AdornmentMove adornment_move(const Model& model, const Geometry& geometry, const ElementId& id, Point to);

/// Replaces the hints that place `id` in a view with `hint`, as one undoable batch. When the last of
/// them is the same sort of hint (a `pin … at` moved or resized), it is rewritten where it stands;
/// otherwise `hint` is appended. A `pin … at` without a size keeps the size of the one it replaces.
Result<Operation> place_operation(const Model& model, std::string_view view_id, const ElementId& id, LayoutHint hint);

/// Moves several elements of a view together by `offset`, as one undoable batch: each gets a
/// `pin id at X, Y` (rounded) where `geometry` (the view's last layout) has it plus the offset, keeping
/// a pinned size (see `place_operation`). In a `layout fixed` view the bend points of the lines whose
/// two ends both move shift with them. Elements `geometry` does not place are skipped; nullopt when
/// nothing moves; an error for an offset that is not finite.
Result<std::optional<Operation>> move_operation(const Model& model, std::string_view view_id, const Geometry& geometry,
                                                const std::vector<ElementId>& ids, Point offset);

/// Sets a relationship's bend points in a view (`route "id" via …`, rounded), keeping the route's
/// `from` and `to`; the route is rewritten where it stands. No bend points and no `from`/`to`
/// remove the route. Nothing when the route already says so.
Result<std::optional<Operation>> route_operation(const Model& model, std::string_view view_id,
                                                 const RelationshipId& relationship, const std::vector<Point>& points);

/// Sets an element's size in a view that is laid out automatically (`size id W, H`, rounded), or
/// with nullopt removes it. The last `size` for the element is rewritten where it stands and
/// earlier ones are removed. Nothing when the view already says so.
Result<std::optional<Operation>> size_operation(const Model& model, std::string_view view_id, const ElementId& id,
                                                std::optional<Size> size);

/// Sizes elements to fit their content in a view: removes their `size` hints and the sizes of
/// their `pin … at` hints, so a leaf takes the size its name and shape measure and keeps its
/// position. In a `layout fixed` view a container's pin goes altogether, sized or not, since a
/// pinned container keeps its pinned box there and only an unpinned one wraps its children.
/// Nothing when that is how the view reads already.
Result<std::optional<Operation>> fit_operation(const Model& model, std::string_view view_id,
                                               const std::vector<ElementId>& ids);

/// Turns an element in a view (`rotate id D`, D rounded to a tenth of a degree and brought into
/// (-180, 180]); 0 removes the turn. As `size_operation`, the last one is rewritten where it stands.
Result<std::optional<Operation>> rotate_operation(const Model& model, std::string_view view_id, const ElementId& id,
                                                  double degrees);

/// An id for a new child of `parent` (or a top-level element) that is not taken yet.
ElementId fresh_element_id(const Model& model, std::string_view base, const std::optional<ElementId>& parent = {});

struct Created {
  ElementId id;
  Operation operation;
};
Created create_operation(const Model& model, const std::string& kind, const std::optional<ElementId>& parent = {},
                         const std::optional<std::string>& name = {}, const std::optional<std::string>& shape = {});

/// A relationship from `source` to `target` with the id the text will give it.
Operation connect_operation(const Model& model, const ElementId& source, const ElementId& target,
                            const std::optional<std::string>& label = {});

/// An inspector's edit of a relationship: `relationship`'s label, tech, arrow, tags, style and
/// props (its id says which one), with `name` as its explicit id, or none for an id derived from
/// its endpoints. Naming or unnaming a line can renumber the derived ids of the lines between the
/// same elements after it, as reading the text back would; those move too, with their routes.
/// `renamed` is the relationship's id afterwards. Nullopt when nothing changes; an error when the
/// name is not an identifier or another relationship has it.
Result<std::optional<Operation>> update_relationship_operation(const Model& model, const Relationship& relationship,
                                                               const std::optional<std::string>& name,
                                                               RelationshipId* renamed = nullptr);
/// One end of a relationship.
enum class End { Source, Target };

/// Moves one end of relationship `id` to `to` (an element, or one of its ports), with the ids the
/// text gives relationships once it is read back: a named relationship (`calls = a -> b`) keeps
/// its id; one whose id is derived from its endpoints (`a->b`, `a->b#2`) gets the id of its new
/// endpoints, and so does any parallel line whose number shifts. `route`s follow the new ids.
/// nullopt when that end is already at `to`.
Result<std::optional<Operation>> reconnect_operation(const dsl::ParseResult& parsed, const RelationshipId& id, End end,
                                                     const Endpoint& to);

/// Renames element `id`: `to` replaces the last part of its dotted id (`shop.web` with `store`
/// becomes `shop.store`), so it stays where it is. Its descendants' ids and every reference to any
/// of them follow, and so do the ids the text derives from them: relationships without a name
/// (`shop.web->db`), with their `route`s, and group ids: those declared inside it follow its path,
/// and any group may renumber (`#2`) as the text numbers repeats. `renamed` is the
/// element's id afterwards. Nullopt when `to` is its name already; an error when `to` is not an
/// identifier or a sibling (or a descendant's new id) is taken.
Result<std::optional<Operation>> change_element_id_operation(const dsl::ParseResult& parsed, const ElementId& id,
                                                             std::string_view to, ElementId* renamed = nullptr);

/// An element name a rename can start from: in the element's declaration or in a reference to it
/// (the part of a dotted reference under the cursor, so `shop` in `shop.web` is `shop`).
struct RenameTarget {
  ElementId element;
  /// The name as written there.
  dsl::Span span;
};
/// The element name at byte `offset` (touching it counts), from the parse's source map; nothing
/// on anything else, a port included.
std::optional<RenameTarget> rename_target(const dsl::ParseResult& parsed, std::size_t offset);

/// A rename of an element written into the text.
struct ElementRename {
  /// The element's id afterwards.
  ElementId id;
  op::ChangeElementId operation;
  dsl::SourceUpdate update;
};
/// Renames element `id` in `source` (`parsed` is its parse), as change_element_id_operation
/// describes, patching the declaration and every reference where they are written. An error when
/// the patched text would not read back as the renamed model: a name the grammar reads as a
/// keyword there, or one that a reference elsewhere would then resolve to instead. Nullopt when
/// `to` is the element's name already.
Result<std::optional<ElementRename>> rename_element(std::string_view source, const dsl::ParseResult& parsed,
                                                    const ElementId& id, std::string_view to);

/// An inspector field whose value an edit would refuse, and why.
struct FieldProblem {
  /// The field: `id`, `kind`, `shape` or `style` of an element, `name` or `style` of a
  /// relationship.
  std::string field;
  std::string message;
  bool operator==(const FieldProblem&) const = default;
};

/// What an inspector's edit of an element would refuse, field by field, without changing
/// anything, so a shell can mark those fields while they are typed in. `edited` is the element
/// with the typed fields applied (its id still the one it has), `local_id` the trimmed Id field:
/// `id` when rename_element would refuse it (not a name, a sibling's, or one the text would read
/// differently), and `kind`, `shape` and `style` when updating the element would (see
/// op::UpdateElement). Empty when the whole edit applies.
std::vector<FieldProblem> element_problems(std::string_view source, const dsl::ParseResult& parsed,
                                           const Element& edited, std::string_view local_id);
/// Why rename_element would refuse to rename element `id` to `local_id`, or nothing: the `id`
/// problem of element_problems. A dry run of the rename (a patch and a parse of the text) when
/// `local_id` is a free name other than the element's own, so a shell typing in the Id field may
/// keep the answer for as long as the text and the field stay the same.
std::optional<std::string> element_id_problem(std::string_view source, const dsl::ParseResult& parsed,
                                              const ElementId& id, std::string_view local_id);

/// The same for a relationship: `name` when update_relationship_operation would refuse it, and
/// `style` when updating the relationship would.
std::vector<FieldProblem> relationship_problems(const Model& model, const Relationship& edited,
                                                const std::optional<std::string>& name);

/// Deletes a selection: relationships first, then the outermost selected elements.
std::optional<Operation> delete_operation(const Model& model, const Selection& selection);

/// An id for a new view that no view of `model` has yet: `view1`, `view2`, …
ViewId fresh_view_id(const Model& model);

/// Adds `view id { include * }` (`of scope` when set) after the other views. `declared` is the
/// model as the text has it: when the text declares no view, the view the document is drawn in
/// (`view default`) is written out first, so it stays. An error when `id` is not a name or is taken.
Result<Operation> add_view_operation(const Model& declared, const ViewId& id,
                                     const std::optional<ElementId>& scope = {});

/// Removes view `id` from the text; an error when it is the document's only view.
Result<Operation> remove_view_operation(const Model& declared, const ViewId& id);

/// Gives view `id` the id `to`, where it stands. The implied `view default` of a text without
/// views is written out under its new id. Nullopt when the id stays; an error when `to` is not a
/// name or another view has it.
Result<std::optional<Operation>> rename_view_operation(const Model& declared, const ViewId& id, const ViewId& to);

/// Elements a drop can be placed against: visible leaf nodes of the view.
std::vector<ElementId> drop_candidates(const Model& model, const std::vector<ElementId>& visible);

/// A laid-out, rendered view.
struct ViewRender {
  ResolvedView view;
  Geometry geometry;
  SvgScene scene;
};

/// Library shapes, kinds and themes for a model: hosts resolve the libraries it uses (built-in, loaded,
/// installed). A provider that returns only a ShapeResolver still works.
using ShapeProvider = std::function<LibraryArt(const Model&)>;

/// What changed in a session, as listeners hear it.
struct SessionEvent {
  enum class Kind {
    /// The text changed: the host set it (`update` is null), or an operation or a gesture patched it.
    Source,
    /// Library shapes were resolved again; views look different, the text did not change.
    Libraries,
  };
  Kind kind = Kind::Source;
  /// The text before the change (for `Source`).
  std::string_view before;
  const dsl::SourceUpdate* update = nullptr;
};

class Session {
 public:
  using Listener = std::function<void(const SessionEvent&)>;

  explicit Session(std::string source = "", ShapeProvider shapes = {});

  /// Hears every change after it is made (the session is already in its new state). One listener;
  /// pass {} to stop.
  void set_listener(Listener listener) { listener_ = std::move(listener); }

  /// Re-resolves library shapes (after the host made more libraries available).
  void refresh_libraries();

  [[nodiscard]] const std::string& source() const { return source_; }
  [[nodiscard]] const dsl::ParseResult& parsed() const { return parsed_; }
  /// The model with an implicit view of everything when the text declares none.
  [[nodiscard]] const Model& model() const { return model_; }

  /// Replaces the text (typing in the editor); layouts are kept as starting points.
  void set_source(std::string source);

  /// Lays out and renders a view, starting from its last layout so small edits stay put.
  Result<ViewRender> render(std::string_view view_id);

  /// Applies an operation to the text; the session moves to the new text.
  Result<dsl::SourceUpdate> apply(const Operation& op);

  /// The hint a drop would write, without writing it (for a preview while dragging).
  Result<std::optional<LayoutHint>> drop_hint(std::string_view view_id, const ElementId& id, const Box& dropped,
                                              bool freeform = false);
  /// A standalone SVG document of a view as last rendered (rendering it first if needed).
  Result<std::string> export_svg(std::string_view view_id);

  // Gestures. Each returns the text update, or nullopt when the gesture changes nothing.
  Result<std::optional<dsl::SourceUpdate>> drop(std::string_view view_id, const ElementId& id, const Box& dropped,
                                                bool freeform = false);
  Result<dsl::SourceUpdate> connect(const ElementId& source, const ElementId& target,
                                    const std::optional<std::string>& label = {});
  Result<std::pair<ElementId, dsl::SourceUpdate>> create(const std::string& kind,
                                                         const std::optional<ElementId>& parent = {},
                                                         const std::optional<std::string>& name = {},
                                                         const std::optional<std::string>& shape = {});
  Result<std::optional<dsl::SourceUpdate>> remove(const Selection& selection);
  /// Several elements dragged together by `offset`, one change (see move_operation).
  Result<std::optional<dsl::SourceUpdate>> move(std::string_view view_id, const std::vector<ElementId>& ids,
                                                Point offset);
  /// A resize. In a `layout fixed` view it pins `id` at `box`, size included
  /// (`pin id at X, Y size W, H`); in a view laid out automatically it sets the size the layout
  /// gives the element (`size id W, H`) and the layout places it.
  Result<std::optional<dsl::SourceUpdate>> resize(std::string_view view_id, const ElementId& id, const Box& box);
  /// Sizes elements to fit their content (see `fit_operation`).
  Result<std::optional<dsl::SourceUpdate>> fit(std::string_view view_id, const std::vector<ElementId>& ids);
  /// Turns `id` by `degrees` clockwise in any view (`rotate id D`; 0 removes it).
  Result<std::optional<dsl::SourceUpdate>> rotate(std::string_view view_id, const ElementId& id, double degrees);
  /// Dragged bend points in a `layout fixed` view: `route "id" via …` (see `route_operation`).
  Result<std::optional<dsl::SourceUpdate>> reroute(std::string_view view_id, const RelationshipId& relationship,
                                                   const std::vector<Point>& points);
  struct Reconnected {
    /// The relationship's id afterwards, which differs when it was derived from its endpoints.
    RelationshipId id;
    dsl::SourceUpdate update;
  };
  /// Moves one end of a relationship to another element or port (see reconnect_operation).
  Result<std::optional<Reconnected>> reconnect(const RelationshipId& id, End end, const Endpoint& to);
  /// Sets an element's kind, name, tech, shape, tags, description, style and props to
  /// `element`'s (an inspector's edit); nullopt when they are already so.
  Result<std::optional<dsl::SourceUpdate>> update_element(const Element& element);
  /// Renames an element, every reference to it and the ids derived from it (see rename_element);
  /// on an error nothing changes. Last layouts follow the new ids, so the diagram stays as it was
  /// drawn.
  Result<std::optional<ElementRename>> rename_element(const ElementId& id, std::string_view to);
  /// Sets a relationship's label, tech, arrow, tags, style, props and name (see
  /// update_relationship_operation); `renamed` is its id afterwards.
  Result<std::optional<dsl::SourceUpdate>> update_relationship(const Relationship& relationship,
                                                               const std::optional<std::string>& name,
                                                               RelationshipId* renamed = nullptr);

 private:
  void reparse();
  /// Moves the session to an update of `op` that dsl::update_source made, and tells the listener.
  dsl::SourceUpdate adopt(const Operation& op, dsl::SourceUpdate update);
  /// The view's last layout, laying it out first if it has none yet.
  Result<const Geometry*> last_geometry(std::string_view view_id);
  /// Library shapes for the current model, resolved again when its `use` lines change.
  void update_shapes(bool force = false);

  Listener listener_;
  ShapeProvider provider_;
  LibraryArt art_;
  std::optional<std::vector<LibraryUse>> uses_;
  bool resolved_ = false;
  std::string source_;
  dsl::ParseResult parsed_;
  Model model_;
  Measure measure_;
  std::map<std::string, Geometry, std::less<>> last_;
};

}  // namespace dd::editor
