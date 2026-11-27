#pragma once

// One open .draw file as every native shell edits it: the core session (with libraries resolved
// next to the file), its views and the one on the canvas, the status line, and canvas gestures
// turned into text changes. Shells keep only widgets: a text view, a canvas
// (dd::canvas::Canvas) and menus.

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/canvas/canvas.hpp"
#include "diadraw/editor/editor.hpp"
#include "host/files.hpp"
#include "host/field_editors.hpp"

namespace dd::host {

/// A diagram file in another format, imported as DiaDraw text.
struct Imported {
  std::string text;
  /// What the import could not carry over, for the status line ("" when everything came across).
  std::string report;
};
/// Reads and imports a Mermaid, PlantUML, Structurizr, draw.io or BPMN file, with local includes
/// read from disk and the draw.io stencil index when available.
Result<Imported> import_file(const std::string& path);

/// A text change for the shell to apply to its text view as one undo step: `edits` are UTF-8
/// byte offsets into the text before, last edit first (so earlier offsets stay valid).
struct Change {
  std::vector<dsl::TextEdit> edits;
  /// Set when the change could not be made in place and the file was reprinted.
  std::optional<std::string> note;
};

/// An element's editable properties as text, one string per inspector field. Empty optional
/// fields (`technology`, `shape`, `description`, `style`) mean "not set".
struct ElementFields {
  std::string id;  ///< which element
  /// The last part of the id, as the declaration writes it (`web` for `shop.web`); another one
  /// renames the element and every reference to it (see Document::update).
  std::string local_id;
  std::string kind;
  std::string name;
  std::string technology;
  std::string shape;
  std::string description;
  /// Comma-separated: `external, legacy`.
  std::string tags;
  /// As inline `style "…"` takes it: `fill=#dae8fc;stroke=#6c8ebf`.
  std::string style;
  /// One `key=value` per line.
  std::string props;
  /// The `style` blocks that apply to the element, one `selector: key=value;…` per line (see
  /// host/styles.hpp). Filled by Document::fields; fields_of and apply_fields leave it alone.
  std::string shared_styles;
};

/// The element with `fields` applied to it (the parts of `element` the fields do not cover stay).
Element apply_fields(Element element, const ElementFields& fields);
ElementFields fields_of(const Element& element);

/// A relationship's editable properties as text, one string per inspector field, as for elements.
struct RelationshipFields {
  std::string id;  ///< which relationship; not editable
  /// The ends as the text writes them (`web`, `api.http`), for the inspector's title; not editable.
  std::string source;
  std::string target;
  /// The explicit id in `name = a -> b`; empty for an id derived from the ends.
  std::string name;
  std::string label;
  std::string technology;
  /// `->`, `-->` or `<->`.
  std::string arrow;
  /// Comma-separated, as for elements.
  std::string tags;
  std::string style;
  /// One `key=value` per line.
  std::string props;
};

/// The relationship with `fields` applied to it (its id and ends stay; `name` is applied by
/// Document::update, since it can change other ids). An arrow other than the three is left as it was.
Relationship apply_fields(Relationship relationship, const RelationshipFields& fields);
RelationshipFields fields_of(const Relationship& relationship);
/// The arrow a relationship is drawn with: `->`, `-->` or `<->`.
std::string arrow_of(const Relationship& relationship);

class Document {
 public:
  /// `file` is where the document lives (or would), for resolving its libraries.
  Document(std::string text, std::string file);

  [[nodiscard]] editor::Session& session() { return session_; }
  [[nodiscard]] const std::string& text() const { return session_.source(); }
  /// The person typed: the text view's whole text.
  void set_text(const std::string& text);

  [[nodiscard]] const std::vector<std::string>& views() const { return views_; }
  [[nodiscard]] const std::string& view() const { return view_; }
  void choose_view(const std::string& id) { view_ = id; }

  struct Refreshed {
    std::optional<editor::ViewRender> render;  ///< nothing when there is no view or layout failed
    std::string status;
    bool error = false;
  };
  /// Updates the views (keeping the current one if it still exists) and renders it.
  Refreshed refresh();

