#include "cloud_dialog.hpp"

#include <algorithm>
#include <utility>

namespace dd::gtk {

using dd::host::cloud::File;
using dd::host::cloud::Provider;
namespace cloud = dd::host::cloud;

void post_to_main(std::function<void()> fn) {
  auto* heap = new std::function<void()>(std::move(fn));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()>*>(data))();
        return G_SOURCE_REMOVE;
      },
      heap, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}

namespace {

/// "today 14:05", or the date and time.
std::string describe_time(std::int64_t ms) {
  if (ms <= 0) return {};
  auto when = Glib::DateTime::create_now_local(ms / 1000);
  auto now = Glib::DateTime::create_now_local();
  if (when.get_year() == now.get_year() && when.get_day_of_year() == now.get_day_of_year())
    return "today " + when.format("%H:%M");
  return when.format("%x %H:%M");
}

}  // namespace

template <class T>
void CloudDialog::run(std::function<Result<T>()> work, std::function<void(Result<T>)> done) {
  set_busy(true);
  std::weak_ptr<bool> alive = alive_;
  cloud::in_background<T>(post_to_main, std::move(work), [this, alive, done = std::move(done)](Result<T> result) {
    if (alive.expired()) return;
    set_busy(false);
    done(std::move(result));
  });
}

CloudDialog::CloudDialog(Gtk::Window& parent, Callbacks callbacks)
    : parent_(parent),
      callbacks_(std::move(callbacks)),
      google_tab_("Google Drive"),
      onedrive_tab_("OneDrive"),
      save_("Save current diagram"),
      save_row_(Gtk::Orientation::HORIZONTAL, 8) {
  set_title("Cloud Storage");
  set_transient_for(parent);
  set_modal(true);
  set_hide_on_close(true);
  set_default_size(480, 520);

  auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  box->set_margin(14);

  auto tabs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  tabs->add_css_class("linked");
  onedrive_tab_.set_group(google_tab_);
  tabs->append(google_tab_);
  tabs->append(onedrive_tab_);
  google_tab_.signal_toggled().connect([this] {
    if (google_tab_.get_active()) select(Provider::GoogleDrive);
  });
  onedrive_tab_.signal_toggled().connect([this] {
    if (onedrive_tab_.get_active()) select(Provider::OneDrive);
  });
  box->append(*tabs);

  auto account = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  account_label_.set_hexpand(true);
  account_label_.set_halign(Gtk::Align::START);
  account_label_.set_wrap(true);
  account->append(account_label_);
  account->append(account_button_);
  account_button_.signal_clicked().connect([this] {
    if (cancel_sign_in_) {
      cancel_sign_in_->store(true);
    } else if (drive().account().signed_in()) {
      drive().account().sign_out();
      refresh();
    } else {
      sign_in();
    }
  });
  box->append(*account);

  name_.set_hexpand(true);
  name_.set_placeholder_text("Name");
  name_.signal_activate().connect([this] { save_.activate(); });
  save_.add_css_class("suggested-action");
  save_row_.append(name_);
  save_row_.append(save_);
  save_.signal_clicked().connect([this] {
    std::string name = name_.get_text();
    if (name.find_first_not_of(" \t") == std::string::npos) {
      name_.grab_focus();
      return;
    }
    const Provider provider = provider_;
    auto text = std::make_shared<std::string>(callbacks_.text());
    run<File>([provider, name, text] { return cloud::drive(provider).create(name, *text); },
              [this, provider, text](Result<File> file) {
                if (!file) return show_error(file.error().message);
                callbacks_.saved({provider, file->id, file->name}, *text);
                hide();
              });
  });
  box->append(save_row_);

  error_.add_css_class("error");
  error_.set_wrap(true);
  error_.set_halign(Gtk::Align::START);
  box->append(error_);

  empty_.add_css_class("dim-label");
  empty_.set_halign(Gtk::Align::START);
  box->append(empty_);

  list_.set_selection_mode(Gtk::SelectionMode::NONE);
  list_.add_css_class("boxed-list");
  scroll_.set_child(list_);
  scroll_.set_vexpand(true);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  box->append(scroll_);

  note_.add_css_class("dim-label");
  note_.set_wrap(true);
  note_.set_halign(Gtk::Align::START);
  note_.set_xalign(0);
  box->append(note_);

  set_child(*box);
  // Escape closes, as in the other dialogs.
  auto keys = Gtk::ShortcutController::create();
  keys->add_shortcut(Gtk::Shortcut::create(Gtk::KeyvalTrigger::create(GDK_KEY_Escape),
                                           Gtk::CallbackAction::create([this](Gtk::Widget&, const Glib::VariantBase&) {
                                             hide();
                                             return true;
                                           })));
  add_controller(keys);
  signal_hide().connect([this] {
    // Closing gives up a sign-in that is still waiting.
    if (cancel_sign_in_) cancel_sign_in_->store(true);
  });
}

