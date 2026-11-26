#include "field_controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "host/swatches.hpp"
#include "painter.hpp"

namespace dd::gtk {

using dd::host::StyleControl;
using dd::host::StyleKey;

namespace {

std::string trim(std::string s) {
  auto space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (!s.empty() && space(s.back())) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && space(s[i])) ++i;
  return s.substr(i);
}

std::string hex_of(const Gdk::RGBA& c) {
  auto byte = [](double f) { return static_cast<int>(std::lround(std::clamp(f, 0.0, 1.0) * 255)); };
  char out[8];
  std::snprintf(out, sizeof out, "#%02x%02x%02x", byte(c.get_red()), byte(c.get_green()), byte(c.get_blue()));
  return out;
}

/// A pop-up entry's preview (an end or a dash pattern), drawn by the core's renderer in the
/// widget's text colour; `preview` renders it again only when that colour or the size changes.
void draw_sample(const Cairo::RefPtr<Cairo::Context>& cr, const Gtk::Widget& widget, dd::host::LinePreview& preview,
                 int width, int height) {
  Gdk::RGBA ink = widget.get_color();
  // Hollow ends are filled: white under dark text, dark under light text.
  bool dark = ink.get_red() + ink.get_green() + ink.get_blue() > 1.5;
  if (const auto& list = preview.list(hex_of(ink), dark ? "#2d2d2d" : "#ffffff", width, height))
    paint(cr->cobj(), *list);
}

Gdk::RGBA rgba_of(const std::string& hex) {
  Gdk::RGBA c;
  if (auto n = dd::host::normalized_color(hex)) {
    c.set(*n);
  } else {
    c.set_rgba(0, 0, 0, 0);
  }
  return c;
}

/// Calls `f` when the widget loses the keyboard focus.
template <class F>
void on_leave(Gtk::Widget& w, F f) {
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect(f);
  w.add_controller(focus);
}

/// Calls `f` on Escape in the widget.
template <class F>
void on_escape(Gtk::Widget& w, F f) {
  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [f](guint key, guint, Gdk::ModifierType) {
        if (key != GDK_KEY_Escape) return false;
        f();
        return true;
      },
      false);
  w.add_controller(keys);
}

Gtk::Button* flat_button(const char* icon, const char* tip) {
  auto b = Gtk::make_managed<Gtk::Button>();
  b->set_icon_name(icon);
  b->set_has_frame(false);
  b->set_tooltip_text(tip);
  b->set_valign(Gtk::Align::CENTER);
  return b;
}

}  // namespace

bool FieldControl::editing() const {
  const auto* root = get_root();
  const Gtk::Widget* focus = root ? root->get_focus() : nullptr;
  return focus && (focus == this || focus->is_ancestor(const_cast<FieldControl&>(*this)));
}

// ── ChoiceEntry ──────────────────────────────────────────────────────────────────────────────

ChoiceEntry::ChoiceEntry() : FieldControl(Gtk::Orientation::HORIZONTAL, 0) {
  add_css_class("linked");
  entry_.set_hexpand(true);
  append(entry_);
  button_.set_icon_name("pan-down-symbolic");
  button_.set_tooltip_text("Choose");
  button_.set_popover(popover_);
  append(button_);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll_.set_max_content_height(320);
  scroll_.set_propagate_natural_height(true);
  scroll_.set_child(list_);
  popover_.set_child(scroll_);
  list_.set_selection_mode(Gtk::SelectionMode::NONE);
  list_.set_activate_on_single_click(true);
  list_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
    auto i = static_cast<std::size_t>(row->get_index());
    if (i >= choices_.size()) return;
    popover_.popdown();
    entry_.set_text(choices_[i].value);
    notify();
  });
}

void ChoiceEntry::set_choices(const std::vector<dd::host::Choice>& choices) {
  if (choices == choices_) return;
  choices_ = choices;
  while (auto* row = list_.get_row_at_index(0)) list_.remove(*row);
  std::string group;
  for (const auto& c : choices_) {
    auto label = Gtk::make_managed<Gtk::Label>(c.group.empty() || c.label == c.value ? c.label : c.label + "  ·  " + c.value);
    label->set_xalign(0);
    label->set_margin_start(6);
    label->set_margin_end(6);
    if (!c.group.empty()) label->add_css_class("dim-label");
    list_.append(*label);
  }
  // A heading before each group (a library, or kinds the document uses that nothing lists).
  list_.set_header_func([this](Gtk::ListBoxRow* row, Gtk::ListBoxRow* before) {
    auto i = static_cast<std::size_t>(row->get_index());
    bool first = i < choices_.size() && !choices_[i].group.empty() &&
                 (!before || choices_[static_cast<std::size_t>(before->get_index())].group != choices_[i].group);
    if (!first) {
      row->unset_header();
      return;
    }
    auto header = Gtk::make_managed<Gtk::Label>(choices_[i].group);
    header->set_xalign(0);
    header->set_margin_start(6);
    header->set_margin_top(6);
    header->add_css_class("caption-heading");
    row->set_header(*header);
  });
}

