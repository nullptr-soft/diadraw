// The document's theme and `style` blocks as fields (see dd::host::DocumentFields), in place of
// the inspector while nothing is selected and the Styles button is on. Committing a field (Enter,
// leaving it after a change, or a change in one of its controls) hands every field to
// `committed`. The themes are entries that list the themes there are; the style blocks, a
// declared theme's keys and its node colours are edited visually (field_controls.hpp).

#pragma once

#include <gtkmm.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "field_controls.hpp"
#include "host/field_editors.hpp"
#include "host/styles.hpp"

namespace dd::gtk {

class StylePanel : public Gtk::Box {
 public:
  StylePanel();

  /// Called with the fields as typed when one of them is committed.
  std::function<void(const dd::host::DocumentFields&)> committed;

  /// What the theme lists, the style blocks' selector lists and the node colours' kinds offer.
  void set_choices(const dd::host::FieldChoices& choices);
  /// Shows the fields; nothing hides the panel. A field being typed in keeps its text.
  void show_fields(const std::optional<dd::host::DocumentFields>& fields);
  /// For scripted tests: sets the field `key` (theme, view-theme, styles, extends, theme-keys,
  /// theme-nodes) to the whole text `value` and commits it. False when hidden or the key is unknown.
  bool set_field(const std::string& key, const std::string& value);

 private:
  struct Row {
    std::string key;
    std::string dd::host::DocumentFields::*member;
    /// A ChoiceEntry of themes, StyleBlocks, ThemeKeys or ThemeNodes.
    FieldControl* control = nullptr;
    /// The label and the field, hidden with the declared theme's section.
    std::vector<Gtk::Widget*> widgets;
  };
  [[nodiscard]] std::string text_of(const Row& row) const;
  void set_text(Row& row, const std::string& text);
  [[nodiscard]] bool has_focus_in(const Row& row) const;
  void commit();

  Gtk::Label title_;
  Gtk::Label theme_title_;
  std::vector<Row> rows_;
  std::optional<dd::host::DocumentFields> shown_;
  bool filling_ = false;
};

}  // namespace dd::gtk
