#include "cloud_window.hpp"

#include <shellapi.h>

#include <algorithm>
#include <utility>

#include "files.hpp"

namespace dd::winui {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton;
using dd::host::cloud::File;
using dd::host::cloud::Provider;
namespace cloud = dd::host::cloud;

namespace {

/// "today 14:05", or the date and time, in the user's format.
std::string describe_time(std::int64_t ms) {
  if (ms <= 0) return {};
  // 100-nanosecond ticks since 1601-01-01.
  const std::uint64_t ticks = static_cast<std::uint64_t>(ms) * 10000ULL + 116444736000000000ULL;
  FILETIME utc{static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(ticks >> 32)};
  FILETIME local_ft{};
  SYSTEMTIME local{}, now{};
  if (!FileTimeToLocalFileTime(&utc, &local_ft) || !FileTimeToSystemTime(&local_ft, &local)) return {};
  GetLocalTime(&now);
  wchar_t date[64] = {}, time[64] = {};
  GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 64);
  if (local.wYear == now.wYear && local.wMonth == now.wMonth && local.wDay == now.wDay)
    return "today " + narrow(winrt::hstring(time));
  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64, nullptr);
  return narrow(winrt::hstring(date)) + " " + narrow(winrt::hstring(time));
}

TextBlock text_block(const std::string& text, bool secondary = false) {
  TextBlock block;
  block.Text(wide(text));
  block.TextWrapping(TextWrapping::Wrap);
  if (secondary) {
    block.Opacity(0.7);
    block.FontSize(12);
  }
  return block;
}

Button button(const wchar_t* label) {
  Button b;
  b.Content(winrt::box_value(label));
  return b;
}

/// Makes `b` the accent (default) button, or a plain one.
void accent(Button const& b, bool on) {
  if (!on) {
    b.ClearValue(FrameworkElement::StyleProperty());
    return;
  }
  auto style = Application::Current().Resources().TryLookup(winrt::box_value(L"AccentButtonStyle"));
  if (auto s = style.try_as<Style>()) b.Style(s);
}

}  // namespace