void ChoiceEntry::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing()) return;
  entry_.set_text(text);
}

// ── TagsField ────────────────────────────────────────────────────────────────────────────────

TagsField::TagsField() : FieldControl(Gtk::Orientation::VERTICAL, 2) {
  chips_.set_selection_mode(Gtk::SelectionMode::NONE);
  chips_.set_max_children_per_line(8);
  chips_.set_column_spacing(4);
  chips_.set_row_spacing(2);
  chips_.set_homogeneous(false);
  append(chips_);
  auto& entry = entry_.entry();
  entry.set_placeholder_text("add a tag");
  append(entry_);
  entry_.changed = [this] { add_typed(); };  // a tag picked from the list
  entry.signal_activate().connect([this] { add_typed(); });
  on_leave(entry, [this] { add_typed(); });
  on_escape(entry, [&entry] { entry.set_text(""); });
  // A comma adds the tag typed so far.
  entry.signal_changed().connect([this, &entry] {
    if (entry.get_text().find(',') != Glib::ustring::npos) add_typed();
  });
}

void TagsField::set_suggestions(const std::vector<std::string>& tags) {
  std::vector<dd::host::Choice> choices;
  for (const auto& t : tags) choices.push_back({t, t, ""});
  entry_.set_choices(choices);
}

void TagsField::add_typed() {
  auto& entry = entry_.entry();
  auto typed = dd::host::split_tags(entry.get_text().raw());
  if (typed.empty()) return;
  auto tags = dd::host::split_tags(text_);
  for (auto& t : typed)
    if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
  // Cleared after the tags are read: clearing fires `changed`, which must not add twice.
  entry.set_text("");
  set(tags);
}

void TagsField::set(const std::vector<std::string>& tags) {
  std::string text = dd::host::join_tags(tags);
  if (text == text_) return;
  text_ = text;
  rebuild();
  notify();
}

void TagsField::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing() && text == text_) return;
  text_ = dd::host::join_tags(dd::host::split_tags(text));
  rebuild();
}

void TagsField::rebuild() {
  while (auto* child = chips_.get_child_at_index(0)) chips_.remove(*child);
  for (const auto& tag : dd::host::split_tags(text_)) {
    auto chip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    chip->add_css_class("tag-chip");
    auto label = Gtk::make_managed<Gtk::Label>(tag);
    label->set_margin_start(8);
    chip->append(*label);
    auto remove = flat_button("window-close-symbolic", ("Remove " + tag).c_str());
    remove->signal_clicked().connect([this, tag] {
      auto tags = dd::host::split_tags(text_);
      tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
      // After this handler: the chip holding the button goes away.
      Glib::signal_idle().connect_once([this, tags] { set(tags); });
    });
    chip->append(*remove);
    chips_.append(*chip);
  }
  chips_.set_visible(!text_.empty());
}

// ── PropertiesTable ──────────────────────────────────────────────────────────────────────────

PropertiesTable::PropertiesTable() : FieldControl(Gtk::Orientation::VERTICAL, 2) {
  append(rows_box_);
  auto add = Gtk::make_managed<Gtk::Button>();
  add->set_icon_name("list-add-symbolic");
  add->set_tooltip_text("Add a property");
  add->set_halign(Gtk::Align::START);
  add->signal_clicked().connect([this] {
    add_row("", "");
    rows_.back().key->grab_focus();
  });
  append(*add);
}

void PropertiesTable::add_row(const std::string& key, const std::string& value) {
  auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
  auto k = Gtk::make_managed<Gtk::Entry>();
  auto v = Gtk::make_managed<Gtk::Entry>();
  k->set_text(key);
  v->set_text(value);
  k->set_placeholder_text("key");
  v->set_placeholder_text("value");
  k->set_width_chars(6);
  v->set_width_chars(8);
  k->set_hexpand(true);
  v->set_hexpand(true);
  for (auto* e : {k, v}) {
    e->signal_activate().connect([this] { commit_rows(); });
    on_leave(*e, [this] { commit_rows(); });
    on_escape(*e, [this] { show_text(text_, false); });
  }
  auto remove = flat_button("list-remove-symbolic", "Remove this property");
  remove->signal_clicked().connect([this, box] {
    auto at = std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.box == box; });
    if (at == rows_.end()) return;
    rows_.erase(at);
    Glib::signal_idle().connect_once([this, box] {
      rows_box_.remove(*box);
      commit_rows();
    });
  });
  box->append(*k);
  box->append(*v);
  box->append(*remove);
  rows_box_.append(*box);
  rows_.push_back({box, k, v});
}

