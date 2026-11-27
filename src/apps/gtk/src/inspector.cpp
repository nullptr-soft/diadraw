#include "inspector.hpp"

#include <algorithm>

namespace dd::gtk {

using dd::host::ElementFields;
using dd::host::RelationshipFields;

struct Inspector::Spec {
  const char* key;
  const char* label;
  const char* placeholder;
  bool multiline = false;
  std::vector<std::string> choices = {};
  /// A control instead of an entry: `kinds`, `shapes` or `technologies` (a ChoiceEntry), `tags`,
  /// `props`, or `style`/`relationship-style`.
  const char* control = nullptr;
};

namespace {

const std::vector<std::string> kArrows{"->", "-->", "<->"};
/// What the arrow picker shows: the arrow and what it draws (a font may join `->` into one glyph).
const std::vector<std::string> kArrowLabels{"->   solid", "-->  dashed", "<->  both ends"};

/// The outline of a field the edit would refuse; the style sheet goes to the display once.
void install_invalid_style() {
  [[maybe_unused]] static const bool installed = [] {
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
      .dd-invalid, .dd-invalid:focus, .dd-invalid:focus-within {
        outline: 2px solid @error_color;
        outline-offset: -2px;
      }
    )");
    Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), css,
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return true;
  }();
}

}  // namespace

Inspector::Inspector() : Gtk::Box(Gtk::Orientation::VERTICAL, 2) {
  set_margin_start(10);
  set_margin_end(10);
  set_margin_top(8);
  set_margin_bottom(8);
  set_size_request(240, -1);
  title_.set_xalign(0);
  title_.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
  title_.add_css_class("dim-label");
  title_.add_css_class("monospace");
  title_.set_margin_bottom(6);
  append(title_);
  install_invalid_style();
  build<ElementFields>(element_, {
                                     {{"id", "Id", ""}, &ElementFields::local_id},
                                     {{"kind", "Kind", "container", false, {}, "kinds"}, &ElementFields::kind},
                                     {{"name", "Name", ""}, &ElementFields::name},
                                     {{"technology", "Tech", "", false, {}, "technologies"},
                                      &ElementFields::technology},
                                     {{"shape", "Shape", "from the kind", false, {}, "shapes"}, &ElementFields::shape},
                                     {{"tags", "Tags", "", false, {}, "tags"}, &ElementFields::tags},
                                     {{"description", "Description", "", true}, &ElementFields::description},
                                     {{"style", "Style", "", false, {}, "style"}, &ElementFields::style},
                                     {{"props", "Properties", "", false, {}, "props"}, &ElementFields::props},
                                     {{"shared-styles", "Shared styles (selector: key=value;… per line)", "", true},
                                      &ElementFields::shared_styles},
                                 });
  build<RelationshipFields>(
      relationship_, {
                         {{"label", "Label", ""}, &RelationshipFields::label},
                         {{"technology", "Tech", "", false, {}, "technologies"}, &RelationshipFields::technology},
                         {{"arrow", "Arrow", "", false, kArrows}, &RelationshipFields::arrow},
                         {{"name", "Name (optional id)", "derived from the ends"}, &RelationshipFields::name},
                         {{"tags", "Tags", "", false, {}, "tags"}, &RelationshipFields::tags},
                         {{"style", "Style", "", false, {}, "relationship-style"}, &RelationshipFields::style},
                         {{"props", "Properties", "", false, {}, "props"}, &RelationshipFields::props},
                     });
  set_visible(false);
}

