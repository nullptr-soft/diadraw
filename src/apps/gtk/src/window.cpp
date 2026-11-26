#include "window.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>

#include "diadraw/dsl/highlight.hpp"
#include "host/examples.hpp"

namespace dd::gtk {

std::function<DocumentWindow*(const std::string&, const std::string&)> DocumentWindow::open_window;

namespace {

const char* kDefaultText =
    "model {\n  user = person \"Customer\"\n  shop = system \"Shop\"\n  user -> shop \"browses\"\n}\n";

/// Character offset (what GtkTextBuffer counts) of each UTF-8 byte offset of `text`.
std::vector<int> char_map(const std::string& text) {
  std::vector<int> map(text.size() + 1);
  int n = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    map[i] = n;
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++n;
  }
  map[text.size()] = n;
  return map;
}

const char* tag_name(dd::dsl::TokenKind kind) {
  using K = dd::dsl::TokenKind;
  switch (kind) {
    case K::Comment: return "dd-comment";
    case K::String: return "dd-string";
    case K::Number: return "dd-number";
    case K::Keyword: return "dd-keyword";
    case K::Kind: return "dd-kind";
    case K::Definition: return "dd-definition";
    case K::Operator:
    case K::Bracket: return "dd-punctuation";
    case K::Name: return nullptr;
  }
  return nullptr;
}

/// The width of a divider between the window's panes, as on the other platforms.
constexpr int kDividerWidth = 8;

/// Gives a divider the look the other platforms have: a bar with a grip of three dots in its
/// middle. The style sheet goes to the display once, for every window.
void style_divider(Gtk::Paned& paned) {
  [[maybe_unused]] static const bool installed = [] {
    auto css = Gtk::CssProvider::create();
    css->load_from_data("paned.dd-divider > separator.wide { min-width: " + std::to_string(kDividerWidth) +
                        "px; }" R"(
      paned.dd-divider > separator.wide {
        /* the hairline edges the theme drew as background images, which the dots replace */
        box-shadow: inset 1px 0 alpha(currentColor, 0.15), inset -1px 0 alpha(currentColor, 0.15);
        background-color: alpha(currentColor, 0.04);
        background-image:
          radial-gradient(circle at 50% 2px, alpha(currentColor, 0.55) 1.5px, transparent 2px),
          radial-gradient(circle at 50% 9px, alpha(currentColor, 0.55) 1.5px, transparent 2px),
          radial-gradient(circle at 50% 16px, alpha(currentColor, 0.55) 1.5px, transparent 2px);
        background-size: 4px 18px;
        background-repeat: no-repeat;
        background-position: center;
      }
      paned.dd-divider > separator.wide:hover {
        background-color: alpha(currentColor, 0.1);
      }
    )");
    Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), css,
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return true;
  }();
  paned.set_wide_handle(true);
  paned.add_css_class("dd-divider");
}

}  // namespace

DocumentWindow::DocumentWindow(const Glib::RefPtr<Gtk::Application>& app, std::string path, const std::string& text)
    : path_(std::move(path)) {
  set_application(app);
  set_default_size(1280, 800);
  build();
  load(text.empty() && path_.empty() ? kDefaultText : text);
}