void PropertiesTable::commit_rows() {
  std::vector<std::pair<std::string, std::string>> rows;
  for (const auto& r : rows_) rows.emplace_back(r.key->get_text(), r.value->get_text());
  std::string text = dd::host::property_text(rows);
  if (text == text_) return;
  text_ = text;
  notify();
}

void PropertiesTable::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing()) return;
  text_ = text;
  for (auto& r : rows_) rows_box_.remove(*r.box);
  rows_.clear();
  for (const auto& [k, v] : dd::host::property_rows(text)) add_row(k, v);
}

// ── ColorField ───────────────────────────────────────────────────────────────────────────────

namespace {

/// A swatch (or the colour button's well) in `color`, with a thin edge so white shows on white;
/// no colour draws the diagonal line that means "default". `ring` marks the current colour.
void paint_swatch(const Cairo::RefPtr<Cairo::Context>& cr, const Gtk::Widget& widget, int w, int h,
                  const std::string& color, bool ring) {
  const double inset = ring ? 3 : 1;
  const double x = inset, y = inset, sw = w - 2 * inset, sh = h - 2 * inset;
  if (sw <= 0 || sh <= 0) return;
  Gdk::RGBA ink = widget.get_color();
  if (ring) {
    cr->set_source_rgb(ink.get_red(), ink.get_green(), ink.get_blue());
    cr->set_line_width(2);
    cr->rectangle(1, 1, w - 2, h - 2);
    cr->stroke();
  }
  if (auto n = dd::host::normalized_color(color)) {
    Gdk::RGBA c(*n);
    cr->set_source_rgb(c.get_red(), c.get_green(), c.get_blue());
    cr->rectangle(x, y, sw, sh);
    cr->fill();
  } else {
    cr->set_source_rgb(0.882, 0.114, 0.282);  // #e11d48, as the other apps draw "default"
    cr->set_line_width(1.5);
    cr->move_to(x + 1, y + sh - 1);
    cr->line_to(x + sw - 1, y + 1);
    cr->stroke();
  }
  cr->set_source_rgba(ink.get_red(), ink.get_green(), ink.get_blue(), 0.25);
  cr->set_line_width(1);
  cr->rectangle(x + 0.5, y + 0.5, sw - 1, sh - 1);
  cr->stroke();
}

/// Names a widget for screen readers (a tooltip is only its description).
void set_accessible_label(Gtk::Widget& widget, const Glib::ustring& name) {
  Glib::Value<Glib::ustring> value;
  value.init(Glib::Value<Glib::ustring>::value_type());
  value.set(name);
  widget.update_property(Gtk::Accessible::Property::LABEL, value);
}

}  // namespace

ColorField::ColorField(bool compact) : FieldControl(Gtk::Orientation::HORIZONTAL, 2) {
  well_.set_content_width(compact ? 22 : 28);
  well_.set_content_height(16);
  well_.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    paint_swatch(cr, well_, w, h, value_, false);
  });
  button_.set_child(well_);
  button_.set_always_show_arrow(false);
  button_.set_valign(Gtk::Align::CENTER);
  append(button_);

  // The swatches are built when first opened: most fields never are.
  button_.set_create_popup_func([this] {
    if (!popover_) build_popover();
  });

  clear_.set_icon_name("edit-clear-symbolic");
  clear_.set_has_frame(false);
  clear_.set_tooltip_text("Use the default colour");
  clear_.set_valign(Gtk::Align::CENTER);
  append(clear_);
  if (!compact) {
    hex_.add_css_class("dim-label");
    hex_.add_css_class("monospace");
    hex_.add_css_class("caption");
    append(hex_);
  }
  clear_.signal_clicked().connect([this] { pick(""); });
  show();
}