CloudWindow::CloudWindow(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

CloudWindow::~CloudWindow() {
  if (cancel_sign_in_) cancel_sign_in_->store(true);
  close();
}

cloud::Post post_to(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue) {
  return [queue](std::function<void()> fn) { queue.TryEnqueue([fn = std::move(fn)] { fn(); }); };
}

template <class T>
void CloudWindow::run(std::function<Result<T>()> work, std::function<void(Result<T>)> done, bool after_close) {
  set_busy(true);
  std::weak_ptr<char> alive = alive_, session = session_;
  cloud::in_background<T>(post_to(window_.DispatcherQueue()), std::move(work),
                          [this, alive, session, after_close, done = std::move(done)](Result<T> result) {
                            if (alive.expired()) return;
                            const bool same_window = !session.expired();
                            if (same_window) set_busy(false);
                            if (same_window || after_close) done(std::move(result));
                          });
}

void CloudWindow::close() {
  if (open_ && window_) window_.Close();
}

void CloudWindow::show(Provider provider, const std::string& message) {
  provider_ = provider;
  if (!open_) {
    window_ = Window();
    window_.Title(L"Cloud Storage — DiaDraw");
    window_.AppWindow().Resize(winrt::Windows::Graphics::SizeInt32{560, 600});
    window_.AppWindow().SetIcon(wide(executable_dir() + "/DiaDraw.ico"));
    root_ = Grid();
    root_.Padding(ThicknessHelper::FromUniformLength(16));
    root_.RowSpacing(10);
    for (int i = 0; i < 8; ++i) {
      RowDefinition row;
      row.Height(i == 6 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
      root_.RowDefinitions().Append(row);
    }
    auto place = [this](UIElement const& e, int row) {
      Grid::SetRow(e.as<FrameworkElement>(), row);
      root_.Children().Append(e);
    };

    StackPanel tabs;
    tabs.Orientation(Orientation::Horizontal);
    google_tab_ = ToggleButton();
    google_tab_.Content(winrt::box_value(L"Google Drive"));
    onedrive_tab_ = ToggleButton();
    onedrive_tab_.Content(winrt::box_value(L"OneDrive"));
    tabs.Children().Append(google_tab_);
    tabs.Children().Append(onedrive_tab_);
    google_tab_.Click([this](auto const&, auto const&) {
      provider_ = Provider::GoogleDrive;
      refresh();
    });
    onedrive_tab_.Click([this](auto const&, auto const&) {
      provider_ = Provider::OneDrive;
      refresh();
    });
    place(tabs, 0);

    Grid account;
    ColumnDefinition grow, fit;
    grow.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    fit.Width(GridLengthHelper::Auto());
    account.ColumnDefinitions().Append(grow);
    account.ColumnDefinitions().Append(fit);
    account.ColumnSpacing(8);
    account_ = text_block("");
    account_.VerticalAlignment(VerticalAlignment::Center);
    account_button_ = button(L"Sign in");
    Grid::SetColumn(account_button_, 1);
    account.Children().Append(account_);
    account.Children().Append(account_button_);
    account_button_.Click([this](auto const&, auto const&) {
      if (cancel_sign_in_) {
        cancel_sign_in_->store(true);
      } else if (cloud::drive(provider_).account().signed_in()) {
        cloud::drive(provider_).account().sign_out();
        refresh();
      } else {
        sign_in();
      }
    });
    place(account, 1);

    save_row_ = Grid();
    ColumnDefinition grow2, fit2;
    grow2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    fit2.Width(GridLengthHelper::Auto());
    save_row_.ColumnDefinitions().Append(grow2);
    save_row_.ColumnDefinitions().Append(fit2);
    save_row_.ColumnSpacing(8);
    name_ = TextBox();
    name_.PlaceholderText(L"Name");
    save_ = button(L"Save current diagram");
    accent(save_, true);
    Grid::SetColumn(save_, 1);
    save_row_.Children().Append(name_);
    save_row_.Children().Append(save_);
    auto save = [this] {
      std::string name = narrow(name_.Text());
      if (name.find_first_not_of(" \t") == std::string::npos || busy_) return;
      const Provider provider = provider_;
      auto text = std::make_shared<std::string>(callbacks_.text());
      // The document learns of the new file even if this window closed meanwhile; only this
      // window's own session shows the error or closes.
      run<File>([provider, name, text] { return cloud::drive(provider).create(name, *text); },
                [this, provider, text, session = std::weak_ptr<char>(session_)](Result<File> file) {
                  if (file) callbacks_.saved({provider, file->id, file->name}, *text);
                  if (session.expired()) return;
                  if (!file) return show_error(file.error().message);
                  close();
                },
                /*after_close=*/true);
    };
    save_.Click([save](auto const&, auto const&) { save(); });
    name_.KeyDown([save](auto const&, Input::KeyRoutedEventArgs const& e) {
      if (e.Key() == winrt::Windows::System::VirtualKey::Enter) {
        e.Handled(true);
        save();
      }
    });
    place(save_row_, 2);

    error_ = text_block("");
    error_.Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{255, 196, 43, 28}));
    place(error_, 3);
    empty_ = text_block("", true);
    place(empty_, 4);
    list_ = StackPanel();
    list_.Spacing(2);
    scroll_ = ScrollViewer();
    scroll_.Content(list_);
    place(scroll_, 6);
    note_ = text_block("", true);
    place(note_, 7);

    window_.Content(root_);
    window_.Closed([this, alive = std::weak_ptr<char>(alive_)](auto const&, auto const&) {
      if (alive.expired()) return;
      open_ = false;
      // Work still under way belongs to this window: its results are dropped, and the next window
      // starts idle. Closing gives up a sign-in that is still waiting.
      session_.reset();
      busy_ = 0;
      asking_ = false;
      if (cancel_sign_in_) cancel_sign_in_->store(true);
      cancel_sign_in_.reset();
    });
    open_ = true;
    busy_ = 0;
    session_ = std::make_shared<char>();
  }
  name_.Text(wide(callbacks_.suggested_name()));
  window_.Activate();
  refresh();
  if (!message.empty()) show_error(message);
}

void CloudWindow::show_error(const std::string& message) {
  error_.Text(wide(message));
  error_.Visibility(message.empty() ? Visibility::Collapsed : Visibility::Visible);
}

