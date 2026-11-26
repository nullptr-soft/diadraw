#pragma once

// Typed operations, a port of packages/core/src/operations.ts.
//
// Every change to a model, from the text editor or the canvas, is one of these operations.
// Applying one validates it and returns the inverse that undoes it.

#include <map>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/model/model.hpp"

namespace dd {

struct Operation;

namespace op {

struct AddElement {
  Element element;
  bool operator==(const AddElement&) const = default;
};
struct RemoveElement {
  ElementId id;
  bool operator==(const RemoveElement&) const = default;
};
struct RenameElement {
  ElementId id;
  std::string name;
  bool operator==(const RenameElement&) const = default;
};
/// Gives element `id` the id `to`, keeping its parent (only the part after the last dot changes)
/// and its place in the model's order. Its descendants' ids follow (`shop.web` → `store.web` when
/// `shop` becomes `store`), and so does every reference: parents, relationship ends, view scopes,
/// selectors, hints, sequence steps and decorations. `relationships` and `groups` change those
/// ids at the same time, old id → new, as ids derived from element ids do (`a->b`, `shop/Edge`);
/// `route`s and memberships follow them. `name`, when set, is the element's name afterwards: one
/// the text does not write shows its id, so it changes with it.
struct ChangeElementId {
  ElementId id;
  ElementId to;
  std::map<RelationshipId, RelationshipId> relationships;
  std::map<GroupId, GroupId> groups;
  std::optional<std::string> name;
  bool operator==(const ChangeElementId&) const = default;
};
/// Sets what an element says about itself, as a canvas inspector edits it: kind, name, `tech`,
/// `shape`, `tag`s, `description`, `style` and `prop`s, all taken from `element`. Its id, parent,
/// group and ports are where it sits in the model and must match the existing element's.
struct UpdateElement {
  Element element;
  bool operator==(const UpdateElement&) const = default;
};
struct AddRelationship {
  Relationship relationship;
  bool operator==(const AddRelationship&) const = default;
};
struct RemoveRelationship {
  RelationshipId id;
  bool operator==(const RemoveRelationship&) const = default;
};
/// Moves a relationship's ends: `source` and `target` replace its endpoints (either may stay as it
/// was). `renames` changes relationship ids at the same time, old id → new, all at once, and every
/// `route` that names them follows; the text uses it for ids derived from endpoints (`a->b`),
/// which change when an endpoint moves. The relationship's own id may be among them.
struct ReconnectRelationship {
  RelationshipId id;
  Endpoint source;
  Endpoint target;
  std::map<RelationshipId, RelationshipId> renames;
  bool operator==(const ReconnectRelationship&) const = default;
};
struct SetRelationshipLabel {
  RelationshipId id;
  std::optional<std::string> label;
  bool operator==(const SetRelationshipLabel&) const = default;
};
/// Sets what a relationship says about itself, as a canvas inspector edits it: label, `tech`,
/// arrow (line and direction), `tag`s, `style` and `prop`s, all taken from `relationship`. Its id
/// and endpoints must match the existing relationship's.
struct UpdateRelationship {
  Relationship relationship;
  bool operator==(const UpdateRelationship&) const = default;
};
/// Gives relationships new ids, all at once (so `a`→`b` and `b`→`c` can go together), and moves
/// the views' routes along with them. Each relationship keeps its place in the model's order.
struct RenameRelationships {
  struct Rename {
    RelationshipId from;
    RelationshipId to;
    bool operator==(const Rename&) const = default;
  };
  std::vector<Rename> renames;
  bool operator==(const RenameRelationships&) const = default;
};
struct AddGroup {
  Group group;
  bool operator==(const AddGroup&) const = default;
};
struct RemoveGroup {
  GroupId id;
  bool operator==(const RemoveGroup&) const = default;
};
struct AddView {
  View view;
  bool operator==(const AddView&) const = default;
};
struct RemoveView {
  ViewId id;
  bool operator==(const RemoveView&) const = default;
};
/// Gives view `id` the id `to`; it keeps its place among the views and everything it holds.
struct RenameView {
  ViewId id;
  ViewId to;
  bool operator==(const RenameView&) const = default;
};
/// Replaces a view wholesale; mostly the inverse of edits that touch several parts of it.
struct ReplaceView {
  View view;
  bool operator==(const ReplaceView&) const = default;
};
struct SetLayout {
  ViewId view;
  LayoutOptions layout;
  bool operator==(const SetLayout&) const = default;
};
struct AddHint {
  ViewId view;
  LayoutHint hint;
  /// Position in the view's hint list; appends when absent.
  std::optional<std::size_t> index;
  bool operator==(const AddHint&) const = default;
};
struct RemoveHint {
  ViewId view;
  std::size_t index = 0;
  bool operator==(const RemoveHint&) const = default;
};
/// Rewrites one hint where it stands (a canvas resize or a dragged bend point).
struct ReplaceHint {
  ViewId view;
  std::size_t index = 0;
  LayoutHint hint;
  bool operator==(const ReplaceHint&) const = default;
};
/// Adds a `use` of a library; the alias must be free.
struct UseLibrary {
  LibraryUse use;
  std::optional<std::size_t> index;
  bool operator==(const UseLibrary&) const = default;
};
struct UnuseLibrary {
  std::string alias;
  bool operator==(const UnuseLibrary&) const = default;
};
/// Adds a `style` block at `index` in `model.styles`; appends when absent.
struct AddStyleRule {
  StyleRule rule;
  std::optional<std::size_t> index;
  bool operator==(const AddStyleRule&) const = default;
};
struct RemoveStyleRule {
  std::size_t index = 0;
  bool operator==(const RemoveStyleRule&) const = default;
};
/// Replaces the `style` block at `index`: its selector and its keys.
struct UpdateStyleRule {
  std::size_t index = 0;
  StyleRule rule;
  bool operator==(const UpdateStyleRule&) const = default;
};
/// `theme name` for the document, or for one view when `view` is set; no `theme` removes the
/// statement, so the view falls back to the document's theme and the document to `default`.
struct SetTheme {
  std::optional<ViewId> view;
  std::optional<std::string> theme;
  bool operator==(const SetTheme&) const = default;
};
/// Declares a theme (`theme brand extends dark { … }`) at `index` in `model.themes`; appends when
/// absent. The name must be free and not a built-in theme's.
struct AddTheme {
  ThemeDef theme;
  std::optional<std::size_t> index;
  bool operator==(const AddTheme&) const = default;
};
struct RemoveTheme {
  std::string name;
  bool operator==(const RemoveTheme&) const = default;
};
/// Replaces a declared theme's `extends`, keys and node colours; its name stays.
struct UpdateTheme {
  ThemeDef theme;
  bool operator==(const UpdateTheme&) const = default;
};
/// Applied atomically: all steps or none.
struct Batch {
  std::vector<Operation> operations;
  bool operator==(const Batch&) const;
};

}  // namespace op

struct Operation
    : std::variant<op::AddElement, op::RemoveElement, op::RenameElement, op::ChangeElementId, op::UpdateElement,
                   op::AddRelationship, op::RemoveRelationship, op::ReconnectRelationship, op::SetRelationshipLabel,
                   op::UpdateRelationship, op::RenameRelationships, op::AddGroup, op::RemoveGroup,
                   op::AddView, op::RemoveView, op::RenameView, op::ReplaceView, op::SetLayout, op::AddHint,
                   op::RemoveHint, op::ReplaceHint, op::UseLibrary, op::UnuseLibrary, op::AddStyleRule,
                   op::RemoveStyleRule, op::UpdateStyleRule, op::SetTheme, op::AddTheme,
                   op::RemoveTheme, op::UpdateTheme, op::Batch> {
  using variant::variant;
  bool operator==(const Operation& o) const {
    return static_cast<const variant&>(*this) == static_cast<const variant&>(o);
  }
};

inline bool op::Batch::operator==(const Batch& o) const { return operations == o.operations; }

struct ApplyResult {
  Model model;
  Operation inverse;
};

/// `name` or `alias.name`, each `[A-Za-z_][A-Za-z0-9_]*`: how kinds and shapes are written.
bool is_qualified_name(std::string_view s);

/// Why a style cannot be written inline (`style "k=v;k"`), as updating an element or a
/// relationship requires (a key empty or holding `=` or `;`, a value holding `;`), or nothing.
std::optional<std::string> inline_style_problem(const Style& style);

/// Why a `style` block cannot be added as it is (a relationship block naming a kind, a kind or a
/// key that is not a name, an empty tag), or nothing; adding or updating one checks this.
std::optional<Error> style_rule_problem(const StyleRule& rule);

/// Applies an operation to a copy of the model. The input is never changed.
Result<ApplyResult> apply_operation(const Model& model, const Operation& operation);

/// Applies an operation in place and returns its inverse. On error the model is unchanged.
Result<Operation> apply_in_place(Model& model, const Operation& operation);

}  // namespace dd