/// The swatches, row by row, then Custom… and Default.
void ColorField::build_popover() {
  popover_ = Gtk::make_managed<Gtk::Popover>();
  auto content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  auto grid = Gtk::make_managed<Gtk::Grid>();
  grid->set_row_spacing(2);
  grid->set_column_spacing(2);
  const auto& rows = dd::host::color_swatches();
  for (std::size_t r = 0; r < rows.size(); ++r) {
    for (std::size_t c = 0; c < rows[r].size(); ++c) {
      const auto& swatch = rows[r][c];
      auto area = Gtk::make_managed<Gtk::DrawingArea>();
      area->set_content_width(20);
      area->set_content_height(20);
      area->set_draw_func([this, area, color = swatch.color](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        paint_swatch(cr, *area, w, h, color, dd::host::normalized_color(value_) == color);
      });
      swatches_.push_back(area);
      auto button = Gtk::make_managed<Gtk::Button>();
      button->set_child(*area);
      button->set_has_frame(false);
      button->add_css_class("flat");
      button->set_tooltip_text(swatch.name + "  " + swatch.color);
      set_accessible_label(*button, swatch.name);
      button->signal_clicked().connect([this, color = swatch.color] {
        popover_->popdown();
        pick(color);
      });
      // A gap between the first row (the greys) and the colours.
      if (r == 1) button->set_margin_top(4);
      grid->attach(*button, static_cast<int>(c), static_cast<int>(r));
    }
  }
  content->append(*grid);
  auto actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  auto custom = Gtk::make_managed<Gtk::Button>("Custom…");
  custom->set_hexpand(true);
  custom->set_halign(Gtk::Align::START);
  custom->signal_clicked().connect([this] {
    popover_->popdown();
    choose_custom();
  });
  default_ = Gtk::make_managed<Gtk::Button>("Default");
  default_->set_tooltip_text("Use the default colour");
  default_->signal_clicked().connect([this] {
    popover_->popdown();
    pick("");
  });
  actions->append(*custom);
  actions->append(*default_);
  content->append(*actions);
  popover_->set_child(*content);
  button_.set_popover(*popover_);
  default_->set_sensitive(!value_.empty());
}

ColorField::~ColorField() {
  if (choosing_) choosing_->cancel();
}

void ColorField::pick(const std::string& value) {
  if (value == value_) return;
  value_ = value;
  show();
  notify();
}

void ColorField::choose_custom() {
  if (choosing_) choosing_->cancel();
  auto dialog = Gtk::ColorDialog::create();
  dialog->set_with_alpha(false);
  auto cancellable = Gio::Cancellable::create();
  choosing_ = cancellable;
  // The reply comes after a cancel too (when the field went away): then it is dropped unread.
  auto done = [this, dialog, cancellable](const Glib::RefPtr<Gio::AsyncResult>& result) {
    if (cancellable->is_cancelled()) return;
    if (choosing_ == cancellable) choosing_.reset();
    Gdk::RGBA rgba;
    try {
      rgba = dialog->choose_rgba_finish(result);
    } catch (const Glib::Error&) {
      return;  // dismissed: nothing changes
    }
    pick(hex_of(rgba));
  };
  Gdk::RGBA initial = rgba_of(dd::host::normalized_color(value_).value_or("#ffffff"));
  if (auto* window = dynamic_cast<Gtk::Window*>(get_root()))
    dialog->choose_rgba(*window, initial, done, cancellable);
  else
    dialog->choose_rgba(initial, done, cancellable);
}

void ColorField::show_text(const std::string& text, bool) {
  value_ = text;
  show();
}

void ColorField::show() {
  well_.queue_draw();
  for (auto* s : swatches_) s->queue_draw();
  if (default_) default_->set_sensitive(!value_.empty());
  clear_.set_visible(!value_.empty());
  hex_.set_text(value_.empty() ? "default" : value_);
  button_.set_tooltip_text(value_.empty() ? "default" : value_);
  set_accessible_label(button_, "Colour: " + (value_.empty() ? std::string("default") : value_));
}

// ── StyleEditor ──────────────────────────────────────────────────────────────────────────────