void DocumentWindow::build() {
  // Header bar: the view on the left; Add and the menu on the right.
  auto header = Gtk::make_managed<Gtk::HeaderBar>();
  view_names_ = Gtk::StringList::create({});
  views_.set_model(view_names_);
  views_.property_selected().signal_changed().connect([this] {
    if (updating_views_) return;
    auto i = views_.get_selected();
    if (i < doc_->views().size()) {
      doc_->choose_view(doc_->views()[i]);
      refresh();
    }
  });
  header->pack_start(views_);
  // Beside the picker: add, rename and remove views.
  auto view_menu = Gio::Menu::create();
  view_menu->append("New View…", "win.new-view");
  view_menu->append("Rename View…", "win.rename-view");
  view_menu->append("Remove View", "win.remove-view");
  auto view_button = Gtk::make_managed<Gtk::MenuButton>();
  view_button->set_icon_name("view-more-symbolic");
  view_button->set_tooltip_text("Add, rename or remove views");
  view_button->set_menu_model(view_menu);
  header->pack_start(*view_button);
  auto menu = Gio::Menu::create();
  auto file = Gio::Menu::create();
  file->append("New", "app.new");
  file->append("Open…", "app.open");
  file->append("Import…", "app.import");
  auto example_menu = Gio::Menu::create();
  for (const auto& e : dd::host::examples()) {
    std::string name(e.name);
    auto entry = Gio::MenuItem::create(name, "");
    entry->set_action_and_target("app.open-example", Glib::Variant<Glib::ustring>::create(name));
    example_menu->append_item(entry);
  }
  file->append_submenu("Examples", example_menu);
  file->append("Save", "win.save");
  file->append("Save As…", "win.save-as");
  menu->append_section(file);
  auto clouds = Gio::Menu::create();
  clouds->append("Cloud Storage…", "win.cloud");
  menu->append_section(clouds);
  auto exports = Gio::Menu::create();
  for (auto [label, ext] : {std::pair{"SVG", "svg"}, {"PNG", "png"}, {"PDF", "pdf"}, {"draw.io", "drawio"},
                             {"BPMN", "bpmn"}})
    exports->append(std::string("Export ") + label + "…", std::string("win.export-") + ext);
  menu->append_section(exports);
  auto edit = Gio::Menu::create();
  edit->append("Undo", "win.undo");
  edit->append("Redo", "win.redo");
  edit->append("Clear Canvas", "win.clear-canvas");
  edit->append("Rename Element…", "win.rename-element");
  edit->append("Size to Fit Content", "win.fit");
  edit->append("Zoom In", "win.zoom-in");
  edit->append("Zoom Out", "win.zoom-out");
  edit->append("Actual Size", "win.zoom-actual");
  edit->append("Show Grid", "win.grid");
  menu->append_section(edit);
  auto help = Gio::Menu::create();
  help->append("Report a Bug…", "app.report-bug");
  help->append("Give Feedback…", "app.send-feedback");
  menu->append_section(help);
  auto about = Gio::Menu::create();
  about->append("About DiaDraw", "app.about");
  menu->append_section(about);
  auto quit = Gio::Menu::create();
  quit->append("Quit", "app.quit");
  menu->append_section(quit);
  auto menu_button = Gtk::make_managed<Gtk::MenuButton>();
  menu_button->set_icon_name("open-menu-symbolic");
  menu_button->set_menu_model(menu);
  header->pack_end(*menu_button);
  // Give Feedback: always in sight and one click away, beside the main menu.
  auto feedback = Gtk::make_managed<Gtk::Button>("Give Feedback");
  feedback->set_action_name("app.send-feedback");
  feedback->set_tooltip_text("Send an idea, a wish or anything that could be easier (opens a form on GitHub)");
  feedback->add_css_class("suggested-action");
  header->pack_end(*feedback);
  // One menu button: choosing a kind adds an element of it.
  auto kinds = Gio::Menu::create();
  for (const auto& k : dd::host::element_kinds()) kinds->append(k, "win.add::" + k);
  auto add = Gtk::make_managed<Gtk::MenuButton>();
  add->set_label("Add");
  add->set_tooltip_text("Add an element of the chosen kind");
  add->set_menu_model(kinds);
  header->pack_end(*add);
  set_titlebar(*header);

  add_action("save", sigc::mem_fun(*this, &DocumentWindow::save));
  add_action("save-as", sigc::mem_fun(*this, &DocumentWindow::save_as));
  add_action("cloud", [this] { show_cloud(); });
  add_action("undo", sigc::mem_fun(*this, &DocumentWindow::undo));
  add_action("redo", sigc::mem_fun(*this, &DocumentWindow::redo));
  add_action("clear-canvas", sigc::mem_fun(*this, &DocumentWindow::ask_clear_canvas));
  add_action("fit", [this] {
    std::vector<std::string> elements = canvas_.canvas().selection().elements;
    std::string before = doc_->text();
    apply(doc_->fit(elements), before);
  });
  add_action("new-view", [this] {
    ask_name("New View", "View name (letters, digits and _):", "Add", doc_->fresh_view(),
             [this](const std::string& id) { new_view(id); });
  });
  add_action("rename-view", [this] {
    ask_name("Rename View", "View name (letters, digits and _):", "Rename", doc_->view(),
             [this](const std::string& id) { rename_view(id); });
  });
  remove_view_action_ = add_action("remove-view", sigc::mem_fun(*this, &DocumentWindow::remove_view));
  add_action("rename-element", sigc::mem_fun(*this, &DocumentWindow::ask_rename_element));
  add_action("zoom-in", [this] { zoom_by(1.25); });
  add_action("zoom-out", [this] { zoom_by(1 / 1.25); });
  add_action("zoom-actual", sigc::mem_fun(*this, &DocumentWindow::zoom_actual));
  // The grid, every 10 diagram units: drags, resizes and bend points line up with it.
  grid_action_ = add_action_bool("grid", [this] {
    bool on = false;
    grid_action_->get_state(on);
    grid_action_->change_state(!on);
    canvas_.canvas().set_grid(on ? 0 : 10);
    canvas_.redraw();
  });
  add_action_with_parameter("add", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& kind) {
    add_element(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(kind).get());
  });
  for (const char* e : {"svg", "png", "pdf", "drawio", "bpmn"})
    add_action(std::string("export-") + e, [this, ext = std::string(e)] { export_as(ext); });

  // The text pane.
  buffer_ = Gtk::TextBuffer::create();
  buffer_->set_enable_undo(true);
  auto colour = [&](const char* name, const char* rgba) { buffer_->create_tag(name)->property_foreground() = rgba; };
  colour("dd-comment", "#8e8e93");
  colour("dd-string", "#d12f1b");
  colour("dd-number", "#c26b00");
  colour("dd-keyword", "#9b2393");
  colour("dd-kind", "#0f7f8c");
  colour("dd-definition", "#0b4fd6");
  colour("dd-punctuation", "#6e6e73");
  for (bool error : {true, false}) {
    auto tag = buffer_->create_tag(error ? "dd-error" : "dd-warning");
    tag->property_underline() = Pango::Underline::ERROR;
    tag->property_underline_rgba() = Gdk::RGBA(error ? "#e5341d" : "#e08a00");
  }
  text_.set_buffer(buffer_);
  text_.set_monospace(true);
  text_.set_wrap_mode(Gtk::WrapMode::NONE);
  text_.set_left_margin(8);
  text_.set_top_margin(8);
  buffer_->signal_changed().connect([this] {
    restyle();
    if (applying_) return;
    timer_.disconnect();
    timer_ = Glib::signal_timeout().connect(
        [this] {
          sync_from_text();
          return false;
        },
        60);
  });
  buffer_->property_cursor_position().signal_changed().connect(
      sigc::mem_fun(*this, &DocumentWindow::show_caret_diagnostic));
  buffer_->signal_modified_changed().connect(sigc::mem_fun(*this, &DocumentWindow::update_title));
  auto text_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  text_scroll->set_child(text_);
  text_scroll->set_size_request(280, -1);

  auto canvas_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  canvas_scroll->set_child(canvas_);

  // The properties panel, on the right of the canvas and always there, so selecting never moves
  // the canvas: the selected element's or relationship's properties, the document's theme and
  // styles while nothing is selected, or a note while several items are.
  canvas_scroll->set_hexpand(true);
  auto panels = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  panels->append(inspector_);
  panels->append(style_panel_);
  panel_note_.set_wrap(true);
  panel_note_.set_xalign(0);
  panel_note_.set_margin(12);
  panel_note_.add_css_class("dim-label");
  panel_note_.set_visible(false);
  panels->append(panel_note_);
  inspector_scroll_.set_child(*panels);
  inspector_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  inspector_scroll_.set_size_request(240, -1);
  // Entries inside fill the panel's width; that must not widen the panel at the canvas's cost.
  inspector_scroll_.set_hexpand(false);
  inspector_.committed = [this](const dd::host::ElementFields& fields) {
    std::string before = doc_->text(), renamed;
    apply(doc_->update(fields, &renamed), before);
    // A new id: keep the element selected under it.
    if (renamed != fields.id) {
      canvas_.canvas().set_selection({{renamed}, {}});
      canvas_.redraw();
      inspect();
    }
  };
  inspector_.check = [this](const dd::host::ElementFields& fields) {
    return doc_ ? doc_->problems(fields) : std::vector<dd::editor::FieldProblem>{};
  };
  inspector_.check_relationship = [this](const dd::host::RelationshipFields& fields) {
    return doc_ ? doc_->problems(fields) : std::vector<dd::editor::FieldProblem>{};
  };
  style_panel_.committed = [this](const dd::host::DocumentFields& fields) {
    std::string before = doc_->text();
    auto op = dd::host::document_operation(doc_->session().model(), doc_->view(), fields);
    if (!op) return apply(op.error(), before);
    if (*op) apply(doc_->apply(**op), before);
  };
  inspector_.committed_relationship = [this](const dd::host::RelationshipFields& fields) {
    std::string before = doc_->text(), renamed;
    apply(doc_->update(fields, &renamed), before);
    // A new name is a new id: keep the line selected under it.
    if (renamed != fields.id) {
      canvas_.canvas().set_selection({{}, {renamed}});
      canvas_.redraw();
      inspect();
    }
  };
  // Text | canvas | panel with a movable divider between each two, as the macOS app's split view:
  // the text starts at 440 and the panel at 280, neither shrinks below what its contents need, and
  // the canvas takes whatever the window gains or loses.
  auto canvas_side = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
  canvas_side->set_start_child(*canvas_scroll);
  canvas_side->set_end_child(inspector_scroll_);
  canvas_side->set_resize_end_child(false);
  canvas_side->set_shrink_end_child(false);
  style_divider(*canvas_side);
  // The position is the canvas's width; once the paned has a width, leave the panel 280 of it.
  auto sized = std::make_shared<sigc::connection>();
  *sized = canvas_side->property_max_position().signal_changed().connect([canvas_side, sized] {
    int width = canvas_side->get_width();
    if (width <= 0) return;
    canvas_side->set_position(
        std::clamp(width - kDividerWidth - 280, 0, canvas_side->property_max_position().get_value()));
    sized->disconnect();
  });

  auto paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
  paned->set_start_child(*text_scroll);
  paned->set_end_child(*canvas_side);
  paned->set_position(440);
  paned->set_vexpand(true);
  paned->set_resize_start_child(false);
  paned->set_shrink_start_child(false);
  paned->set_shrink_end_child(false);
  style_divider(*paned);

  status_.set_xalign(0);
  status_.set_ellipsize(Pango::EllipsizeMode::END);
  status_.set_margin_start(10);
  status_.set_margin_end(10);
  status_.set_margin_top(4);
  status_.set_margin_bottom(4);
  status_.add_css_class("caption");

  auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  box->append(*paned);
  box->append(status_);
  set_child(*box);

  // Canvas gestures become document changes.
  auto& c = canvas_.canvas().callbacks;
  c.selected = [this](const dd::editor::Selection& s) {
    inspect();
    if (s.elements.size() != 1) return;
    auto span = doc_->declaration_of(s.elements[0]);
    if (!span) return;
    const std::string& text = doc_->text();
    auto from = iter_at_byte(text, span->from), to = iter_at_byte(text, span->to);
    buffer_->select_range(from, to);
    text_.scroll_to(from);
  };
  auto run = [this](auto f) {
    std::string before = doc_->text();
    apply(f(), before);
  };
  c.dropped = [this, run](const std::string& id, const dd::Box& box, bool freeform) {
    run([&] { return doc_->drop(id, box, freeform); });
  };
  c.moved = [this, run](const std::vector<std::string>& ids, const dd::Point& offset) {
    run([&] { return doc_->move(ids, offset); });
  };
  c.resized = [this, run](const std::string& id, const dd::Box& box) { run([&] { return doc_->resize(id, box); }); };
  c.fitted = [this, run](const std::string& id) { run([&] { return doc_->fit({id}); }); };
  c.rotated = [this, run](const std::string& id, double degrees) { run([&] { return doc_->rotate(id, degrees); }); };
  c.rerouted = [this, run](const std::string& id, const std::vector<dd::Point>& points) {
    run([&] { return doc_->reroute(id, points); });
  };
  c.connected = [this, run](const std::string& a, const std::string& b) { run([&] { return doc_->connect(a, b); }); };
  c.reconnected = [this](const std::string& id, dd::editor::End end, const std::string& element) {
    std::string after, before = doc_->text();
    apply(doc_->reconnect(id, end, element, &after), before);
    // The line stays selected under its new id (a derived id follows its endpoints).
    if (!after.empty()) {
      canvas_.canvas().set_selection({{}, {after}});
      canvas_.redraw();
      inspect();
    }
  };
  c.renamed = [this, run](const std::string& id, const std::string& name) { run([&] { return doc_->rename(id, name); }); };
  c.relabelled = [this, run](const std::string& id, const std::optional<std::string>& label) {
    run([&] { return doc_->relabel(id, label); });
  };
  c.deleted = [this, run](const dd::editor::Selection& s) { run([&] { return doc_->remove(s); }); };
}