template <class Fields>
void Inspector::build(Panel<Fields>& panel, const std::vector<std::pair<Spec, std::string Fields::*>>& specs) {
  panel.box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  append(*panel.box);
  for (const auto& [spec, member] : specs) {
    auto label = Gtk::make_managed<Gtk::Label>(spec.label);
    label->set_xalign(0);
    label->add_css_class("caption");
    label->add_css_class("dim-label");
    panel.box->append(*label);
    Field field{spec.key};
    std::string control = spec.control ? spec.control : "";
    if (control == "kinds" || control == "shapes" || control == "technologies") {
      auto choice = Gtk::make_managed<ChoiceEntry>();
      field.control = choice;
      field.list = control;
      field.entry = &choice->entry();
      field.entry->set_placeholder_text(spec.placeholder);
      choice->changed = [this] { commit_shown(); };  // picked from the list
      choice->set_margin_bottom(6);
      panel.box->append(*choice);
    } else if (!control.empty()) {
      if (control == "tags")
        field.control = Gtk::make_managed<TagsField>();
      else if (control == "props")
        field.control = Gtk::make_managed<PropertiesTable>();
      else
        field.control = Gtk::make_managed<StyleEditor>(control == "style" ? dd::StyleTarget::Elements
                                                                          : dd::StyleTarget::Relationships);
      field.control->changed = [this] { commit_shown(); };
      field.control->set_margin_bottom(6);
      panel.box->append(*field.control);
      panel.fields.emplace_back(std::move(field), member);
      continue;
    } else if (!spec.choices.empty()) {
      field.choices = spec.choices;
      const auto& shown = spec.choices == kArrows ? kArrowLabels : spec.choices;
      std::vector<Glib::ustring> names(shown.begin(), shown.end());
      field.choice = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(names));
      field.choice->set_margin_bottom(6);
      // Picking an arrow applies it at once.
      field.choice->property_selected().signal_changed().connect([this] { commit_shown(); });
      panel.box->append(*field.choice);
    } else if (spec.multiline) {
      field.view = Gtk::make_managed<Gtk::TextView>();
      field.view->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
      field.view->set_size_request(-1, 54);
      field.view->add_css_class("view");
      field.view->set_left_margin(6);
      field.view->set_right_margin(6);
      field.view->set_top_margin(4);
      field.view->set_bottom_margin(4);
      auto focus = Gtk::EventControllerFocus::create();
      focus->signal_leave().connect([this] { commit_shown(); });
      field.view->add_controller(focus);
      field.view->get_buffer()->signal_changed().connect([this] {
        if (!filling_) check_shown();
      });
      auto frame = Gtk::make_managed<Gtk::Frame>();
      frame->set_child(*field.view);
      frame->set_margin_bottom(6);
      panel.box->append(*frame);
    } else {
      field.entry = Gtk::make_managed<Gtk::Entry>();
      field.entry->set_placeholder_text(spec.placeholder);
      field.entry->set_margin_bottom(6);
      panel.box->append(*field.entry);
    }
    if (field.entry) {
      field.entry->signal_changed().connect([this] {
        if (!filling_) check_shown();
      });
      field.entry->signal_activate().connect([this] { commit_shown(); });
      auto focus = Gtk::EventControllerFocus::create();
      focus->signal_leave().connect([this] { commit_shown(); });
      field.entry->add_controller(focus);
      auto keys = Gtk::EventControllerKey::create();
      keys->signal_key_pressed().connect(
          [this, f = field](guint key, guint, Gdk::ModifierType) {
            if (key != GDK_KEY_Escape) return false;
            revert(f);  // Escape puts the field back
            return true;
          },
          false);
      field.entry->add_controller(keys);
    }
    panel.fields.emplace_back(std::move(field), member);
  }
  panel.box->set_visible(false);
}

std::string Inspector::text_of(const Field& field) const {
  if (field.control) return field.control->text();
  if (field.entry) return field.entry->get_text();
  if (field.view) return field.view->get_buffer()->get_text(true);
  auto at = field.choice->get_selected();
  return at < field.choices.size() ? field.choices[at] : "";
}

void Inspector::set_text(Field& field, const std::string& text) {
  if (field.control) {
    field.control->show_text(text, false);
  } else if (field.entry) {
    field.entry->set_text(text);
  } else if (field.view) {
    field.view->get_buffer()->set_text(text);
  } else {
    auto it = std::find(field.choices.begin(), field.choices.end(), text);
    field.choice->set_selected(it == field.choices.end() ? GTK_INVALID_LIST_POSITION
                                                          : static_cast<guint>(it - field.choices.begin()));
  }
}

bool Inspector::has_focus_in(const Field& field) const {
  if (field.control) return field.control->editing();
  Gtk::Widget* w = field.entry ? static_cast<Gtk::Widget*>(field.entry)
                   : field.view ? static_cast<Gtk::Widget*>(field.view)
                                : static_cast<Gtk::Widget*>(field.choice);
  const auto* root = get_root();
  const Gtk::Widget* focus = root ? root->get_focus() : nullptr;
  // An entry's focus sits on the text widget inside it.
  return focus && (focus == w || focus->is_ancestor(*w));
}

template <class Fields>
void Inspector::show(Panel<Fields>& panel, const std::optional<Fields>& fields, const std::string& title) {
  bool same = fields && panel.shown && fields->id == panel.shown->id;
  panel.shown = fields;
  panel.box->set_visible(fields.has_value());
  if (!fields) {
    // A hidden panel keeps no outlines; the panel shown next checks its own.
    invalid_.clear();
    check_panel<Fields>(panel, {});
    return;
  }
  filling_ = true;
  title_.set_text(title);
  for (auto& [field, member] : panel.fields) {
    if (same && has_focus_in(field)) {
      // A control keeps what is being typed, and shows the rest.
      if (field.control && !field.entry) field.control->show_text((*fields).*member, true);
      continue;
    }
    set_text(field, (*fields).*member);
  }
  filling_ = false;
  check_shown();
}

void Inspector::set_choices(const dd::host::FieldChoices& choices) {
  std::vector<dd::host::Choice> technologies;
  for (const auto& t : choices.technologies) technologies.push_back({t, t, ""});
  auto fill = [&](auto& panel) {
    for (auto& [field, member] : panel.fields) {
      if (auto* tags = dynamic_cast<TagsField*>(field.control)) tags->set_suggestions(choices.tags);
      auto* choice = dynamic_cast<ChoiceEntry*>(field.control);
      if (!choice) continue;
      choice->set_choices(field.list == "kinds" ? choices.kinds : field.list == "shapes" ? choices.shapes : technologies);
    }
  };
  fill(element_);
  fill(relationship_);
}