StyleEditor::StyleEditor(dd::StyleTarget target) : FieldControl(Gtk::Orientation::VERTICAL, 4), target_(target) {
  add_css_class("card");
  set_margin_bottom(6);
  auto grid = Gtk::make_managed<Gtk::Grid>();
  grid->set_row_spacing(4);
  grid->set_column_spacing(8);
  grid->set_margin_start(8);
  grid->set_margin_end(8);
  grid->set_margin_top(6);
  append(*grid);
  int row = 0;
  std::string section;
  const auto& keys = dd::host::style_keys(target);
  controls_.reserve(keys.size());
  auto caption = [&](const std::string& text, const char* css) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    l->set_xalign(css ? 0.0f : 1.0f);
    l->add_css_class(css ? css : "dim-label");
    return l;
  };
  for (const auto& k : keys) {
    if (k.section != section) {
      section = k.section;
      auto heading = caption(section, "heading");
      heading->set_margin_top(row ? 6 : 0);
      grid->attach(*heading, 0, row++, 2, 1);
    }
    grid->attach(*caption(k.label, nullptr), 0, row);
    Control c{&k};
    Gtk::Widget* widget = nullptr;
    switch (k.control) {
      case StyleControl::Color: {
        c.color = Gtk::make_managed<ColorField>(true);
        c.color->changed = [this, &k, f = c.color] { set(k, f->text()); };
        widget = c.color;
        break;
      }
      case StyleControl::Number: {
        c.entry = Gtk::make_managed<Gtk::Entry>();
        c.entry->set_placeholder_text(k.fallback);
        c.entry->set_input_purpose(Gtk::InputPurpose::NUMBER);
        c.entry->set_width_chars(6);
        c.entry->set_max_width_chars(6);
        auto e = c.entry;
        e->signal_activate().connect([this, &k, e] { set(k, trim(e->get_text())); });
        on_leave(*e, [this, &k, e] { set(k, trim(e->get_text())); });
        on_escape(*e, [this] { show(); });
        widget = e;
        break;
      }
      case StyleControl::Flag: {
        c.check = Gtk::make_managed<Gtk::CheckButton>();
        auto b = c.check;
        b->signal_toggled().connect([this, &k, b] {
          if (!filling_) set(k, b->get_active() ? "1" : "0");
        });
        widget = b;
        break;
      }
      case StyleControl::Choice: {
        std::vector<Glib::ustring> labels;
        for (const auto& choice : k.choices) labels.emplace_back(choice.label);
        c.choice = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(labels));
        auto d = c.choice;
        if (dd::host::style_sample(k, "")) {
          // Ends and dash patterns show a preview of the line beside their name.
          auto factory = Gtk::SignalListItemFactory::create();
          factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto area = Gtk::make_managed<Gtk::DrawingArea>();
            area->set_content_width(48);
            area->set_content_height(14);
            box->append(*area);
            box->append(*Gtk::make_managed<Gtk::Label>());
            item->set_child(*box);
          });
          factory->signal_bind().connect([&k](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto at = item->get_position();
            auto box = dynamic_cast<Gtk::Box*>(item->get_child());
            if (at >= k.choices.size() || !box) return;
            auto area = dynamic_cast<Gtk::DrawingArea*>(box->get_first_child());
            auto label = area ? dynamic_cast<Gtk::Label*>(area->get_next_sibling()) : nullptr;
            if (!label) return;
            label->set_text(k.choices[at].label);
            auto sample = dd::host::style_sample(k, k.choices[at].value);
            // Shared: the slot may be copied, and every copy draws the same preview.
            auto preview = std::make_shared<dd::host::LinePreview>(sample.value_or(dd::Style{}));
            area->set_draw_func([area, preview](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
              draw_sample(cr, *area, *preview, w, h);
            });
          });
          d->set_factory(factory);
        }
        d->property_selected().signal_changed().connect([this, &k, d] {
          if (filling_) return;
          auto at = d->get_selected();
          if (at < k.choices.size()) set(k, k.choices[at].value);
        });
        widget = d;
        break;
      }
      case StyleControl::FontStyle: {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        box->add_css_class("linked");
        const std::pair<const char*, const char*> names[] = {{"format-text-bold-symbolic", "Bold"},
                                                            {"format-text-italic-symbolic", "Italic"},
                                                            {"format-text-underline-symbolic", "Underline"}};
        for (int bit = 0; bit < 3; ++bit) {
          auto t = Gtk::make_managed<Gtk::ToggleButton>();
          t->set_icon_name(names[bit].first);
          t->set_tooltip_text(names[bit].second);
          t->signal_toggled().connect([this, &k, bit] {
            if (filling_) return;
            int bits = dd::host::font_style_bits(dd::host::style_value(style_, k.key).value_or(""));
            set(k, std::to_string(bits ^ (1 << bit)));
          });
          box->append(*t);
          c.toggles.push_back(t);
        }
        widget = box;
        break;
      }
    }
    widget->set_halign(Gtk::Align::START);
    grid->attach(*widget, 1, row++);
    controls_.push_back(std::move(c));
  }
  grid->attach(*caption("Other", nullptr), 0, row);
  other_.set_placeholder_text("shadow=1;whiteSpace=wrap");
  other_.add_css_class("monospace");
  other_.set_hexpand(true);
  auto typed = [this] { apply(dd::host::with_other_style(style_, target_, other_.get_text().raw())); };
  other_.signal_activate().connect(typed);
  on_leave(other_, typed);
  on_escape(other_, [this] { show(); });
  grid->attach(other_, 1, row++);
  clear_.set_halign(Gtk::Align::START);
  clear_.set_margin_start(8);
  clear_.set_margin_bottom(8);
  clear_.signal_clicked().connect([this] { apply(""); });
  append(clear_);
  show();
}

void StyleEditor::set(const StyleKey& key, const std::string& value) {
  apply(dd::host::with_style_setting(style_, key, value));
}

void StyleEditor::apply(std::string style) {
  if (style == style_) {
    show();
    return;
  }
  style_ = std::move(style);
  show();
  notify();
}

void StyleEditor::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing() && text == style_) return;
  style_ = text;
  show();
}

