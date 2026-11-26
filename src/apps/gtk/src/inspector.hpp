// The selected element's or relationship's properties as fields, beside the canvas. Committing a
// field (Enter, leaving it after a change, or a change in one of its controls) hands every field
// to `committed` (elements) or `committed_relationship`, which turn them into a text change
// through dd::host::Document::update.
//
// Kind, Shape and Tech are entries with a list to pick from, Tags are chips, Style is the visual
// style editor, Properties a key/value table (field_controls.hpp); each writes the field's text.
//
// While a field is typed in, `check` (or `check_relationship`) says which fields the edit would
// refuse; those get a red outline and the reason as their tooltip.

#pragma once

#include <gtkmm.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "field_controls.hpp"
#include "host/document.hpp"
#include "host/field_editors.hpp"

namespace dd::gtk {

class Inspector : public Gtk::Box {
 public:
  Inspector();

  /// Called with the fields as typed when one of them is committed.
  std::function<void(const dd::host::ElementFields&)> committed;
  std::function<void(const dd::host::RelationshipFields&)> committed_relationship;
  /// The fields the edit as typed would refuse, and why (dd::host::Document::problems).
  std::function<std::vector<dd::editor::FieldProblem>(const dd::host::ElementFields&)> check;
  std::function<std::vector<dd::editor::FieldProblem>(const dd::host::RelationshipFields&)> check_relationship;

  /// Shows an element's fields; nothing hides the inspector. A field being typed in keeps its
  /// text when the same element is shown again.
  void show_fields(const std::optional<dd::host::ElementFields>& fields);
  /// The same for a relationship.
  void show_fields(const std::optional<dd::host::RelationshipFields>& fields);
  /// What Kind, Shape, Tech and Tags offer (Document::choices).
  void set_choices(const dd::host::FieldChoices& choices);
  /// For scripted tests: types `value` into the field `key` of the panel shown (an element's id,
  /// kind, name, technology, shape, tags, description, style, props, shared-styles; a relationship's
  /// name, label, technology, arrow, tags, style, props) and commits it; `style.<key>` sets one
  /// key in the style editor as its control would. False when hidden or the key is unknown.
  bool set_field(const std::string& key, const std::string& value);
  /// For scripted tests (`enter`): types `value` into the text field `key` as set_field does, without
  /// committing it. False when hidden, the key is unknown or the field is not text.
  bool type_field(const std::string& key, const std::string& value);
  /// The fields outlined as invalid, with why, in the order shown.
  [[nodiscard]] const std::vector<dd::editor::FieldProblem>& invalid_fields() const { return invalid_; }

 private:
  /// One field: a single-line entry, a multi-line view, a choice of arrows or a control.
  struct Field {
    std::string key;
    Gtk::Entry* entry = nullptr;
    Gtk::TextView* view = nullptr;
    Gtk::DropDown* choice = nullptr;
    std::vector<std::string> choices;
    /// A control of field_controls.hpp; for a ChoiceEntry, `entry` is its entry.
    FieldControl* control = nullptr;
    /// Which list a ChoiceEntry offers: kinds, shapes or technologies.
    std::string list;
  };
  /// The fields of one kind of item, in a box of their own.
  template <class Fields>
  struct Panel {
    Gtk::Box* box = nullptr;
    std::vector<std::pair<Field, std::string Fields::*>> fields;
    std::optional<Fields> shown;
  };
  struct Spec;

  template <class Fields>
  void build(Panel<Fields>& panel, const std::vector<std::pair<Spec, std::string Fields::*>>& specs);
  template <class Fields>
  void show(Panel<Fields>& panel, const std::optional<Fields>& fields, const std::string& title);
  template <class Fields>
  void commit(Panel<Fields>& panel, const std::function<void(const Fields&)>& to);
  template <class Fields>
  bool set(Panel<Fields>& panel, const std::string& key, const std::string& value);
  template <class Fields>
  void check_panel(Panel<Fields>& panel,
                   const std::function<std::vector<dd::editor::FieldProblem>(const Fields&)>& with);
  /// Outlines the fields of the shown panel the edit as typed would refuse.
  void check_shown();
  /// The widget a field's outline goes on.
  [[nodiscard]] static Gtk::Widget* outlined(const Field& field);

  [[nodiscard]] std::string text_of(const Field& field) const;
  void set_text(Field& field, const std::string& text);
  [[nodiscard]] bool has_focus_in(const Field& field) const;
  void commit_shown();
  void revert(const Field& field);

  Gtk::Label title_;
  Panel<dd::host::ElementFields> element_;
  Panel<dd::host::RelationshipFields> relationship_;
  bool filling_ = false;
  std::vector<dd::editor::FieldProblem> invalid_;
};

}  // namespace dd::gtk