  // Gestures. Each returns the change to apply, nothing when the gesture changes nothing, or an
  // error message. The session already holds the new text.
  Result<std::optional<Change>> drop(const std::string& element, const Box& box, bool freeform);
  /// Several selected nodes dragged together by `offset`: one change that pins each where it lands.
  Result<std::optional<Change>> move(const std::vector<std::string>& elements, const Point& offset);
  /// A node resized on the canvas: `pin … size` in a fixed view, `size …` in the others.
  Result<std::optional<Change>> resize(const std::string& element, const Box& box);
  /// Sizes elements to fit their content in the current view (see editor::fit_operation).
  Result<std::optional<Change>> fit(const std::vector<std::string>& elements);
  /// A node turned on the canvas (`rotate … degrees`; 0 removes it).
  Result<std::optional<Change>> rotate(const std::string& element, double degrees);
  /// Bend points dragged on the canvas of a fixed view (`route "…" via …`).
  Result<std::optional<Change>> reroute(const std::string& relationship, const std::vector<Point>& points);
  Result<std::optional<Change>> connect(const std::string& source, const std::string& target);
  Result<std::optional<Change>> rename(const std::string& element, const std::string& name);
  Result<std::optional<Change>> relabel(const std::string& relationship, const std::optional<std::string>& label);
  Result<std::optional<Change>> remove(const editor::Selection& selection);
  /// Moves one end of a relationship onto `element`; `reconnected` is the relationship's id
  /// afterwards (an id derived from its endpoints changes with them).
  Result<std::optional<Change>> reconnect(const std::string& relationship, editor::End end, const std::string& element,
                                          std::string* reconnected = nullptr);
  /// Sets an element's properties from an inspector (see ElementFields). A new `local_id` renames
  /// it first (see rename_element); `renamed` is its id afterwards.
  Result<std::optional<Change>> update(const ElementFields& fields, std::string* renamed = nullptr);
  /// Renames an element: `to_typed` trimmed replaces the last part of its id, and every reference
  /// to it (and to its descendants) follows (see editor::rename_element). `renamed` is its id
  /// afterwards.
  Result<std::optional<Change>> rename_element(const std::string& element, const std::string& to_typed,
                                               std::string* renamed = nullptr);
  /// The element whose name is written at `offset` (UTF-8 bytes into the text), in its
  /// declaration or a reference to it, for a text view's Rename; nothing elsewhere.
  [[nodiscard]] std::optional<std::string> element_at(std::size_t offset) const;
  /// A caret for element_to_rename that names no element, for a shell with no text cursor at hand.
  static constexpr std::size_t kNoCaret = std::numeric_limits<std::size_t>::max();
  /// The element a Rename command acts on: the one named at `caret` (as element_at) when the text
  /// has the focus; else the one element `selection` holds; else (a menu takes the focus from the
  /// text) the one at `caret`.
  [[nodiscard]] std::optional<std::string> element_to_rename(const editor::Selection& selection, bool text_focused,
                                                             std::size_t caret) const;
  /// `selection` after element `from` was renamed to `to`: it and its descendants under their new
  /// ids, and only the relationships that still exist (an id derived from the ends changes).
  [[nodiscard]] editor::Selection follow_rename(editor::Selection selection, const std::string& from,
                                                const std::string& to) const;
  /// The fields of an element's inspector whose typed value update() would refuse, and why,
  /// without changing anything: `id`, `kind`, `shape` and `style` (see editor::element_problems)
  /// and `shared-styles`. Empty when the edit applies, or the element no longer exists.
  [[nodiscard]] std::vector<editor::FieldProblem> problems(const ElementFields& typed) const;
  /// The same for a relationship's: `name` and `style` (see editor::relationship_problems).
  [[nodiscard]] std::vector<editor::FieldProblem> problems(const RelationshipFields& typed) const;
  /// Applies an operation an inspector built (see host/styles.hpp).
  Result<std::optional<Change>> apply(const Operation& operation);
  /// The inspector fields of an element, or nothing when it does not exist.
  [[nodiscard]] std::optional<ElementFields> fields(const std::string& element) const;
  /// Sets a relationship's properties and name from an inspector (see RelationshipFields);
  /// `renamed` is its id afterwards, which a new name changes.
  Result<std::optional<Change>> update(const RelationshipFields& fields, std::string* renamed = nullptr);
  /// The inspector fields of a relationship, or nothing when it does not exist.
  [[nodiscard]] std::optional<RelationshipFields> relationship_fields(const std::string& relationship) const;
  /// What the inspector's pickers offer: kinds and shapes (the document's libraries' too), and
  /// the tags and technologies the model uses (see host/field_editors.hpp).
  [[nodiscard]] FieldChoices choices() const;
  // Views. Each moves the canvas to the view it leaves the person in (the new one, the renamed
  // one, or the first one after a removal).
  /// A name a new view could take (`view1`, `view2`, …), to offer when asking for one.
  [[nodiscard]] std::string fresh_view() const;
  /// Adds a view named `id_typed` trimmed (as typed into a dialog), showing what the current
  /// view's scope holds (`include *`), and shows it.
  Result<std::optional<Change>> add_view(const std::string& id_typed);
  /// Removes the current view from the text; an error for the only view.
  Result<std::optional<Change>> remove_view();
  /// Gives the current view the id `to_typed` trimmed.
  Result<std::optional<Change>> rename_view(const std::string& to_typed);

