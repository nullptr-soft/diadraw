#include "style_panel.hpp"

namespace dd::gtk {

namespace {

using Fields = dd::host::DocumentFields;

enum class Kind { Theme, Blocks, Keys, Nodes };

struct FieldSpec {
  const char* key;
  const char* label;
  const char* placeholder;
  std::string Fields::*member;
  Kind kind;
  bool declared;  ///< in the declared theme's section
};

const FieldSpec kFields[] = {
    {"theme", "Theme", "default, dark, mono or a new name", &Fields::theme, Kind::Theme, false},
    {"view-theme", "This view's theme", "the document's", &Fields::view_theme, Kind::Theme, false},
    {"styles", "Style blocks", "", &Fields::styles, Kind::Blocks, false},
    {"extends", "Extends", "default", &Fields::extends, Kind::Theme, true},
    {"theme-keys", "Theme keys", "", &Fields::theme_keys, Kind::Keys, true},
    {"theme-nodes", "Node colours", "", &Fields::theme_nodes, Kind::Nodes, true},
};

Gtk::Label* caption(const char* text) {
  auto label = Gtk::make_managed<Gtk::Label>(text);
  label->set_xalign(0);
  label->set_wrap(true);
  label->add_css_class("caption");
  label->add_css_class("dim-label");
  return label;
}

}  // namespace

StylePanel::StylePanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 2) {
  set_margin_start(10);
  set_margin_end(10);
  set_margin_top(8);
  set_margin_bottom(8);
  set_size_request(240, -1);
  title_.set_text("Theme and styles");
  title_.set_xalign(0);
  title_.add_css_class("heading");
  title_.set_margin_bottom(6);
  append(title_);
  for (const auto& f : kFields) {
    if (f.declared && !theme_title_.get_parent()) {
      theme_title_.set_xalign(0);
      theme_title_.add_css_class("heading");
      theme_title_.set_margin_top(8);
      theme_title_.set_margin_bottom(6);
      append(theme_title_);
    }
    Row row{f.key, f.member};
    auto label = caption(f.label);
    append(*label);
    row.widgets.push_back(label);
    switch (f.kind) {
      case Kind::Theme: {
        auto choice = Gtk::make_managed<ChoiceEntry>();
        auto& entry = choice->entry();
        entry.set_placeholder_text(f.placeholder);
        entry.signal_activate().connect([this] { commit(); });
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this] { commit(); });
        entry.add_controller(focus);
        auto keys = Gtk::EventControllerKey::create();
        keys->signal_key_pressed().connect(
            [this, &entry, member = f.member](guint key, guint, Gdk::ModifierType) {
              if (key != GDK_KEY_Escape || !shown_) return false;
              entry.set_text((*shown_).*member);  // Escape puts the field back
              return true;
            },
            false);
        entry.add_controller(keys);
        row.control = choice;
        break;
      }
      case Kind::Blocks:
        row.control = Gtk::make_managed<StyleBlocks>();
        break;
      case Kind::Keys:
        row.control = Gtk::make_managed<ThemeKeys>();
        break;
      case Kind::Nodes:
        row.control = Gtk::make_managed<ThemeNodes>();
        break;
    }
    row.control->changed = [this] { commit(); };
    row.control->set_margin_bottom(8);
    append(*row.control);
    row.widgets.push_back(row.control);
    rows_.push_back(row);
  }
  set_visible(false);
}

std::string StylePanel::text_of(const Row& row) const { return row.control->text(); }

void StylePanel::set_text(Row& row, const std::string& text) { row.control->show_text(text, false); }

bool StylePanel::has_focus_in(const Row& row) const { return row.control->editing(); }

void StylePanel::set_choices(const dd::host::FieldChoices& choices) {
  std::vector<std::string> selectors = dd::host::selector_choices(choices);
  for (auto& row : rows_) {
    if (auto* themes = dynamic_cast<ChoiceEntry*>(row.control)) themes->set_choices(choices.themes);
    if (auto* blocks = dynamic_cast<StyleBlocks*>(row.control)) blocks->set_selectors(selectors);
    if (auto* nodes = dynamic_cast<ThemeNodes*>(row.control)) nodes->set_kinds(choices.kinds);
  }
}

void StylePanel::show_fields(const std::optional<Fields>& fields) {
  bool same_theme = fields && shown_ && fields->declared == shown_->declared;
  shown_ = fields;
  set_visible(fields.has_value());
  if (!fields) return;
  filling_ = true;
  bool declared = !fields->declared.empty();
  theme_title_.set_text("Theme " + fields->declared);
  theme_title_.set_visible(declared);
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    auto& row = rows_[i];
    if (kFields[i].declared)
      for (auto* w : row.widgets) w->set_visible(declared);
    // What is being typed stays, unless it belongs to another theme.
    bool keep = !kFields[i].declared || same_theme;
    if (dynamic_cast<ChoiceEntry*>(row.control) && keep && has_focus_in(row)) continue;
    row.control->show_text((*fields).*(row.member), keep);
  }
  filling_ = false;
}

void StylePanel::commit() {
  if (filling_ || !shown_ || !committed) return;
  Fields typed = *shown_;
  for (const auto& row : rows_) typed.*(row.member) = text_of(row);
  if (!(typed == *shown_)) committed(typed);
}

bool StylePanel::set_field(const std::string& key, const std::string& value) {
  if (!get_visible() || !shown_) return false;
  for (auto& row : rows_) {
    if (row.key != key) continue;
    if (!row.widgets.back()->get_visible()) return false;
    set_text(row, value);
    commit();
    return true;
  }
  return false;
}

}  // namespace dd::gtk