CloudDialog::~CloudDialog() {
  if (cancel_sign_in_) cancel_sign_in_->store(true);
}

void CloudDialog::show_for(Provider provider, const std::string& message) {
  name_.set_text(callbacks_.suggested_name());
  provider_ = provider;  // first, so that the tab's toggle below does not refresh too
  (provider == Provider::GoogleDrive ? google_tab_ : onedrive_tab_).set_active(true);
  present();
  refresh();
  if (!message.empty()) show_error(message);
  if (save_row_.get_visible()) {
    name_.grab_focus();
    name_.select_region(0, -1);
  }
}

void CloudDialog::select(Provider provider) {
  if (provider == provider_) return;
  provider_ = provider;
  refresh();
}

void CloudDialog::show_error(const std::string& message) {
  error_.set_text(message);
  error_.set_visible(!message.empty());
}

void CloudDialog::set_busy(bool busy) {
  busy_ = busy ? busy_ + 1 : std::max(busy_ - 1, 0);
  const bool idle = busy_ == 0;
  save_.set_sensitive(idle);
  list_.set_sensitive(idle);
  google_tab_.set_sensitive(idle);
  onedrive_tab_.set_sensitive(idle);
  // The account button stays live while signing in: it cancels.
  account_button_.set_sensitive(idle || cancel_sign_in_ != nullptr);
}

void CloudDialog::refresh() {
  const int generation = ++generation_;
  const cloud::PanelState state = cloud::panel_state(provider_, cancel_sign_in_ != nullptr);
  const bool signed_in = state.signed_in;
  show_error({});
  while (auto* row = list_.get_first_child()) list_.remove(*row);
  account_label_.set_text(state.account_text);
  account_button_.set_label(state.button_text);
  account_button_.set_visible(state.show_button);
  if (state.button_primary) account_button_.add_css_class("suggested-action");
  else account_button_.remove_css_class("suggested-action");
  save_row_.set_visible(signed_in);
  save_.set_tooltip_text(state.save_tooltip);
  note_.set_text(state.note);
  empty_.set_visible(signed_in);
  scroll_.set_visible(signed_in);
  if (!signed_in) return;
  empty_.set_text(state.loading_text);
  const Provider provider = provider_;
  run<std::vector<File>>([provider] { return cloud::drive(provider).list(); },
                         [this, generation, empty_text = state.empty_text](Result<std::vector<File>> files) {
                           if (generation != generation_) return;
                           if (!files) {
                             empty_.set_visible(false);
                             // A sign-in that ran out shows the Sign in button again.
                             if (!drive().account().signed_in()) refresh();
                             return show_error(files.error().message);
                           }
                           empty_.set_text(empty_text);
                           empty_.set_visible(files->empty());
                           for (const auto& file : *files) add_row(file);
                         });
}

void CloudDialog::sign_in() {
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  cancel_sign_in_ = cancel;
  const Provider provider = provider_;
  std::weak_ptr<bool> alive = alive_;
  refresh();
  // Opens the sign-in page in the browser, from the main loop.
  auto open = [this, alive](const std::string& url) {
    post_to_main([this, alive, url] {
      if (alive.expired()) return;
      auto launcher = Gtk::UriLauncher::create(url);
      launcher->launch(parent_, [launcher](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
          (void)launcher->launch_finish(result);
        } catch (const Glib::Error&) {
          // The address is in the dialog's error line below if this fails often; nothing to do here.
        }
      });
    });
  };
  run<bool>([provider, open, cancel] { return cloud::drive(provider).account().sign_in(open, *cancel); },
            [this, cancel](Result<bool> done) {
              if (cancel_sign_in_ == cancel) cancel_sign_in_.reset();
              refresh();
              if (!done && !cancel->load()) show_error(done.error().message);
            });
}