void DocumentWindow::load(const std::string& text) {
  std::string file = path_.empty() ? Glib::get_current_dir() + "/untitled.draw" : path_;
  doc_ = std::make_unique<dd::host::Document>(text, file);
  doc_->attach(canvas_.canvas());
  applying_ = true;
  buffer_->begin_irreversible_action();
  buffer_->set_text(text);
  buffer_->end_irreversible_action();
  buffer_->set_modified(false);
  buffer_->place_cursor(buffer_->begin());
  applying_ = false;
  update_title();
  refresh();
}

Gtk::TextIter DocumentWindow::iter_at_byte(const std::string& text, std::size_t byte) {
  int chars = 0;
  for (std::size_t i = 0; i < std::min(byte, text.size()); ++i)
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++chars;
  return buffer_->get_iter_at_offset(chars);
}

void DocumentWindow::sync_from_text() {
  timer_.disconnect();
  doc_->set_text(buffer_->get_text(true));
  refresh();
}

void DocumentWindow::refresh() {
  auto refreshed = doc_->refresh();
  updating_views_ = true;
  std::vector<Glib::ustring> names;
  for (const auto& id : doc_->views()) names.push_back("view " + id);
  view_names_->splice(0, view_names_->get_n_items(), names);
  const auto& views = doc_->views();
  auto it = std::find(views.begin(), views.end(), doc_->view());
  if (it != views.end()) views_.set_selected(static_cast<guint>(it - views.begin()));
  views_.set_sensitive(views.size() > 1);  // nothing to choose between with one view
  remove_view_action_->set_enabled(views.size() > 1);  // a document keeps one view
  updating_views_ = false;
  if (refreshed.render) canvas_.show_render(*refreshed.render);
  else canvas_.clear();
  inspect();

  // Diagnostics: underlined in the text, the one under the caret named in the status line.
  marks_.clear();
  buffer_->remove_tag_by_name("dd-error", buffer_->begin(), buffer_->end());
  buffer_->remove_tag_by_name("dd-warning", buffer_->begin(), buffer_->end());
  const std::string& text = doc_->text();
  if (buffer_->get_text(true).raw() == text) {
    auto map = char_map(text);
    int length = buffer_->get_char_count();
    for (const auto& d : doc_->session().parsed().diagnostics) {
      int from = map[std::min<std::size_t>(d.span.from, text.size())];
      int to = map[std::min<std::size_t>(std::max(d.span.to, d.span.from), text.size())];
      if (to == from) {
        if (to < length) ++to;
        else if (from > 0) --from;
      }
      bool error = d.severity == dd::dsl::Severity::Error;
      buffer_->apply_tag_by_name(error ? "dd-error" : "dd-warning", buffer_->get_iter_at_offset(from),
                                 buffer_->get_iter_at_offset(to));
      marks_.push_back({from, to, error, d.message});
    }
  }
  summary_ = refreshed.status;
  summary_error_ = refreshed.error;
  showing_mark_ = false;
  flash(summary_, summary_error_);
  show_caret_diagnostic();
}