  /// What Clear Canvas leaves: an empty diagram.
  static constexpr std::string_view kEmptyText = "model {\n}\n";
  /// Clear Canvas: replaces the whole text with kEmptyText as one change, so one undo brings the
  /// diagram back; nothing when the text is that already. The canvas moves to the empty
  /// diagram's one view on the next refresh().
  Result<std::optional<Change>> clear();
  /// Format Document: reindents and respaces the text (dsl::format_source) with `indent_size`
  /// spaces per level, as one change covering what differs; nothing when the text is
  /// formatted already.
  Result<std::optional<Change>> format(int indent_size);
  /// What every native shell's Clear Canvas confirmation says, under the question "Clear the
  /// canvas?". The web app has the same words in src/apps/web/src/app.ts.
  static constexpr std::string_view kClearDetail =
      "Everything in this diagram is removed, including changes that are not saved. Undo brings it back.";

  /// Adds an element of `kind` to the current view's scope; `created` is its id.
  Result<std::optional<Change>> add(const std::string& kind, std::string* created = nullptr);

  [[nodiscard]] std::optional<LayoutHint> preview_drop(const std::string& element, const Box& box, bool freeform);
  [[nodiscard]] std::string name_of(const std::string& element) const;
  [[nodiscard]] std::string label_of(const std::string& relationship) const;
  /// Where an element's name is declared in the text (UTF-8 bytes), to reveal it on selection.
  [[nodiscard]] std::optional<dsl::Span> declaration_of(const std::string& element) const;

  /// Points a canvas's lookups (names, labels, drop preview) at this document. The shell still
  /// wires the callbacks that change the text, since it has to apply the change.
  void attach(canvas::Canvas& canvas);

  // Exports of the current view (SVG, PNG at 2×, PDF) or of every view (draw.io, BPMN pages).
  Result<std::string> svg();
  Result<std::string> png();
  Result<std::string> pdf();
  std::string drawio();
  std::string bpmn();

 private:
  Result<std::optional<Change>> changed(Result<dsl::SourceUpdate> update, const std::string& before);
  /// Applies an inspector's fields other than the id.
  Result<std::optional<Change>> update_fields(const ElementFields& fields);

  editor::Session session_;
  std::string file_;
  /// The kinds and shapes choices() last listed, by the `use` lines they were listed for.
  mutable std::optional<std::pair<std::string, FieldChoices>> library_choices_;
  /// The Id problem problems() last found, for the text, element and Id it was found for: the
  /// check is a dry run of the rename, and an inspector asks on every keystroke in any field.
  struct IdCheck {
    std::string source, element, local_id;
    std::optional<std::string> problem;
  };
  mutable std::optional<IdCheck> id_check_;
  std::vector<std::string> views_;
  std::string view_;
};

/// The kinds the toolbar's Add offers.
const std::vector<std::string>& element_kinds();

}  // namespace dd::host