void StyleEditor::show() {
  filling_ = true;
  const auto* root = get_root();
  const Gtk::Widget* focus = root ? root->get_focus() : nullptr;
  auto typing = [&](Gtk::Widget& w) { return focus && (focus == &w || focus->is_ancestor(w)); };
  for (auto& c : controls_) {
    std::string value = dd::host::style_control_value(style_, *c.key).value_or("");
    if (c.color) {
      c.color->show_text(value, false);
    } else if (c.entry) {
      if (!typing(*c.entry)) c.entry->set_text(value);
    } else if (c.check) {
      c.check->set_active((value.empty() ? c.key->fallback : value) == "1");
    } else if (c.choice) {
      const auto& choices = c.key->choices;
      auto at = std::find_if(choices.begin(), choices.end(), [&](const auto& ch) { return ch.value == value; });
      // A value the list does not have shows as nothing picked; Other does not take it either.
      c.choice->set_selected(at == choices.end() ? GTK_INVALID_LIST_POSITION
                                                 : static_cast<guint>(at - choices.begin()));
    } else {
      int bits = dd::host::font_style_bits(value);
      for (std::size_t i = 0; i < c.toggles.size(); ++i) c.toggles[i]->set_active((bits >> i) & 1);
    }
  }
  if (!typing(other_)) other_.set_text(dd::host::other_style(style_, target_));
  clear_.set_sensitive(!style_.empty());
  filling_ = false;
}

bool StyleEditor::set_key(const std::string& key, const std::string& value) {
  if (key == "other") {
    apply(dd::host::with_other_style(style_, target_, value));
    return true;
  }
  for (const auto& c : controls_) {
    if (c.key->key != key) continue;
    std::string v = value;
    if (c.key->control == StyleControl::Color && !v.empty()) {
      // As the colour button would write it.
      auto n = dd::host::normalized_color(v);
      if (!n) return false;
      v = *n;
    }
    if (v.empty() && c.key->control == StyleControl::Flag) v = c.key->fallback;
    set(*c.key, v);
    return true;
  }
  return false;
}

// ── StyleBlocks ──────────────────────────────────────────────────────────────────────────────

StyleBlocks::StyleBlocks() : FieldControl(Gtk::Orientation::VERTICAL, 4) {
  append(list_);
  auto add = Gtk::make_managed<Gtk::Button>("Add style block");
  add->set_halign(Gtk::Align::START);
  add->signal_clicked().connect([this] {
    rows_.push_back({"", ""});
    open_.push_back(true);
    rebuild();
    blocks_.back().selector->entry().grab_focus();
  });
  append(*add);
}

void StyleBlocks::set_selectors(const std::vector<std::string>& selectors) {
  std::vector<dd::host::Choice> choices;
  for (const auto& s : selectors) choices.push_back({s, s, ""});
  if (choices == selectors_) return;
  selectors_ = std::move(choices);
  for (auto& b : blocks_) b.selector->set_choices(selectors_);
}

void StyleBlocks::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing()) return;
  // Blocks just added and without a selector yet are not in the text: keep them while they are.
  bool pending = std::any_of(rows_.begin(), rows_.end(), [](const auto& r) { return trim(r.selector).empty(); });
  auto rows = dd::host::block_rows(text);
  bool same_shape = rows.size() == rows_.size() && blocks_.size() == rows.size();
  for (std::size_t i = 0; same_shape && i < rows.size(); ++i) same_shape = rows[i].selector == rows_[i].selector;
  text_ = text;
  if (!same_shape && pending && text == dd::host::block_text(rows_)) return;
  rows_ = std::move(rows);
  open_.resize(rows_.size(), false);
  if (same_shape)
    refresh();
  else
    rebuild();
}

void StyleBlocks::refresh() {
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    auto& b = blocks_[i];
    if (!b.selector->editing()) b.selector->show_text(rows_[i].selector, false);
    b.summary->set_text(rows_[i].style.empty() ? "no keys yet" : rows_[i].style);
    if (b.editor) b.editor->show_text(rows_[i].style, true);
  }
}