void DocumentWindow::restyle() {
  for (const char* tag : {"dd-comment", "dd-string", "dd-number", "dd-keyword", "dd-kind", "dd-definition",
                          "dd-punctuation"})
    buffer_->remove_tag_by_name(tag, buffer_->begin(), buffer_->end());
  std::string text = buffer_->get_text(true);
  auto map = char_map(text);
  for (const auto& t : dd::dsl::highlight(text))
    if (const char* tag = tag_name(t.kind))
      buffer_->apply_tag_by_name(tag, buffer_->get_iter_at_offset(map[t.from]), buffer_->get_iter_at_offset(map[t.to]));
}

void DocumentWindow::show_caret_diagnostic() {
  int caret = buffer_->property_cursor_position();
  for (const auto& m : marks_)
    if (caret >= m.from && caret <= m.to) {
      flash((m.error ? "error: " : "warning: ") + m.message, true);
      showing_mark_ = true;
      return;
    }
  if (showing_mark_) flash(summary_, summary_error_);
  showing_mark_ = false;
}

void DocumentWindow::flash(const std::string& message, bool error) {
  status_.set_text(message);
  if (error) status_.add_css_class("error");
  else status_.remove_css_class("error");
}

void DocumentWindow::apply(const Result<std::optional<dd::host::Change>>& result, const std::string& before) {
  if (!result) return flash(result.error().message, true);
  if (!*result) return;
  applying_ = true;
  buffer_->begin_user_action();
  for (const auto& e : (*result)->edits) {  // last first, so earlier offsets stay valid
    auto from = iter_at_byte(before, e.from), to = iter_at_byte(before, e.to);
    auto at = buffer_->erase(from, to);
    buffer_->insert(at, e.insert);
  }
  buffer_->end_user_action();
  applying_ = false;
  refresh();
  if ((*result)->note) flash(*(*result)->note);
}

