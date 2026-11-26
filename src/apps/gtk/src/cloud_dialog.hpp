// File › Cloud Storage…: diagrams in Google Drive and OneDrive (host/cloud.hpp), one tab each, the
// same as the web app's storage dialog. It signs in and out, lists, renames and deletes by itself;
// opening a diagram and saving the current one go to the window through its callbacks.

#pragma once

#include <gtkmm.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "host/cloud.hpp"

namespace dd::gtk {

/// Runs `fn` on the GTK main loop; safe to call from any thread.
void post_to_main(std::function<void()> fn);

using CloudPlace = dd::host::cloud::Place;

class CloudDialog : public Gtk::Window {
 public:
  struct Callbacks {
    /// The current diagram's text and a name to suggest for it.
    std::function<std::string()> text;
    std::function<std::string()> suggested_name;
    /// The diagram being edited, when it came from a cloud (highlighted in the list).
    std::function<std::optional<CloudPlace>()> editing;
    /// A diagram was opened (with its text), saved as a new file, renamed, or deleted.
    std::function<void(const CloudPlace&, const std::string& text)> opened;
    std::function<void(const CloudPlace&, const std::string& text)> saved;
    std::function<void(const CloudPlace&)> renamed;
    std::function<void(const CloudPlace&)> removed;
  };

  CloudDialog(Gtk::Window& parent, Callbacks callbacks);
  ~CloudDialog() override;

  /// Shows the dialog on `provider`, with `message` (such as "Sign in to save") above the list.
  void show_for(dd::host::cloud::Provider provider, const std::string& message = {});

 private:
  void select(dd::host::cloud::Provider provider);
  void refresh();
  void sign_in();
  void show_error(const std::string& message);
  void set_busy(bool busy);
  void add_row(const dd::host::cloud::File& file);
  void rename(const dd::host::cloud::File& file);
  void remove(const dd::host::cloud::File& file);
  [[nodiscard]] dd::host::cloud::Drive& drive() const { return dd::host::cloud::drive(provider_); }
  /// Runs `work` off the main loop and `done` back on it, unless the dialog is gone by then.
  template <class T>
  void run(std::function<Result<T>()> work, std::function<void(Result<T>)> done);

  Gtk::Window& parent_;
  Callbacks callbacks_;
  dd::host::cloud::Provider provider_ = dd::host::cloud::Provider::GoogleDrive;
  Gtk::ToggleButton google_tab_, onedrive_tab_;
  Gtk::Label account_label_;
  Gtk::Button account_button_;
  Gtk::Entry name_;
  Gtk::Button save_;
  Gtk::Box save_row_;
  Gtk::Label error_;
  Gtk::Label empty_;
  Gtk::ListBox list_;
  Gtk::ScrolledWindow scroll_;
  Gtk::Label note_;
  std::unique_ptr<Gtk::Window> prompt_;
  /// Set while a sign-in waits for the browser; Cancel sets it.
  std::shared_ptr<std::atomic<bool>> cancel_sign_in_;
  int busy_ = 0;
  /// Bumped on every refresh, so a slow list from an earlier tab is dropped.
  int generation_ = 0;
  /// Background work checks this before touching the dialog.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace dd::gtk