void CloudDialog::add_row(const File& file) {
  auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  row->set_margin(4);
  const std::string name = cloud::plain_name(file.name);
  auto open = Gtk::make_managed<Gtk::Button>();
  auto label = Gtk::make_managed<Gtk::Label>(name);
  label->set_ellipsize(Pango::EllipsizeMode::END);
  label->set_halign(Gtk::Align::START);
  if (auto editing = callbacks_.editing(); editing && editing->provider == provider_ && editing->id == file.id) {
    label->set_markup("<b>" + Glib::Markup::escape_text(name) + "</b> · open");
  }
  open->set_child(*label);
  open->add_css_class("flat");
  open->set_hexpand(true);
  open->set_tooltip_text("Open " + file.name);
  auto time = Gtk::make_managed<Gtk::Label>(describe_time(file.updated));
  time->add_css_class("dim-label");
  auto rename = Gtk::make_managed<Gtk::Button>("Rename");
  auto remove = Gtk::make_managed<Gtk::Button>("Delete");
  remove->set_tooltip_text(std::string(cloud::remove_tooltip(provider_)));
  row->append(*open);
  row->append(*time);
  row->append(*rename);
  row->append(*remove);
  list_.append(*row);
  const Provider provider = provider_;
  open->signal_clicked().connect([this, provider, file] {
    run<std::optional<cloud::Contents>>(
        [provider, id = file.id] { return cloud::drive(provider).read(id); },
        [this, provider](Result<std::optional<cloud::Contents>> contents) {
          if (!contents) return show_error(contents.error().message);
          if (!*contents) {
            refresh();
            return show_error("That diagram is no longer in " + std::string(cloud::label(provider)));
          }
          const auto& c = **contents;
          hide();
          callbacks_.opened({provider, c.file.id, c.file.name}, c.text);
        });
  });
  rename->signal_clicked().connect([this, file] { this->rename(file); });
  remove->signal_clicked().connect([this, file] { this->remove(file); });
}

void CloudDialog::rename(const File& file) {
  prompt_ = std::make_unique<Gtk::Window>();
  prompt_->set_title("Rename");
  prompt_->set_transient_for(*this);
  prompt_->set_modal(true);
  prompt_->set_resizable(false);
  prompt_->set_hide_on_close(true);
  auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(12);
  auto entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(cloud::plain_name(file.name));
  entry->set_activates_default(true);
  auto buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  buttons->set_halign(Gtk::Align::END);
  auto cancel = Gtk::make_managed<Gtk::Button>("Cancel");
  auto ok = Gtk::make_managed<Gtk::Button>("Rename");
  ok->add_css_class("suggested-action");
  buttons->append(*cancel);
  buttons->append(*ok);
  box->append(*entry);
  box->append(*buttons);
  prompt_->set_child(*box);
  prompt_->set_default_widget(*ok);
  cancel->signal_clicked().connect([this] { prompt_->hide(); });
  const Provider provider = provider_;
  ok->signal_clicked().connect([this, entry, provider, file] {
    std::string name = entry->get_text();
    prompt_->hide();
    if (name.empty() || name == cloud::plain_name(file.name)) return;
    run<File>([provider, id = file.id, name] { return cloud::drive(provider).rename(id, name); },
              [this, provider](Result<File> renamed) {
                if (!renamed) return show_error(renamed.error().message);
                callbacks_.renamed({provider, renamed->id, renamed->name});
                refresh();
              });
  });
  prompt_->present();
  entry->grab_focus();
}

void CloudDialog::remove(const File& file) {
  const Provider provider = provider_;
  auto alert = Gtk::AlertDialog::create("Delete “" + cloud::plain_name(file.name) + "”?");
  alert->set_detail(std::string(cloud::remove_detail(provider)));
  alert->set_buttons({"Cancel", "Delete"});
  alert->set_cancel_button(0);
  alert->set_default_button(0);
  std::weak_ptr<bool> alive = alive_;
  alert->choose(*this, [this, alive, alert, provider, file](const Glib::RefPtr<Gio::AsyncResult>& answer) {
    try {
      if (alive.expired() || alert->choose_finish(answer) != 1) return;
    } catch (const Glib::Error&) {
      return;
    }
    run<bool>([provider, id = file.id] { return cloud::drive(provider).remove(id); },
              [this, provider, file](Result<bool> removed) {
                if (!removed) return show_error(removed.error().message);
                callbacks_.removed({provider, file.id, file.name});
                refresh();
              });
  });
}

}  // namespace dd::gtk