void DocumentWindow::add_element(const std::string& kind) {
  std::string created, before = doc_->text();
  apply(doc_->add(kind, &created), before);
  if (!created.empty()) {
    canvas_.canvas().set_selection({{created}, {}});
    canvas_.redraw();
    inspect();
  }
}

void DocumentWindow::new_view(const std::string& id) {
  std::string before = doc_->text();
  apply(doc_->add_view(id), before);
}

void DocumentWindow::rename_view(const std::string& id) {
  std::string before = doc_->text();
  apply(doc_->rename_view(id), before);
}

void DocumentWindow::rename_element(const std::string& element, const std::string& to) {
  std::string before = doc_->text(), renamed;
  auto selection = canvas_.canvas().selection();
  apply(doc_->rename_element(element, to, &renamed), before);
  if (renamed == element) return;
  selection = doc_->follow_rename(std::move(selection), element, renamed);
  canvas_.canvas().set_selection(selection);
  canvas_.redraw();
  inspect();
}

std::size_t DocumentWindow::cursor_byte() {
  // A text change still waiting for its sync goes in first, so the cursor and the parse agree.
  if (timer_.connected()) sync_from_text();
  auto cursor = buffer_->get_iter_at_mark(buffer_->get_insert());
  return buffer_->get_text(buffer_->begin(), cursor, true).bytes();
}

std::optional<std::string> DocumentWindow::element_to_rename() {
  const auto* root = get_root();
  const Gtk::Widget* focus = root ? root->get_focus() : nullptr;
  bool in_text = focus && (focus == &text_ || focus->is_ancestor(text_));
  std::size_t caret = cursor_byte();
  return doc_->element_to_rename(canvas_.canvas().selection(), in_text, caret);
}

void DocumentWindow::ask_rename_element() {
  auto element = element_to_rename();
  if (!element) {
    flash("Put the cursor on an element's name, or select one element, to rename it", true);
    return;
  }
  auto fields = doc_->fields(*element);
  if (!fields) return;
  ask_name("Rename Element", "New name for " + *element + " (letters, digits and _):", "Rename", fields->local_id,
           [this, id = *element](const std::string& to) { rename_element(id, to); });
}

void DocumentWindow::remove_view() {
  std::string before = doc_->text();
  apply(doc_->remove_view(), before);
}

void DocumentWindow::ask_clear_canvas() {
  if (timer_.connected()) sync_from_text();
  // Saved or untouched, undo is enough; an empty diagram has nothing to clear.
  if (!buffer_->get_modified() || doc_->text() == dd::host::Document::kEmptyText) return clear_canvas();
  auto alert = Gtk::AlertDialog::create("Clear the canvas?");
  alert->set_detail(std::string(dd::host::Document::kClearDetail));
  alert->set_buttons({"Cancel", "Clear"});
  alert->set_cancel_button(0);
  alert->set_default_button(0);  // Return keeps the changes, as cloud_dialog.cpp's Delete does
  alert->choose(*this, [this, alert](const Glib::RefPtr<Gio::AsyncResult>& answer) {
    try {
      if (alert->choose_finish(answer) == 1) clear_canvas();
    } catch (const Glib::Error&) {
      // dismissed
    }
  });
}

void DocumentWindow::clear_canvas() {
  if (timer_.connected()) sync_from_text();  // the change replaces the whole text, typing included
  std::string before = doc_->text();
  apply(doc_->clear(), before);  // the refresh drops the selection, as nothing selected is left
}

void DocumentWindow::ask_name(const std::string& title, const std::string& prompt, const std::string& verb,
                              const std::string& initial, std::function<void(const std::string&)> done) {
  prompt_ = std::make_unique<Gtk::Window>();
  prompt_->set_title(title);
  prompt_->set_transient_for(*this);
  prompt_->set_modal(true);
  prompt_->set_resizable(false);
  prompt_->set_hide_on_close(true);
  auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(12);
  auto label = Gtk::make_managed<Gtk::Label>(prompt);
  label->set_halign(Gtk::Align::START);
  auto entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(initial);
  entry->set_activates_default(true);
  auto buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  buttons->set_halign(Gtk::Align::END);
  auto cancel = Gtk::make_managed<Gtk::Button>("Cancel");
  auto ok = Gtk::make_managed<Gtk::Button>(verb);
  ok->add_css_class("suggested-action");
  buttons->append(*cancel);
  buttons->append(*ok);
  box->append(*label);
  box->append(*entry);
  box->append(*buttons);
  prompt_->set_child(*box);
  prompt_->set_default_widget(*ok);
  cancel->signal_clicked().connect([this] { prompt_->hide(); });
  // Escape cancels, as in the other shells' dialogs.
  auto keys = Gtk::ShortcutController::create();
  keys->add_shortcut(Gtk::Shortcut::create(Gtk::KeyvalTrigger::create(GDK_KEY_Escape),
                                           Gtk::CallbackAction::create([this](Gtk::Widget&, const Glib::VariantBase&) {
                                             prompt_->hide();
                                             return true;
                                           })));
  prompt_->add_controller(keys);
  ok->signal_clicked().connect([this, entry, done = std::move(done)] {
    std::string name = entry->get_text();
    prompt_->hide();
    done(name);
  });
  prompt_->present();
  entry->grab_focus();
}

