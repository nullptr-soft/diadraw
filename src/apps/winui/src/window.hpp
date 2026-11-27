// A window on one .draw file: the text on the left, the canvas on the right, a status line below.
// The text is the source of truth. Typing updates the shared document (dd::host::Document);
// canvas gestures come back from it as text edits. The window keeps one undo history of text
// states for both panes (RichEdit's own undo would also record the syntax colouring).

#pragma once

#include "winrt.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_view.hpp"
#include "cloud_window.hpp"
#include "diadraw/dsl/format.hpp"
#include "text_folds.hpp"
#include "host/document.hpp"

namespace dd::winui {

class DocumentWindow;

/// What the window asks of the app (menu items that are not about this document).
struct AppActions {
  std::function<void()> new_document;
  std::function<void()> open;
  std::function<void()> import;
  std::function<void(std::string_view)> open_example;  ///< File › Examples, by name
  /// A new, untitled window on `text` (a diagram opened from Google Drive or OneDrive).
  std::function<DocumentWindow*(const std::string& text)> open_text;
  std::function<void()> quit;
};

class DocumentWindow {
 public:
  DocumentWindow(AppActions actions, std::string path, const std::string& text);

  [[nodiscard]] winrt::Microsoft::UI::Xaml::Window const& window() const { return window_; }
  /// Shows the window; a test run shows it without activating it, so it cannot take focus.
  void show(bool activate);
  /// Runs when the window has closed.
  std::function<void()> on_closed;

  /// Shows a message in the status line until the next change (e.g. an import report).
  void flash(const std::string& message, bool error = false);

  void save();
  void save_as();
  /// File › Cloud Storage…: Google Drive and OneDrive, on `provider` (the document's, or the last).
  void show_cloud(std::optional<dd::host::cloud::Provider> provider = {}, const std::string& message = {});
  /// Makes the document the diagram at `place`, last saved as `text` (an open or a Save there).
  void set_cloud(const dd::host::cloud::Place& place, const std::string& text);
  void undo();
  void redo();
  void export_as(const std::string& extension);
  void add_element(const std::string& kind);
  /// Adds view `id` and shows it; renames or removes the view on the canvas.
  void new_view(const std::string& id);
  void rename_view(const std::string& id);
  void remove_view();
  /// Clear Canvas: makes the diagram empty as one undo step. ask_clear_canvas asks first when
  /// there are unsaved changes.
  winrt::fire_and_forget ask_clear_canvas();
  /// Help: what the canvas does with the mouse and the keyboard (host/canvas_help.hpp).
  winrt::fire_and_forget show_help();
  void clear_canvas();
  /// Format Document: reindents the text with the Indent Size setting, as one undo step.
  void format_text();
  /// Indent Size: remembers the size (unless a test script chose it) and checks its menu item.
  void set_indent_size(int size, bool remember);
  /// Renames an element (its id) and every reference to it; the selection follows.
  void rename_element(const std::string& element, const std::string& to);

  /// Runs a test script (see DIADRAW_SCRIPT in main.cpp) and returns its log.
  std::string run_script(const std::string& script);
  /// Writes the window's content to a PNG, then calls `done`.
  winrt::fire_and_forget snapshot(std::string path, std::function<void()> done);

 private:
  winrt::Microsoft::UI::Xaml::UIElement build_toolbar();
  void load(const std::string& text);
  void sync_from_text();
  void refresh();
  void restyle();
  void show_caret_diagnostic();
  void apply(const Result<std::optional<dd::host::Change>>& result, const std::string& before);
  void restore(const std::string& text);
  void update_title();
  void ask_new_view();
  void ask_rename_view();
  /// Rename Element… (F2): asks for a new name for the element named at the text cursor (when
  /// the text has the focus) or the one selected on the canvas.
  void ask_rename_element();
  /// The text cursor in UTF-8 bytes into the synced text.
  [[nodiscard]] std::size_t caret_byte();
  /// Asks for a name in a dialog, starting from `initial`; `done` gets what was typed.
  winrt::fire_and_forget ask_name(winrt::hstring title, winrt::hstring header, winrt::hstring verb,
                                  std::string initial, std::function<void(const std::string&)> done);
  void write_to(const std::string& path);
  [[nodiscard]] std::string box_text() const;
  void set_box_text(const std::string& text);

  struct Mark {
    int from = 0, to = 0;  ///< UTF-16 offsets in the text box
    bool error = true;
    std::string message;
  };

  AppActions actions_;
  std::string path_;
  /// Where the document lives when it came from, or was saved to, Google Drive or OneDrive.
  std::optional<dd::host::cloud::Place> cloud_;
  dd::host::cloud::Saver cloud_saver_;
  std::unique_ptr<CloudWindow> cloud_window_;
  dd::host::cloud::Provider last_provider_ = dd::host::cloud::Provider::GoogleDrive;
  std::unique_ptr<dd::host::Document> doc_;
  winrt::Microsoft::UI::Xaml::Window window_;
  winrt::Microsoft::UI::Xaml::Controls::Grid root_;
  winrt::Microsoft::UI::Xaml::Controls::RichEditBox text_;
  /// The fold markers beside the text; folding restyles the text.
  TextFolds folds_{text_, [this] {
                     sync_from_text();
                     restyle();
                   }};
  CanvasView canvas_;
  winrt::Microsoft::UI::Xaml::Controls::ComboBox views_;
  winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem grid_item_;  ///< View › Show Grid
  /// Edit › Indent Size: one item per size in host::kIndentSizes, and the size chosen.
  std::vector<winrt::Microsoft::UI::Xaml::Controls::RadioMenuFlyoutItem> indent_items_;
  int indent_size_ = dd::dsl::kDefaultIndentSize;
  /// Set when a test script chose the size (`indent`), or the settings file could not keep it:
  /// the size then stays as it is rather than following the file, until a size chosen from the
  /// menu is saved.
  bool keep_indent_ = false;
  /// Remove View in the View menu and beside the picker, off while the document has one view.
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem remove_view_item_;
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem remove_view_button_item_;
  winrt::Microsoft::UI::Xaml::Controls::TextBlock status_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
  bool setting_ = false;        ///< the window itself is changing the text box
  bool updating_views_ = false;
  bool asking_ = false;         ///< a name, Clear Canvas or Help dialog is open
  /// Expires with the window, for coroutines that resume after it may have closed.
  std::shared_ptr<char> alive_ = std::make_shared<char>();
  std::string committed_;       ///< the text as last given to the document
  std::string saved_;           ///< the text as last saved
  std::vector<std::string> undo_, redo_;
  std::vector<Mark> marks_;
  std::string summary_;
  bool summary_error_ = false;
  bool showing_mark_ = false;
};

}  // namespace dd::winui