void Inspector::show_fields(const std::optional<ElementFields>& fields) {
  // One panel at a time: showing an element or nothing hides a relationship, and the other way round.
  show<RelationshipFields>(relationship_, std::nullopt, "");
  show(element_, fields, fields ? fields->id : "");
  set_visible(fields.has_value());
}

void Inspector::show_fields(const std::optional<RelationshipFields>& fields) {
  show<ElementFields>(element_, std::nullopt, "");
  std::string title;
  if (fields) {
    title = fields->source + " " + fields->arrow + " " + fields->target;
    if (!fields->name.empty()) title = fields->name + " = " + title;
  }
  show(relationship_, fields, title);
  // Without a name, the field shows the id derived from the ends, greyed.
  for (auto& [field, member] : relationship_.fields)
    if (field.key == "name" && fields) field.entry->set_placeholder_text(fields->name.empty() ? fields->id : "");
  set_visible(fields.has_value());
}

template <class Fields>
void Inspector::commit(Panel<Fields>& panel, const std::function<void(const Fields&)>& to) {
  if (filling_ || !panel.shown || !to) return;
  Fields typed = *panel.shown;
  bool changed = false;
  for (const auto& [field, member] : panel.fields) {
    typed.*member = text_of(field);
    changed = changed || typed.*member != (*panel.shown).*member;
  }
  if (changed) to(typed);
}

void Inspector::commit_shown() {
  if (element_.shown) commit(element_, committed);
  if (relationship_.shown) commit(relationship_, committed_relationship);
  // A refused commit keeps the typed text: it stays outlined.
  check_shown();
}

Gtk::Widget* Inspector::outlined(const Field& field) {
  if (field.control) return field.control;
  if (field.entry) return field.entry;
  if (field.view) return field.view->get_parent();  // the frame around it
  return field.choice;
}

template <class Fields>
void Inspector::check_panel(Panel<Fields>& panel,
                            const std::function<std::vector<dd::editor::FieldProblem>(const Fields&)>& with) {
  std::vector<dd::editor::FieldProblem> problems;
  if (panel.shown && with) {
    Fields typed = *panel.shown;
    for (const auto& [field, member] : panel.fields) typed.*member = text_of(field);
    problems = with(typed);
  }
  for (auto& [field, member] : panel.fields) {
    auto it = std::ranges::find(problems, field.key, &dd::editor::FieldProblem::field);
    Gtk::Widget* widget = outlined(field);
    // The theme's `error` colours an entry's text; on a box of controls it would colour every label.
    bool entry = widget == field.entry;
    if (it == problems.end()) {
      if (!widget->has_css_class("dd-invalid")) continue;
      widget->remove_css_class("dd-invalid");
      if (entry) widget->remove_css_class("error");
      widget->set_tooltip_text("");
    } else {
      widget->add_css_class("dd-invalid");
      if (entry) widget->add_css_class("error");
      widget->set_tooltip_text(it->message);
      invalid_.push_back(*it);
    }
  }
}

void Inspector::check_shown() {
  invalid_.clear();
  check_panel(element_, check);
  check_panel(relationship_, check_relationship);
}

void Inspector::revert(const Field& field) {
  auto back = [&](auto& panel) {
    if (!panel.shown) return;
    for (auto& [f, member] : panel.fields)
      if (f.entry == field.entry) set_text(f, (*panel.shown).*member);
  };
  back(element_);
  back(relationship_);
}

template <class Fields>
bool Inspector::set(Panel<Fields>& panel, const std::string& key, const std::string& value) {
  if (key.rfind("style.", 0) == 0) {
    for (auto& [field, member] : panel.fields)
      if (auto* editor = dynamic_cast<StyleEditor*>(field.control)) return editor->set_key(key.substr(6), value);
    return false;
  }
  for (auto& [field, member] : panel.fields) {
    if (field.key != key) continue;
    if (field.choice && std::find(field.choices.begin(), field.choices.end(), value) == field.choices.end())
      return false;
    filling_ = true;  // a choice commits when it changes; commit once, below
    set_text(field, value);
    filling_ = false;
    commit_shown();
    return true;
  }
  return false;
}

bool Inspector::type_field(const std::string& key, const std::string& value) {
  if (!get_visible()) return false;
  auto type = [&](auto& panel) {
    for (auto& [field, member] : panel.fields) {
      if (field.key != key || !(field.entry || field.view)) continue;
      set_text(field, value);  // checked as it changes, as typing is
      return true;
    }
    return false;
  };
  if (element_.shown) return type(element_);
  if (relationship_.shown) return type(relationship_);
  return false;
}

bool Inspector::set_field(const std::string& key, const std::string& value) {
  if (!get_visible()) return false;
  if (element_.shown) return set(element_, key, value);
  if (relationship_.shown) return set(relationship_, key, value);
  return false;
}

}  // namespace dd::gtk