void DocumentWindow::inspect() {
  const auto& selection = canvas_.canvas().selection();
  std::optional<dd::host::ElementFields> element;
  std::optional<dd::host::RelationshipFields> relationship;
  if (selection.elements.size() == 1 && selection.relationships.empty())
    element = doc_->fields(selection.elements[0]);
  else if (selection.relationships.size() == 1 && selection.elements.empty())
    relationship = doc_->relationship_fields(selection.relationships[0]);
  if (element || relationship) inspector_.set_choices(doc_->choices());
  if (relationship)
    inspector_.show_fields(relationship);
  else
    inspector_.show_fields(element);
  std::size_t selected = selection.elements.size() + selection.relationships.size();
  std::optional<dd::host::DocumentFields> document;
  if (selected == 0) {
    document = dd::host::document_fields(doc_->session().model(), doc_->view());
    style_panel_.set_choices(doc_->choices());
  }
  style_panel_.show_fields(document);
  bool note = !element && !relationship && !document;
  panel_note_.set_visible(note);
  if (note)
    panel_note_.set_text(selected > 1 ? std::to_string(selected) +
                                            " items selected. Select one element or relationship to "
                                            "edit its properties."
                                      : "Nothing to edit here.");
}

void DocumentWindow::undo() {
  if (buffer_->get_can_undo()) buffer_->undo();
  sync_from_text();
}

void DocumentWindow::redo() {
  if (buffer_->get_can_redo()) buffer_->redo();
  sync_from_text();
}

void DocumentWindow::zoom_by(double factor) { canvas_.set_zoom(canvas_.zoom() * factor); }
void DocumentWindow::zoom_actual() { canvas_.set_zoom(1); }

void DocumentWindow::update_title() {
  std::string name = cloud_ ? cloud_->name + " (" + std::string(dd::host::cloud::label(cloud_->provider)) + ")"
                     : path_.empty() ? "Untitled"
                                     : Glib::path_get_basename(path_);
  set_title((buffer_->get_modified() ? "• " : "") + name + " — DiaDraw");
}

void DocumentWindow::write_to(const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  out << buffer_->get_text(true);
  if (!out) return flash("Could not save " + path, true);
  path_ = path;
  cloud_.reset();
  buffer_->set_modified(false);
  update_title();
}

void DocumentWindow::save() {
  if (cloud_) {
    namespace cloud = dd::host::cloud;
    std::weak_ptr<bool> alive = alive_;
    // Keep the app running until the save is done, even if the last window closes meanwhile.
    Glib::RefPtr<Gio::Application> app = Gio::Application::get_default();
    if (app) app->hold();
    flash("Saving to " + std::string(cloud::label(cloud_->provider)) + "…");
    cloud_saver_.save(post_to_main, *cloud_, buffer_->get_text(true),
                      [this, alive, app](const cloud::SaveOutcome& outcome) {
                        if (app) app->release();
                        if (alive.expired()) return;
                        // The sign-in ran out: sign in from the dialog, then save again.
                        if (outcome.needs_sign_in) return show_cloud(outcome.place.provider, outcome.message);
                        if (!outcome.ok) return flash(outcome.message, true);
                        // Typing meanwhile, or a move to another place, keeps the document modified.
                        if (cloud_ && cloud_->provider == outcome.place.provider && cloud_->id == outcome.place.id &&
                            std::string(buffer_->get_text(true)) == outcome.text)
                          buffer_->set_modified(false);
                        update_title();
                        flash(outcome.message);
                      });
    return;
  }
  if (path_.empty()) return save_as();
  write_to(path_);
}

void DocumentWindow::set_cloud(const dd::host::cloud::Place& place, const std::string& text) {
  cloud_ = place;
  last_provider_ = place.provider;
  path_.clear();
  if (std::string(buffer_->get_text(true)) == text) buffer_->set_modified(false);
  update_title();
}

void DocumentWindow::show_cloud(std::optional<dd::host::cloud::Provider> provider, const std::string& message) {
  namespace cloud = dd::host::cloud;
  if (!cloud_dialog_) {
    CloudDialog::Callbacks callbacks;
    callbacks.text = [this] { return std::string(buffer_->get_text(true)); };
    callbacks.suggested_name = [this] {
      return cloud_ ? cloud::plain_name(cloud_->name)
                    : path_.empty() ? std::string("Untitled") : cloud::plain_name(Glib::path_get_basename(path_));
    };
    callbacks.editing = [this] { return cloud_; };
    callbacks.saved = [this](const dd::host::cloud::Place& place, const std::string& text) {
      set_cloud(place, text);
      flash("Saved " + place.name + " in " + std::string(cloud::label(place.provider)));
    };
    callbacks.opened = [this](const dd::host::cloud::Place& place, const std::string& text) {
      last_provider_ = place.provider;
      // An untouched new window takes the diagram; otherwise it opens in a window of its own, as
      // Open does.
      DocumentWindow* target = this;
      if (!path_.empty() || cloud_ || buffer_->get_modified()) target = open_window ? open_window("", text) : nullptr;
      else load(text);
      if (target) target->set_cloud(place, text);
    };
    callbacks.renamed = [this](const dd::host::cloud::Place& place) {
      if (cloud_ && cloud_->provider == place.provider && cloud_->id == place.id) {
        cloud_->name = place.name;
        update_title();
      }
    };
    callbacks.removed = [this](const dd::host::cloud::Place& place) {
      if (cloud_ && cloud_->provider == place.provider && cloud_->id == place.id) {
        // The text stays, unsaved: saving again asks where.
        cloud_.reset();
        buffer_->set_modified(true);
        update_title();
      }
    };
    cloud_dialog_ = std::make_unique<CloudDialog>(*this, std::move(callbacks));
  }
  const auto chosen = provider ? *provider : cloud_ ? cloud_->provider : last_provider_;
  last_provider_ = chosen;
  cloud_dialog_->show_for(chosen, message);
}