void CloudWindow::set_busy(bool busy) {
  busy_ = busy ? busy_ + 1 : std::max(busy_ - 1, 0);
  const bool idle = busy_ == 0;
  save_.IsEnabled(idle);
  list_.IsHitTestVisible(idle);
  list_.Opacity(idle ? 1.0 : 0.6);
  google_tab_.IsEnabled(idle);
  onedrive_tab_.IsEnabled(idle);
  // The account button stays live while signing in: it cancels.
  account_button_.IsEnabled(idle || cancel_sign_in_ != nullptr);
}

void CloudWindow::refresh() {
  if (!open_) return;
  const int generation = ++generation_;
  google_tab_.IsChecked(provider_ == Provider::GoogleDrive);
  onedrive_tab_.IsChecked(provider_ == Provider::OneDrive);
  const cloud::PanelState state = cloud::panel_state(provider_, cancel_sign_in_ != nullptr);
  const bool signed_in = state.signed_in;
  show_error({});
  list_.Children().Clear();
  account_.Text(wide(state.account_text));
  account_button_.Content(winrt::box_value(wide(state.button_text)));
  accent(account_button_, state.button_primary);
  account_button_.Visibility(state.show_button ? Visibility::Visible : Visibility::Collapsed);
  const auto shown = signed_in ? Visibility::Visible : Visibility::Collapsed;
  save_row_.Visibility(shown);
  scroll_.Visibility(shown);
  empty_.Visibility(shown);
  ToolTipService::SetToolTip(save_, winrt::box_value(wide(state.save_tooltip)));
  note_.Text(wide(state.note));
  if (!signed_in) return;
  empty_.Text(wide(state.loading_text));
  const Provider provider = provider_;
  run<std::vector<File>>([provider] { return cloud::drive(provider).list(); },
                         [this, generation, empty_text = state.empty_text](Result<std::vector<File>> files) {
                           if (generation != generation_) return;
                           if (!files) {
                             empty_.Visibility(Visibility::Collapsed);
                             // A sign-in that ran out shows the Sign in button again.
                             if (!cloud::drive(provider_).account().signed_in()) refresh();
                             return show_error(files.error().message);
                           }
                           empty_.Text(wide(empty_text));
                           empty_.Visibility(files->empty() ? Visibility::Visible : Visibility::Collapsed);
                           for (const auto& file : *files) add_row(file);
                         });
}

void CloudWindow::sign_in() {
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  cancel_sign_in_ = cancel;
  refresh();
  const Provider provider = provider_;
  // Opens the sign-in page in the browser (ShellExecute works from any thread).
  auto open = [](const std::string& url) {
    std::wstring address(wide(url));
    // ShellExecute may hand the URL to a COM handler, so this background thread needs COM.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (SUCCEEDED(com)) CoUninitialize();
  };
  run<bool>([provider, open, cancel] { return cloud::drive(provider).account().sign_in(open, *cancel); },
            [this, cancel](Result<bool> done) {
              if (cancel_sign_in_ == cancel) cancel_sign_in_.reset();
              refresh();
              if (!done && !cancel->load()) show_error(done.error().message);
              window_.Activate();  // back to DiaDraw from the browser
            });
}

