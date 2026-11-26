// File › Cloud Storage…: diagrams in Google Drive and OneDrive (host/cloud.hpp), one tab each, in a
// window of its own (so that its Rename and Delete dialogs have a root to show on), the same as the
// web app's storage dialog. It signs in and out, lists, renames and deletes by itself; opening a
// diagram and saving the current one go to the document window through its callbacks.

#pragma once

#include "winrt.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "host/cloud.hpp"

namespace dd::winui {

using CloudPlace = dd::host::cloud::Place;

/// Runs a function on `queue`'s thread (the UI thread), for host/cloud.hpp's background work.
dd::host::cloud::Post post_to(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue);

class CloudWindow {
 public:
  struct Callbacks {
    /// The current diagram's text and a name to suggest for it.
    std::function<std::string()> text;
    std::function<std::string()> suggested_name;
    /// The diagram being edited, when it came from a cloud (shown in bold in the list).
    std::function<std::optional<CloudPlace>()> editing;
    /// A diagram was opened (with its text), saved as a new file, renamed, or deleted.
    std::function<void(const CloudPlace&, const std::string& text)> opened;
    std::function<void(const CloudPlace&, const std::string& text)> saved;
    std::function<void(const CloudPlace&)> renamed;
    std::function<void(const CloudPlace&)> removed;
  };

  explicit CloudWindow(Callbacks callbacks);
  ~CloudWindow();
  CloudWindow(const CloudWindow&) = delete;
  CloudWindow& operator=(const CloudWindow&) = delete;

  /// Shows the window on `provider`, with `message` (such as "Sign in to save") above the list.
  void show(dd::host::cloud::Provider provider, const std::string& message = {});
  void close();

 private:
  void refresh();
  void sign_in();
  void show_error(const std::string& message);
  void set_busy(bool busy);
  void add_row(const dd::host::cloud::File& file);
  winrt::fire_and_forget rename(dd::host::cloud::File file);
  winrt::fire_and_forget remove(dd::host::cloud::File file);
  /// Runs `work` off the UI thread and `done` back on it, unless the window has closed by then
  /// (or, with `after_close`, unless this object is gone: for work whose result the document needs).
  template <class T>
  void run(std::function<Result<T>()> work, std::function<void(Result<T>)> done, bool after_close = false);

  Callbacks callbacks_;
  dd::host::cloud::Provider provider_ = dd::host::cloud::Provider::GoogleDrive;
  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton google_tab_{nullptr}, onedrive_tab_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock account_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button account_button_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox name_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button save_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid save_row_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock error_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock empty_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel list_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock note_{nullptr};
  bool open_ = false;
  bool asking_ = false;  ///< a Rename or Delete dialog is open
  /// Set while a sign-in waits for the browser; Cancel sets it.
  std::shared_ptr<std::atomic<bool>> cancel_sign_in_;
  int busy_ = 0;
  /// Bumped on every refresh, so a slow list from an earlier tab is dropped.
  int generation_ = 0;
  /// Background work checks these before touching the window: `alive_` lasts as long as this
  /// object, `session_` as long as the window is open (a new one for each time it opens).
  std::shared_ptr<char> alive_ = std::make_shared<char>();
  std::shared_ptr<char> session_;
};

}  // namespace dd::winui
