// DiaDraw for Linux: a gtkmm 4 shell over the C++ core. It also builds on macOS with Homebrew's
// gtkmm4, which is how it is tested there.
//
// Test hooks (environment):
//   DIADRAW_SCRIPT=<file>    runs canvas gestures from the file, writes <file>.log and quits
//   DIADRAW_SNAPSHOT=<png>   writes the window to a PNG and quits
// In a test run the app never takes focus and ignores real input, so it cannot steal keystrokes.

#include <gtkmm.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

#include "diadraw/diadraw.h"
#include "host/about.hpp"
#include "host/canvas_help.hpp"
#include "host/credits.hpp"
#include "host/document.hpp"
#include "host/examples.hpp"
#include "host/feedback.hpp"
#include "host/libraries.hpp"
#include "window.hpp"

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace {

bool test_run() { return std::getenv("DIADRAW_SCRIPT") || std::getenv("DIADRAW_SNAPSHOT"); }

/// macOS only: run as a background app that does not activate (GDK makes it a regular one).
void stay_in_background() {
#ifdef __APPLE__
  using Send = id (*)(id, SEL);
  id app = reinterpret_cast<Send>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSApplication")),
                                                sel_registerName("sharedApplication"));
  reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 1);
#endif
}

/// Claims every real key press and click before any widget sees it.
void ignore_input(Gtk::Window& window) {
  auto keys = Gtk::EventControllerKey::create();
  keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  keys->signal_key_pressed().connect([](guint, guint, Gdk::ModifierType) { return true; }, false);
  window.add_controller(keys);
  auto clicks = Gtk::GestureClick::create();
  clicks->set_button(0);
  clicks->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  clicks->signal_pressed().connect(
      [clicks](int, double, double) { clicks->set_state(Gtk::EventSequenceState::CLAIMED); });
  window.add_controller(clicks);
}

void write_snapshot(Gtk::Window& window, const std::string& path) {
  GtkWidget* widget = GTK_WIDGET(window.gobj());
  int w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
  GdkPaintable* paintable = gtk_widget_paintable_new(widget);
  GtkSnapshot* snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), w, h);
  GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
  if (node) {
    GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(w), static_cast<float>(h));
    GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &bounds);
    gdk_texture_save_to_png(texture, path.c_str());
    g_object_unref(texture);
    gsk_render_node_unref(node);
  }
  g_object_unref(paintable);
}

class App : public Gtk::Application {
 public:
  App() : Gtk::Application("com.diadraw.DiaDraw", Gio::Application::Flags::HANDLES_OPEN | Gio::Application::Flags::NON_UNIQUE) {}

