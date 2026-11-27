// A window on one .draw file: the text on the left, the canvas on the right, a status line below.
// The text is the source of truth. Typing updates the shared document (dd::host::Document);
// canvas gestures come back from it as text edits applied to the buffer as one undo step, so a
// single undo history covers both panes.

#pragma once

#include <gtkmm.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "canvas_view.hpp"
#include "cloud_dialog.hpp"
#include "inspector.hpp"
#include "style_panel.hpp"
#include "text_folds.hpp"
#include "host/document.hpp"

namespace dd::gtk {

class DocumentWindow : public Gtk::ApplicationWindow {
 public:
  /// `path` is the file (empty for a new document); `text` its contents.
  DocumentWindow(const Glib::RefPtr<Gtk::Application>& app, std::string path, const std::string& text);

  [[nodiscard]] const std::string& path() const { return path_; }
  /// Shows a message in the status line until the next change (e.g. an import report).
  void flash(const std::string& message, bool error = false);

  void save();
  void save_as();
  /// File › Cloud Storage…: Google Drive and OneDrive, on `provider` (the document's, or the last).
  void show_cloud(std::optional<dd::host::cloud::Provider> provider = {}, const std::string& message = {});
  /// Makes the document the diagram at `place`, last saved as `text` (an open or a Save there).
  void set_cloud(const dd::host::cloud::Place& place, const std::string& text);

  /// Opens a new window on `text` (set by the application, which owns the windows).
  static std::function<DocumentWindow*(const std::string& path, const std::string& text)> open_window;
  void undo();
  void redo();
  void zoom_by(double factor);
  void zoom_actual();
  void export_as(const std::string& extension);
  void add_element(const std::string& kind);
  /// Adds view `id` and shows it; renames or removes the view on the canvas.
  void new_view(const std::string& id);
  void rename_view(const std::string& id);
  void remove_view();
  /// Clear Canvas: makes the diagram empty as one undo step. ask_clear_canvas asks first when
  /// there are unsaved changes.
  void ask_clear_canvas();
  void clear_canvas();
  /// Format Document: reindents the text with the Indent Size setting, as one undo step.
  void format_text();
  /// Format Document's indent size: the one kept in memory, else the settings file's, which
  /// another window may have changed. Moves Indent Size's check mark to it.
  int sync_indent_size();
  /// Renames an element (its id) and every reference to it; the selection follows.
  void rename_element(const std::string& element, const std::string& to);
  /// Rename Element…: asks for a new name for the element named at the text cursor (when the text
  /// has the focus) or the one selected on the canvas.
  void ask_rename_element();

  /// Runs a test script (see DIADRAW_SCRIPT in main.cpp) and returns its log.
  std::string run_script(const std::string& script);

 private:
  void build();
  void load(const std::string& text);
  void sync_from_text();
  void refresh();
  /// Shows the one selected element or relationship in the inspector, or the document's theme and
  /// styles when nothing is selected and the Styles button is on.
  void inspect();
  void restyle();
  void show_caret_diagnostic();
  void apply(const Result<std::optional<dd::host::Change>>& result, const std::string& before);
  void update_title();
  /// Asks for a name in a small dialog, starting from `initial`; `done` gets what was typed.
  void ask_name(const std::string& title, const std::string& prompt, const std::string& verb,
                const std::string& initial, std::function<void(const std::string&)> done);
  /// The element Rename Element… renames: the one named at the text cursor when the text has the
  /// focus, otherwise the one selected on the canvas, or with none the one at the text cursor.
  [[nodiscard]] std::optional<std::string> element_to_rename();
  /// The text cursor in UTF-8 bytes, once a pending text change is synced.
  [[nodiscard]] std::size_t cursor_byte();
  void write_to(const std::string& path);
  Gtk::TextIter iter_at_byte(const std::string& text, std::size_t byte);

  struct Mark {
    int from = 0, to = 0;  ///< character offsets
    bool error = true;
    std::string message;
  };

  std::string path_;
  /// Where the document lives when it came from, or was saved to, Google Drive or OneDrive.
  std::optional<dd::host::cloud::Place> cloud_;
  dd::host::cloud::Saver cloud_saver_;
  std::unique_ptr<CloudDialog> cloud_dialog_;
  dd::host::cloud::Provider last_provider_ = dd::host::cloud::Provider::GoogleDrive;
  /// Background saves check this before touching the window.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::unique_ptr<dd::host::Document> doc_;
  Glib::RefPtr<Gtk::TextBuffer> buffer_;
  Gtk::TextView text_;
  /// The fold markers beside the text; made once the text has its buffer.
  std::unique_ptr<TextFolds> folds_;
  CanvasView canvas_;
  Inspector inspector_;
  StylePanel style_panel_;
  /// What the properties panel says while several items are selected.
  Gtk::Label panel_note_;
  Gtk::ScrolledWindow inspector_scroll_;
  Gtk::DropDown views_;
  Glib::RefPtr<Gtk::StringList> view_names_;
  Gtk::Label status_;
  sigc::connection timer_;
  Glib::RefPtr<Gio::SimpleAction> grid_action_;
  Glib::RefPtr<Gio::SimpleAction> indent_action_;
  /// The indent size a test script chose (`indent`), or one the settings file could not keep;
  /// 0 to follow the settings file, as a size chosen from the menu and saved does again.
  int kept_indent_ = 0;
  Glib::RefPtr<Gio::SimpleAction> remove_view_action_;
  std::unique_ptr<Gtk::Window> prompt_;
  bool applying_ = false;
  bool updating_views_ = false;
  std::vector<Mark> marks_;
  std::string summary_;
  bool summary_error_ = false;
  bool showing_mark_ = false;
};

}  // namespace dd::gtk