void StyleBlocks::rebuild() {
  while (auto* child = list_.get_first_child()) list_.remove(*child);
  blocks_.clear();
  open_.resize(rows_.size(), false);
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    auto toggle = Gtk::make_managed<Gtk::Button>();
    toggle->set_icon_name(open_[i] ? "pan-down-symbolic" : "pan-end-symbolic");
    toggle->set_has_frame(false);
    toggle->set_tooltip_text("Edit this block's keys");
    toggle->signal_clicked().connect([this, i] {
      open_[i] = !open_[i];
      Glib::signal_idle().connect_once([this] { rebuild(); });
    });
    head->append(*toggle);
    auto selector = Gtk::make_managed<ChoiceEntry>();
    selector->set_hexpand(true);
    selector->set_choices(selectors_);
    selector->show_text(rows_[i].selector, false);
    auto& entry = selector->entry();
    entry.set_placeholder_text("container, tag \"external\", relationship");
    entry.add_css_class("monospace");
    selector->changed = [this, i] { selector_typed(i); };
    entry.signal_activate().connect([this, i] { selector_typed(i); });
    on_leave(entry, [this, i] { selector_typed(i); });
    on_escape(entry, [this, i, &entry] {
      if (i < rows_.size()) entry.set_text(rows_[i].selector);
    });
    head->append(*selector);
    auto remove = flat_button("list-remove-symbolic", "Remove this style block");
    remove->signal_clicked().connect([this, i] {
      if (i >= rows_.size()) return;
      rows_.erase(rows_.begin() + static_cast<long>(i));
      open_.erase(open_.begin() + static_cast<long>(i));
      Glib::signal_idle().connect_once([this] { rebuild(); });
      commit_rows();
    });
    head->append(*remove);
    list_.append(*head);
    auto summary = Gtk::make_managed<Gtk::Label>(rows_[i].style.empty() ? "no keys yet" : rows_[i].style);
    summary->set_xalign(0);
    summary->set_ellipsize(Pango::EllipsizeMode::END);
    summary->add_css_class("dim-label");
    summary->add_css_class("monospace");
    summary->add_css_class("caption");
    summary->set_margin_start(28);
    summary->set_visible(!open_[i]);
    list_.append(*summary);
    Block block{selector, summary};
    if (open_[i]) {
      block.editor = Gtk::make_managed<StyleEditor>(dd::host::selector_target(rows_[i].selector));
      block.editor->show_text(rows_[i].style, false);
      block.editor->set_margin_start(12);
      auto* editor = block.editor;
      editor->changed = [this, i, editor] {
        if (i >= rows_.size()) return;
        rows_[i].style = editor->text();
        blocks_[i].summary->set_text(rows_[i].style.empty() ? "no keys yet" : rows_[i].style);
        commit_rows();
      };
      list_.append(*editor);
    }
    blocks_.push_back(block);
  }
}

void StyleBlocks::selector_typed(std::size_t i) {
  if (i >= rows_.size() || i >= blocks_.size()) return;
  std::string typed = trim(blocks_[i].selector->text());
  if (typed == rows_[i].selector) return;
  bool retarget = dd::host::selector_target(typed) != dd::host::selector_target(rows_[i].selector);
  rows_[i].selector = typed;
  // An editor for elements does not fit a relationship block, and the other way round.
  if (retarget && open_[i]) Glib::signal_idle().connect_once([this] { rebuild(); });
  commit_rows();
}

void StyleBlocks::commit_rows() {
  std::string text = dd::host::block_text(rows_);
  if (text == text_) return;
  text_ = text;
  notify();
}

// ── ThemeKeys ────────────────────────────────────────────────────────────────────────────────

ThemeKeys::ThemeKeys() : FieldControl(Gtk::Orientation::VERTICAL, 0) {
  auto grid = Gtk::make_managed<Gtk::Grid>();
  grid->set_row_spacing(4);
  grid->set_column_spacing(8);
  append(*grid);
  const auto& keys = dd::theme_keys();
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const std::string& key = keys[i];
    auto label = Gtk::make_managed<Gtk::Label>(key);
    label->set_xalign(1);
    label->add_css_class("dim-label");
    grid->attach(*label, 0, static_cast<int>(i));
    if (dd::host::is_color_key(key)) {
      auto color = Gtk::make_managed<ColorField>();
      color->changed = [this, key, color] { set(key, color->text()); };
      color->set_halign(Gtk::Align::START);
      grid->attach(*color, 1, static_cast<int>(i));
      controls_.emplace_back(color, nullptr);
    } else {
      auto entry = Gtk::make_managed<Gtk::Entry>();
      entry->set_placeholder_text("the base theme's");
      entry->set_hexpand(true);
      entry->signal_activate().connect([this, key, entry] { set(key, entry->get_text()); });
      on_leave(*entry, [this, key, entry] { set(key, entry->get_text()); });
      on_escape(*entry, [this] { show_text(text_, false); });
      grid->attach(*entry, 1, static_cast<int>(i));
      controls_.emplace_back(nullptr, entry);
    }
  }
}

void ThemeKeys::show_text(const std::string& text, bool keep_typing) {
  text_ = text;
  auto rows = dd::host::property_rows(text);
  const auto& keys = dd::theme_keys();
  const auto* root = get_root();
  const Gtk::Widget* focus = root ? root->get_focus() : nullptr;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    std::string value;
    for (const auto& [k, v] : rows)
      if (k == keys[i]) value = v;
    auto [color, entry] = controls_[i];
    if (color) {
      color->show_text(value, false);
    } else if (!(keep_typing && focus && (focus == entry || focus->is_ancestor(*entry)))) {
      entry->set_text(value);
    }
  }
}