void CloudWindow::add_row(const File& file) {
  Grid row;
  for (auto width : {GridLengthHelper::FromValueAndType(1, GridUnitType::Star), GridLengthHelper::Auto(),
                     GridLengthHelper::Auto(), GridLengthHelper::Auto()}) {
    ColumnDefinition column;
    column.Width(width);
    row.ColumnDefinitions().Append(column);
  }
  row.ColumnSpacing(6);
  const std::string name = cloud::plain_name(file.name);
  Button open;
  open.HorizontalAlignment(HorizontalAlignment::Stretch);
  open.HorizontalContentAlignment(HorizontalAlignment::Left);
  TextBlock title;
  title.Text(wide(name));
  title.TextTrimming(TextTrimming::CharacterEllipsis);
  if (auto editing = callbacks_.editing(); editing && editing->provider == provider_ && editing->id == file.id) {
    title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    title.Text(wide(name + " · open"));
  }
  open.Content(title);
  ToolTipService::SetToolTip(open, winrt::box_value(wide("Open " + file.name)));
  TextBlock time = text_block(describe_time(file.updated), true);
  time.VerticalAlignment(VerticalAlignment::Center);
  Button rename = button(L"Rename");
  Button remove = button(L"Delete");
  ToolTipService::SetToolTip(remove, winrt::box_value(wide(std::string(cloud::remove_tooltip(provider_)))));
  Grid::SetColumn(time, 1);
  Grid::SetColumn(rename, 2);
  Grid::SetColumn(remove, 3);
  row.Children().Append(open);
  row.Children().Append(time);
  row.Children().Append(rename);
  row.Children().Append(remove);
  list_.Children().Append(row);
  const Provider provider = provider_;
  open.Click([this, provider, file](auto const&, auto const&) {
    run<std::optional<cloud::Contents>>(
        [provider, id = file.id] { return cloud::drive(provider).read(id); },
        [this, provider](Result<std::optional<cloud::Contents>> contents) {
          if (!contents) return show_error(contents.error().message);
          if (!*contents) {
            refresh();
            return show_error("That diagram is no longer in " + std::string(cloud::label(provider)));
          }
          const auto c = **contents;
          close();
          callbacks_.opened({provider, c.file.id, c.file.name}, c.text);
        });
  });
  rename.Click([this, file](auto const&, auto const&) { this->rename(file); });
  remove.Click([this, file](auto const&, auto const&) { this->remove(file); });
}

winrt::fire_and_forget CloudWindow::rename(File file) {
  // One dialog at a time: ShowAsync throws while another is open on the same root, and an
  // exception escaping a fire_and_forget coroutine ends the app.
  if (asking_ || busy_) co_return;
  asking_ = true;
  std::weak_ptr<char> alive = alive_, session = session_;
  TextBox name;
  name.Text(wide(cloud::plain_name(file.name)));
  name.SelectAll();
  ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.Title(winrt::box_value(L"Rename"));
  dialog.Content(name);
  dialog.PrimaryButtonText(L"Rename");
  dialog.CloseButtonText(L"Cancel");
  dialog.DefaultButton(ContentDialogButton::Primary);
  ContentDialogResult result = ContentDialogResult::None;
  try {
    result = co_await dialog.ShowAsync();
  } catch (const winrt::hresult_error&) {
  }
  // Gone, or closed meanwhile (perhaps opened again since): this dialog's answer is stale.
  if (alive.expired() || session.expired()) co_return;
  asking_ = false;
  const std::string chosen = narrow(name.Text());
  if (result != ContentDialogResult::Primary || chosen.find_first_not_of(" \t") == std::string::npos ||
      chosen == cloud::plain_name(file.name) || !open_)
    co_return;
  const Provider provider = provider_;
  run<File>([provider, id = file.id, chosen] { return cloud::drive(provider).rename(id, chosen); },
            [this, provider](Result<File> renamed) {
              if (!renamed) return show_error(renamed.error().message);
              callbacks_.renamed({provider, renamed->id, renamed->name});
              refresh();
            });
}

winrt::fire_and_forget CloudWindow::remove(File file) {
  if (asking_ || busy_) co_return;
  asking_ = true;
  std::weak_ptr<char> alive = alive_, session = session_;
  const Provider provider = provider_;
  ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.Title(winrt::box_value(wide("Delete “" + cloud::plain_name(file.name) + "”?")));
  dialog.Content(winrt::box_value(wide(std::string(cloud::remove_detail(provider)))));
  dialog.PrimaryButtonText(L"Delete");
  dialog.CloseButtonText(L"Cancel");
  dialog.DefaultButton(ContentDialogButton::Close);
  ContentDialogResult result = ContentDialogResult::None;
  try {
    result = co_await dialog.ShowAsync();
  } catch (const winrt::hresult_error&) {
  }
  // Gone, or closed meanwhile (perhaps opened again since): this dialog's answer is stale.
  if (alive.expired() || session.expired()) co_return;
  asking_ = false;
  if (result != ContentDialogResult::Primary || !open_) co_return;
  run<bool>([provider, id = file.id] { return cloud::drive(provider).remove(id); },
            [this, provider, file](Result<bool> removed) {
              if (!removed) return show_error(removed.error().message);
              callbacks_.removed({provider, file.id, file.name});
              refresh();
            });
}

}  // namespace dd::winui
