// The inspector's controls for fields that are more than a line of text (see dd::host's
// field_editors.hpp for the rules they share with the macOS and web apps). Each shows one text
// field and writes the same text back, so the inspector reads and commits it as before:
//
// - ChoiceEntry: an entry with a button that lists choices (Kind, Shape, Tech); typing still works.
// - TagsField: tags as chips with a remove button, and an entry that adds one on Enter or a comma
//   or picks one of the document's tags.
// - PropertiesTable: `key=value` lines as a table of two entries per row, with + and −.
// - ColorField: a colour button with a button that clears it; `#rrggbb` or empty for the default.
//   The colour button opens the preset swatches (host/swatches.hpp), Custom… and Default.
// - StyleEditor: an inline style as a control per key the renderer knows, and an Other entry.
//
// And for the Theme and styles panel:
// - StyleBlocks: the Style blocks field (`selector: k=v;…` per line) as a list of blocks, each a
//   selector entry and, when opened, the style editor.
// - ThemeKeys: a declared theme's keys (`key=value` per line), a colour button per colour key and
//   an entry for the font keys; keys without a control are kept.
// - ThemeNodes: its node colours (`kind: fill=…;stroke=…;text=…` per line), a row per kind with
//   three colour buttons.

#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "host/field_editors.hpp"

namespace dd::gtk {

/// What the inspector needs of a control: its text, showing a text, and hearing a change.
class FieldControl : public Gtk::Box {
 public:
  explicit FieldControl(Gtk::Orientation orientation = Gtk::Orientation::VERTICAL, int spacing = 2)
      : Gtk::Box(orientation, spacing) {}
  /// Called after the person changed the text (not after show_text).
  std::function<void()> changed;
  [[nodiscard]] virtual std::string text() const = 0;
  /// Shows a text; a part being typed in keeps its text when `keep_typing`.
  virtual void show_text(const std::string& text, bool keep_typing) = 0;
  /// Whether the keyboard focus is inside the control.
  [[nodiscard]] bool editing() const;

 protected:
  void notify() const {
    if (changed) changed();
  }
};

/// An entry and a button that lists choices; picking one sets the entry and reports a change.
class ChoiceEntry : public FieldControl {
 public:
  ChoiceEntry();
  void set_choices(const std::vector<dd::host::Choice>& choices);
  [[nodiscard]] std::string text() const override { return entry_.get_text(); }
  void show_text(const std::string& text, bool keep_typing) override;
  Gtk::Entry& entry() { return entry_; }

 private:
  /// Before the list, whose header function reads it while the list is torn down.
  std::vector<dd::host::Choice> choices_;
  Gtk::Entry entry_;
  Gtk::MenuButton button_;
  Gtk::Popover popover_;
  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
};

class TagsField : public FieldControl {
 public:
  TagsField();
  void set_suggestions(const std::vector<std::string>& tags);
  [[nodiscard]] std::string text() const override { return text_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  void add_typed();
  void set(const std::vector<std::string>& tags);
  void rebuild();

  std::string text_;
  Gtk::FlowBox chips_;
  /// Where a tag is typed, or picked from the document's tags.
  ChoiceEntry entry_;
};

class PropertiesTable : public FieldControl {
 public:
  PropertiesTable();
  [[nodiscard]] std::string text() const override { return text_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  struct Row {
    Gtk::Box* box;
    Gtk::Entry* key;
    Gtk::Entry* value;
  };
  void add_row(const std::string& key, const std::string& value);
  void commit_rows();

  std::string text_;
  Gtk::Box rows_box_{Gtk::Orientation::VERTICAL, 2};
  std::vector<Row> rows_;
};

class ColorField : public FieldControl {
 public:
  explicit ColorField(bool compact = false);
  ~ColorField() override;
  ColorField(const ColorField&) = delete;
  ColorField& operator=(const ColorField&) = delete;
  [[nodiscard]] std::string text() const override { return value_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  void show();
  /// Sets the value as the person picked it and reports the change.
  void pick(const std::string& value);
  /// Builds the swatches' popover, the first time it opens.
  void build_popover();
  /// Opens the colour dialog (Custom…) with the current colour.
  void choose_custom();

  std::string value_;
  Gtk::MenuButton button_;  ///< shows the colour; opens the swatches
  Gtk::DrawingArea well_;
  Gtk::Popover* popover_ = nullptr;
  std::vector<Gtk::DrawingArea*> swatches_;  ///< redrawn to mark the current colour (once built)
  Gtk::Button* default_ = nullptr;
  Gtk::Button clear_;
  Gtk::Label hex_;
  /// Cancels an open colour dialog when the field goes away; its reply is then dropped.
  Glib::RefPtr<Gio::Cancellable> choosing_;
};

class StyleEditor : public FieldControl {
 public:
  explicit StyleEditor(dd::StyleTarget target);
  [[nodiscard]] std::string text() const override { return style_; }
  void show_text(const std::string& text, bool keep_typing) override;
  /// For scripted tests: sets one key as its control would (`fill`, `lineStyle`, `fontStyle`, …;
  /// `other` for the Other entry); an empty value puts the control back to its default. False
  /// for a key without a control.
  bool set_key(const std::string& key, const std::string& value);

 private:
  struct Control {
    const dd::host::StyleKey* key;
    ColorField* color = nullptr;
    Gtk::Entry* entry = nullptr;
    Gtk::CheckButton* check = nullptr;
    Gtk::DropDown* choice = nullptr;
    std::vector<Gtk::ToggleButton*> toggles;
  };
  void set(const dd::host::StyleKey& key, const std::string& value);
  void apply(std::string style);
  void show();

  dd::StyleTarget target_;
  std::string style_;
  std::vector<Control> controls_;
  Gtk::Entry other_;
  Gtk::Button clear_{"Clear style"};
  bool filling_ = false;
};

class StyleBlocks : public FieldControl {
 public:
  StyleBlocks();
  /// What the selector lists offer (dd::host::selector_choices).
  void set_selectors(const std::vector<std::string>& selectors);
  [[nodiscard]] std::string text() const override { return text_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  struct Block {
    ChoiceEntry* selector;
    Gtk::Label* summary;
    StyleEditor* editor = nullptr;
  };
  void rebuild();
  void refresh();
  void commit_rows();
  void selector_typed(std::size_t i);

  std::string text_;
  std::vector<dd::host::BlockRow> rows_;
  std::vector<bool> open_;
  std::vector<dd::host::Choice> selectors_;
  Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};
  std::vector<Block> blocks_;
};

class ThemeKeys : public FieldControl {
 public:
  ThemeKeys();
  [[nodiscard]] std::string text() const override { return text_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  void set(const std::string& key, const std::string& value);

  std::string text_;
  /// Per key of dd::theme_keys(): a ColorField or an entry.
  std::vector<std::pair<ColorField*, Gtk::Entry*>> controls_;
};

class ThemeNodes : public FieldControl {
 public:
  ThemeNodes();
  void set_kinds(const std::vector<dd::host::Choice>& kinds) { add_.set_choices(kinds); }
  [[nodiscard]] std::string text() const override { return text_; }
  void show_text(const std::string& text, bool keep_typing) override;

 private:
  void rebuild();
  void add_kind();

  std::string text_;
  std::vector<dd::host::BlockRow> rows_;
  Gtk::Box list_{Gtk::Orientation::VERTICAL, 4};
  /// Per row, its fill, line and text buttons.
  std::vector<std::vector<ColorField*>> fields_;
  ChoiceEntry add_;
};

}  // namespace dd::gtk