 protected:
  void on_startup() override {
    Gtk::Application::on_startup();
    if (test_run()) stay_in_background();
    add_action("new", [this] { open_window("", ""); });
    // A diagram opened from Google Drive or OneDrive into a new window.
    dd::gtk::DocumentWindow::open_window = [this](const std::string& path, const std::string& text) {
      return open_window(path, text);
    };
    add_action("open", sigc::mem_fun(*this, &App::choose_and_open));
    add_action("import", sigc::mem_fun(*this, &App::choose_and_import));
    // File › Examples: the example opens as a new, untitled document.
    add_action_with_parameter("open-example", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& name) {
      auto value = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(name).get();
      if (const auto* example = dd::host::find_example(value.raw())) open_window("", std::string(example->source));
    });
    add_action("quit", [this] { quit(); });
    add_action("report-bug", [this] { open_feedback(dd::host::FeedbackKind::Bug); });
    add_action("send-feedback", [this] { open_feedback(dd::host::FeedbackKind::Feedback); });
    add_action("about", sigc::mem_fun(*this, &App::show_about));
    add_action("help", sigc::mem_fun(*this, &App::show_help));
    set_accel_for_action("app.new", "<Primary>n");
    set_accel_for_action("app.open", "<Primary>o");
    set_accel_for_action("app.import", "<Primary><Shift>i");
    set_accel_for_action("app.quit", "<Primary>q");
    set_accel_for_action("win.save", "<Primary>s");
    set_accel_for_action("win.save-as", "<Primary><Shift>s");
    set_accel_for_action("win.undo", "<Primary>z");
    set_accel_for_action("win.redo", "<Primary><Shift>z");
    set_accel_for_action("win.fit", "<Primary><Alt>equal");
    set_accel_for_action("win.zoom-in", "<Primary>plus");
    set_accel_for_action("win.zoom-out", "<Primary>minus");
    set_accel_for_action("win.zoom-actual", "<Primary>0");
    set_accel_for_action("win.grid", "<Primary>apostrophe");
    set_accel_for_action("win.rename-element", "F2");
    set_accel_for_action("win.format", "<Shift><Alt>f");
    set_accel_for_action("app.help", "F1");
  }

  void on_activate() override { open_window("", ""); }

  void on_open(const type_vec_files& files, const Glib::ustring&) override {
    for (const auto& file : files) open_path(file->get_path());
  }

 private:
  dd::gtk::DocumentWindow* open_window(const std::string& path, const std::string& text) {
    auto window = new dd::gtk::DocumentWindow(Glib::RefPtr<Gtk::Application>(this, [](auto*) {}), path, text);
    window->signal_hide().connect([window] { delete window; });
    if (test_run()) {
      ignore_input(*window);
      schedule_tests(*window);
    }
    window->present();
    return window;
  }

  void open_path(const std::string& path) {
    std::string text = dd::host::read_file(path).value_or("");
    open_window(path, text);
  }

  /// Opens the issue form for `kind` in the browser, with the app, version and system filled in.
  void open_feedback(dd::host::FeedbackKind kind) {
#ifdef __APPLE__
    std::string os = "macOS";
#else
    std::string os = "Linux";
#endif
    if (char* pretty = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME)) {
      os = pretty;
      g_free(pretty);
    }
    std::string url = dd::host::feedback_url(kind, {"Linux (GTK)", os});
    auto launcher = Gtk::UriLauncher::create(url);
    auto done = [this, launcher, url](const Glib::RefPtr<Gio::AsyncResult>& result) {
      try {
        if (launcher->launch_finish(result)) return;
      } catch (const Gtk::DialogError& error) {
        if (error.code() == Gtk::DialogError::DISMISSED) return;  // the app chooser was closed
      } catch (const Glib::Error&) {
      }
      // No browser opened: show the address, with a button that copies it.
      auto alert = Gtk::AlertDialog::create("Could not open a web browser");
      alert->set_detail("Open this address to report it:\n" + url);
      alert->set_buttons({"Copy Address", "Close"});
      alert->set_default_button(0);
      alert->set_cancel_button(1);
      auto chosen = [alert, url](const Glib::RefPtr<Gio::AsyncResult>& answer) {
        try {
          if (alert->choose_finish(answer) == 0) Gdk::Display::get_default()->get_clipboard()->set_text(url);
        } catch (const Glib::Error&) {
        }
      };
      if (auto* parent = get_active_window())
        alert->choose(*parent, chosen);
      else
        alert->choose(chosen);
    };
    if (auto* parent = get_active_window())
      launcher->launch(*parent, done);
    else
      launcher->launch(done);
  }

  /// Help › About DiaDraw: the name, version, project page, licence and the open-source software
  /// the app ships (its Credits page). One dialog, kept for the next time.
  void show_about() {
    if (!about_) {
      about_ = std::make_unique<Gtk::AboutDialog>();
      about_->set_program_name("DiaDraw");
      about_->set_version(dd_version());
      about_->set_comments(dd::host::kSummary);
      about_->set_website(dd::host::kProjectUrl);
      about_->set_copyright(dd::host::kCopyright);
      about_->set_license_type(Gtk::License::MIT_X11);
      // GTK links an entry that ends in an address, showing the text before it.
      std::vector<Glib::ustring> credits;
      for (const auto& credit : dd::host::credits_for("gtk"))
        credits.push_back(dd::host::credit_label(credit) + " " + credit.url);
      credits.push_back(Glib::ustring("Everything DiaDraw uses, and what for ") + dd::host::kCreditsUrl);
      about_->add_credit_section("Open-source software", credits);
      // The installed icon; run from the build tree, the repository's copy.
      auto theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
      if (theme->has_icon("com.diadraw.DiaDraw")) {
        about_->set_logo_icon_name("com.diadraw.DiaDraw");
      } else {
        try {
          about_->set_logo(Gdk::Texture::create_from_filename(DIADRAW_SOURCE_ICON));
        } catch (const Glib::Error&) {  // no logo rather than no dialog
        }
      }
      about_->set_default_size(480, 600);  // room for the credits list
      about_->set_modal(true);
      about_->set_hide_on_close(true);
    }
    if (auto* parent = get_active_window()) about_->set_transient_for(*parent);
    about_->present();
  }

  /// Help: what the canvas does with the mouse and the keyboard (host/canvas_help.hpp), a
  /// heading and a two-column list per section. One dialog, kept for the next time.
  void show_help() {
    if (!help_) {
      help_ = std::make_unique<Gtk::Window>();
      help_->set_title("Mouse and Keyboard");
      // One grid for every section, so that the columns line up: a heading across both, then a
      // row per entry, the keys dimmed beside what they do.
      auto grid = Gtk::make_managed<Gtk::Grid>();
      grid->set_margin(18);
      grid->set_row_spacing(6);
      grid->set_column_spacing(18);
      // Ctrl and Alt everywhere. On macOS (a test build with Homebrew's gtkmm4) the canvas still
      // reads Control, but the menu accelerators (<Primary>) are ⌘ there, so undo, the grid and
      // size to fit show Ctrl though they take ⌘: a known gap of that build.
      constexpr auto keys = dd::host::HelpKeys::pc;
      int row = 0;
      for (const auto& section : dd::host::canvas_help(keys)) {
        auto heading = Gtk::make_managed<Gtk::Label>();
        heading->set_markup("<b>" + Glib::Markup::escape_text(section.title) + "</b>");
        heading->set_xalign(0);
        if (row > 0) heading->set_margin_top(12);
        grid->attach(*heading, 0, row++, 2, 1);
        for (const auto& entry : section.entries) {
          auto gesture = Gtk::make_managed<Gtk::Label>(entry.keys);
          gesture->set_xalign(0);
          gesture->set_wrap(true);
          gesture->set_max_width_chars(28);
          gesture->add_css_class("dim-label");
          auto action = Gtk::make_managed<Gtk::Label>(entry.action);
          action->set_xalign(0);
          action->set_wrap(true);
          action->set_hexpand(true);
          grid->attach(*gesture, 0, row);
          grid->attach(*action, 1, row++);
        }
      }
      auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
      scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
      scroll->set_propagate_natural_height(true);
      scroll->set_max_content_height(640);
      scroll->set_child(*grid);
      help_->set_child(*scroll);
      help_->set_default_size(520, -1);
      help_->set_modal(true);
      help_->set_hide_on_close(true);
      // Escape closes it, as it does the About dialog.
      auto close = Gtk::ShortcutController::create();
      close->add_shortcut(Gtk::Shortcut::create(Gtk::KeyvalTrigger::create(GDK_KEY_Escape),
                                                Gtk::NamedAction::create("window.close")));
      help_->add_controller(close);
    }
    if (auto* parent = get_active_window()) help_->set_transient_for(*parent);
    help_->present();
  }

  void choose_and_open() {
    auto dialog = Gtk::FileDialog::create();
    dialog->open([this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
      try {
        if (auto file = dialog->open_finish(result)) open_path(file->get_path());
      } catch (const Glib::Error&) {
      }
    });
  }

  void choose_and_import() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Import a Mermaid, PlantUML, Structurizr, draw.io or BPMN file");
    dialog->open([this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
      std::string path;
      try {
        if (auto file = dialog->open_finish(result)) path = file->get_path();
      } catch (const Glib::Error&) {
        return;
      }
      if (path.empty()) return;
      auto imported = dd::host::import_file(path);
      if (!imported) {
        auto alert = Gtk::AlertDialog::create(imported.error().message);
        alert->show();
        return;
      }
      auto window = open_window("", imported->text);
      if (!imported->report.empty()) window->flash(imported->report);
    });
  }

  /// The test hooks: a script one second after the window appears, a snapshot after two.
  void schedule_tests(dd::gtk::DocumentWindow& window) {
    const char* snapshot = std::getenv("DIADRAW_SNAPSHOT");
    if (const char* script = std::getenv("DIADRAW_SCRIPT")) {
      std::string path = script;
      Glib::signal_timeout().connect_once(
          [this, &window, path, snapshot] {
            std::string log = window.run_script(dd::host::read_file(path).value_or(""));
            std::ofstream(path + ".log") << log;
            if (!snapshot) quit();
          },
          1000);
    }
    if (snapshot) {
      std::string target = snapshot;
      Glib::signal_timeout().connect_once(
          [this, &window, target] {
            write_snapshot(window, target);
            quit();
          },
          2000);
    }
  }

  std::unique_ptr<Gtk::AboutDialog> about_;
  std::unique_ptr<Gtk::Window> help_;
};

}  // namespace

int main(int argc, char* argv[]) {
  // draw.io's stencil sets are found by the host code: $DIADRAW_DRAWIO, then
  // <prefix>/share/diadraw/drawio when installed, then assets/drawio in the repository.
  // The UI is English; keep the C++ global locale as is (macOS's libc++ lacks some locales).
  Glib::set_init_to_users_preferred_locale(false);
  App app;
  return app.run(argc, argv);
}