void ThemeKeys::set(const std::string& key, const std::string& value) {
  auto rows = dd::host::property_rows(text_);
  std::string v = trim(value);
  auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.first == key; });
  if (v.empty()) {
    if (at == rows.end()) return;
    rows.erase(at);
  } else if (at != rows.end()) {
    at->second = v;
  } else {
    rows.emplace_back(key, v);
  }
  std::string text = dd::host::property_text(rows);
  if (text == text_) return;
  text_ = text;
  notify();
}

// ── ThemeNodes ───────────────────────────────────────────────────────────────────────────────

namespace {
const std::pair<const char*, const char*> kNodeKeys[] = {{"fill", "Fill"}, {"stroke", "Line"}, {"text", "Text"}};
}

ThemeNodes::ThemeNodes() : FieldControl(Gtk::Orientation::VERTICAL, 4) {
  append(list_);
  auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  add_.set_hexpand(true);
  add_.entry().set_placeholder_text("kind");
  add_.changed = [this] { add_kind(); };
  add_.entry().signal_activate().connect([this] { add_kind(); });
  row->append(add_);
  auto button = Gtk::make_managed<Gtk::Button>("Add");
  button->signal_clicked().connect([this] { add_kind(); });
  row->append(*button);
  append(*row);
}

void ThemeNodes::show_text(const std::string& text, bool keep_typing) {
  if (keep_typing && editing() && text == text_) return;
  auto rows = dd::host::block_rows(text);
  // A kind just added has no colours yet, so the text does not have it: keep it shown, unless
  // another theme is shown (keep_typing false).
  if (keep_typing)
    for (const auto& r : rows_)
      if (r.style.empty() &&
          std::none_of(rows.begin(), rows.end(), [&](const auto& t) { return t.selector == r.selector; }))
        rows.push_back(r);
  bool same_shape = rows.size() == rows_.size() && fields_.size() == rows.size();
  for (std::size_t i = 0; same_shape && i < rows.size(); ++i) same_shape = rows[i].selector == rows_[i].selector;
  text_ = text;
  rows_ = std::move(rows);
  if (!same_shape) rebuild();
  for (std::size_t i = 0; i < rows_.size(); ++i)
    for (std::size_t k = 0; k < 3; ++k)
      fields_[i][k]->show_text(dd::host::style_value(rows_[i].style, kNodeKeys[k].first).value_or(""), false);
}

void ThemeNodes::rebuild() {
  while (auto* child = list_.get_first_child()) list_.remove(*child);
  fields_.clear();
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto kind = Gtk::make_managed<Gtk::Label>(rows_[i].selector);
    kind->set_xalign(0);
    kind->set_hexpand(true);
    kind->add_css_class("heading");
    head->append(*kind);
    auto remove = flat_button("list-remove-symbolic", "Remove this kind's colours");
    remove->signal_clicked().connect([this, i] {
      if (i >= rows_.size()) return;
      rows_.erase(rows_.begin() + static_cast<long>(i));
      text_ = dd::host::block_text(rows_);
      Glib::signal_idle().connect_once([this] { show_text(text_, false); });
      notify();
    });
    head->append(*remove);
    list_.append(*head);
    auto wells = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    std::vector<ColorField*> fields;
    for (std::size_t k = 0; k < 3; ++k) {
      auto label = Gtk::make_managed<Gtk::Label>(kNodeKeys[k].second);
      label->add_css_class("dim-label");
      wells->append(*label);
      auto field = Gtk::make_managed<ColorField>(true);
      field->changed = [this, i, k, field] {
        if (i >= rows_.size()) return;
        auto value = field->text();
        rows_[i].style = dd::host::with_style_value(rows_[i].style, kNodeKeys[k].first,
                                                    value.empty() ? std::nullopt : std::optional<std::string>(value));
        std::string text = dd::host::block_text(rows_);
        if (text == text_) return;
        text_ = text;
        notify();
      };
      wells->append(*field);
      fields.push_back(field);
    }
    list_.append(*wells);
    fields_.push_back(std::move(fields));
  }
}

void ThemeNodes::add_kind() {
  std::string kind = trim(add_.text());
  if (kind.empty()) return;
  add_.entry().set_text("");
  if (std::any_of(rows_.begin(), rows_.end(), [&](const auto& r) { return r.selector == kind; })) return;
  // Written once it has a colour; until then it is a row of unset buttons.
  rows_.push_back({kind, ""});
  rebuild();
  for (std::size_t i = 0; i < rows_.size(); ++i)
    for (std::size_t k = 0; k < 3; ++k)
      fields_[i][k]->show_text(dd::host::style_value(rows_[i].style, kNodeKeys[k].first).value_or(""), false);
}

}  // namespace dd::gtk