void DocumentWindow::save_as() {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_initial_name(path_.empty() ? "untitled.draw" : Glib::path_get_basename(path_));
  dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
      if (auto file = dialog->save_finish(result)) write_to(file->get_path());
    } catch (const Glib::Error&) {
      // cancelled
    }
  });
}

void DocumentWindow::export_as(const std::string& extension) {
  Result<std::string> data = extension == "svg"      ? doc_->svg()
                             : extension == "png"    ? doc_->png()
                             : extension == "pdf"    ? doc_->pdf()
                             : extension == "drawio" ? Result<std::string>(doc_->drawio())
                                                     : Result<std::string>(doc_->bpmn());
  if (!data) return flash(data.error().message, true);
  std::string base = path_.empty() ? "untitled" : Glib::path_get_basename(path_);
  if (auto dot = base.rfind('.'); dot != std::string::npos) base.resize(dot);
  auto dialog = Gtk::FileDialog::create();
  dialog->set_initial_name(base + "." + extension);
  auto bytes = std::make_shared<std::string>(std::move(*data));
  dialog->save(*this, [this, dialog, bytes](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
      if (auto file = dialog->save_finish(result)) {
        std::ofstream out(file->get_path(), std::ios::binary);
        out << *bytes;
        if (!out) flash("Could not write " + file->get_path(), true);
      }
    } catch (const Glib::Error&) {
      // cancelled
    }
  });
}

// ── Scripted tests ──

std::string DocumentWindow::run_script(const std::string& script) {
  std::ostringstream log;
  sync_from_text();
  canvas_.scripted = true;
  std::istringstream lines(script);
  std::string raw;
  while (std::getline(lines, raw)) {
    std::istringstream words(raw);
    std::vector<std::string> w;
    for (std::string word; words >> word;) w.push_back(word);
    if (w.empty() || w[0][0] == '#') continue;
    auto rest = [&](std::size_t from) {
      std::string out;
      for (std::size_t i = from; i < w.size(); ++i) out += (i > from ? " " : "") + w[i];
      return out;
    };
    auto& c = canvas_.canvas();
    auto click = [&](dd::draw::Point p, int clicks = 1) {
      canvas_.press(p.x, p.y, clicks, {});
      if (clicks == 1) canvas_.release(p.x, p.y, {});
    };
    auto drag = [&](dd::draw::Point a, dd::draw::Point b, dd::canvas::Modifiers m) {
      canvas_.press(a.x, a.y, 1, m);
      for (int i = 1; i <= 5; ++i) canvas_.drag_to(a.x + (b.x - a.x) * i / 5, a.y + (b.y - a.y) * i / 5, m);
      canvas_.release(b.x, b.y, m);
    };
    const std::string& cmd = w[0];
    bool ok = true;
    if (cmd == "view" && w.size() > 1) {
      doc_->choose_view(w[1]);
      refresh();
    } else if (cmd == "select" && w.size() > 1) {
      // An element by its id, or a relationship (clicked on the middle of its line).
      auto p = c.center_of(w[1]);
      if (!p) p = c.label_point(w[1]);
      if ((ok = p.has_value())) click(*p);
    } else if (cmd == "resize" && w.size() > 3) {
      // resize <id> <dx> <dy>: select it and drag its bottom-right corner, in diagram units.
      auto p = c.center_of(w[1]);
      if ((ok = p.has_value())) {
        click(*p);
        auto corners = c.resize_points();
        if ((ok = corners.size() > 2)) {
          double scale = 1 / c.units_per_pixel();
          dd::draw::Point a = corners[2];
          drag(a, {a.x + std::stod(w[2]) * scale, a.y + std::stod(w[3]) * scale}, {});
        }
      }
    } else if (cmd == "fit" && w.size() > 1) {
      // fit <id>: select it and double-click its top-left resize corner, as the toolkit reports
      // a double-click: two single clicks, then the double-click.
      auto p = c.center_of(w[1]);
      if ((ok = p.has_value())) {
        click(*p);
        auto corners = c.resize_points();
        if ((ok = !corners.empty())) {
          click(corners[0]);
          click(corners[0]);
          click(corners[0], 2);
        }
      }
    } else if (cmd == "drag" && w.size() > 3) {
      auto p = c.center_of(w[1]);
      if ((ok = p.has_value())) {
        double scale = 1 / c.units_per_pixel();
        drag(*p, {p->x + std::stod(w[2]) * scale, p->y + std::stod(w[3]) * scale}, {false, w.size() > 4});
      }
    } else if (cmd == "connect" && w.size() > 2) {
      auto a = c.center_of(w[1]);
      if ((ok = a.has_value())) {
        click(*a);
        auto h = c.handle_point();
        auto b = c.center_of(w[2]);
        if ((ok = h && b)) drag(*h, *b, {});
      }
    } else if (cmd == "reconnect" && w.size() > 3) {
      // reconnect <relationship> source|target <element>: select the line, drag that end.
      auto l = c.label_point(w[1]);
      auto end = w[2] == "source" ? dd::editor::End::Source : dd::editor::End::Target;
      if ((ok = l.has_value() && (w[2] == "source" || w[2] == "target"))) {
        click(*l);
        auto a = c.end_point(end);
        auto b = c.center_of(w[3]);
        if ((ok = a && b)) drag(*a, *b, {});
      }
    } else if ((cmd == "rename" || cmd == "relabel") && w.size() > 1) {
      auto p = cmd == "rename" ? c.center_of(w[1]) : c.label_point(w[1]);
      if ((ok = p.has_value())) {
        click(*p, 2);
        ok = canvas_.commit_editing(rest(2));
      }
    } else if (cmd == "add-select" && w.size() > 1) {
      // add-select <id>: Ctrl-click, adding an element or relationship to the selection (or taking it out)
      auto p = c.center_of(w[1]);
      if (!p) p = c.label_point(w[1]);
      if ((ok = p.has_value())) {
        canvas_.press(p->x, p->y, 1, {.toggle = true});
        canvas_.release(p->x, p->y, {.toggle = true});
      }
    } else if (cmd == "band" && w.size() > 4) {
      // band <x1> <y1> <x2> <y2> [shift]: drags a selection band between two points in diagram units
      auto a = c.to_view(std::stod(w[1]), std::stod(w[2]));
      auto b = c.to_view(std::stod(w[3]), std::stod(w[4]));
      drag(a, b, {.shift = w.size() > 5});
    } else if (cmd == "select-all") {
      if (c.select_all()) canvas_.redraw();
    } else if (cmd == "delete-selection") {
      canvas_.delete_selection();
    } else if (cmd == "selection") {
      // selection: logs what is selected
      log << "selected:";
      for (const auto& id : c.selection().elements) log << " " << id;
      for (const auto& id : c.selection().relationships) log << " " << id;
      log << "\n";
    } else if (cmd == "delete" && w.size() > 1) {
      auto p = c.center_of(w[1]);
      if ((ok = p.has_value())) {
        click(*p);
        canvas_.delete_selection();
      }
    } else if (cmd == "set" && w.size() > 1) {
      // set <field> <value…>: types into the inspector of the selected element or relationship, or
      // into the theme and styles panel when nothing is selected; `\n` is a new line.
      std::string value = rest(2);
      for (std::size_t at; (at = value.find("\\n")) != std::string::npos;) value.replace(at, 2, "\n");
      ok = inspector_.get_visible() ? inspector_.set_field(w[1], value) : style_panel_.set_field(w[1], value);
    } else if (cmd == "enter" && w.size() > 1) {
      // enter <field> <value…>: types into an inspector field without committing it
      ok = inspector_.type_field(w[1], rest(2));
    } else if (cmd == "invalid") {
      // invalid: logs the inspector fields outlined as invalid, with why
      log << "invalid:";
      for (const auto& p : inspector_.invalid_fields()) log << " " << p.field;
      log << "\n";
      for (const auto& p : inspector_.invalid_fields()) log << "  " << p.field << ": " << p.message << "\n";
    } else if (cmd == "styles") {
      // styles: clears the selection, so the panel shows the document's theme and styles.
      c.set_selection({});
      canvas_.redraw();
      inspect();
    } else if (cmd == "grid" && w.size() > 1) {
      // grid <step>: shows the grid with that step in diagram units (0 hides it)
      c.set_grid(std::stod(w[1]));
      grid_action_->change_state(c.grid() > 0);  // keeps Show Grid's check mark and next toggle right
      canvas_.redraw();
    } else if (cmd == "add" && w.size() > 1) {
      const auto& kinds = dd::host::element_kinds();
      auto it = std::find(kinds.begin(), kinds.end(), w[1]);
      if ((ok = it != kinds.end())) add_element(w[1]);
    } else if (cmd == "new-view" && w.size() > 1) {
      new_view(w[1]);
    } else if ((cmd == "rename-at" || cmd == "rename-selected") && w.size() > 1) {
      // rename-at <name>: Rename Element… in the text, on the element named at the cursor;
      // rename-selected <name>: on the canvas, on the one selected element
      auto element = cmd == "rename-at" ? doc_->element_at(cursor_byte())
                                        : doc_->element_to_rename(canvas_.canvas().selection(), false,
                                                                  dd::host::Document::kNoCaret);
      if ((ok = element.has_value())) rename_element(*element, rest(1));
    } else if (cmd == "rename-view" && w.size() > 1) {
      rename_view(w[1]);
    } else if (cmd == "remove-view") {
      remove_view();
    } else if (cmd == "clear-canvas") {
      clear_canvas();
    } else if (cmd == "views") {
      // views: logs the views, the one on the canvas marked with *
      log << "views:";
      for (const auto& id : doc_->views()) log << " " << id << (id == doc_->view() ? "*" : "");
      log << "\n";
    } else if (cmd == "undo") {
      undo();
    } else if (cmd == "redo") {
      redo();
    } else if (cmd == "type" && w.size() > 1) {
      // Appends a line at the end of the text, as typing would.
      buffer_->begin_user_action();
      auto end = buffer_->end();
      buffer_->insert(end, rest(1) + "\n");
      buffer_->end_user_action();
      sync_from_text();
    } else if (cmd == "caret" && w.size() > 2) {
      auto line = buffer_->get_iter_at_line(std::stoi(w[1]) - 1);
      line.forward_chars(std::stoi(w[2]) - 1);
      buffer_->place_cursor(line);
      text_.grab_focus();
    } else {
      ok = false;
    }
    log << (ok ? "ok   " : "FAIL ") << raw << "\n";
  }
  canvas_.scripted = false;
  log << "--- text\n" << buffer_->get_text(true) << "--- status\n" << status_.get_text() << "\n";
  return log.str();
}

}  // namespace dd::gtk
