#include "window.hpp"

#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "diadraw/base/text.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/highlight.hpp"
#include "files.hpp"
#include "host/about.hpp"
#include "host/canvas_help.hpp"
#include "host/credits.hpp"
#include "host/examples.hpp"
#include "host/feedback.hpp"
#include "host/settings.hpp"

namespace dd::winui {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
namespace text = winrt::Microsoft::UI::Text;
using winrt::Windows::System::VirtualKey;
using winrt::Windows::System::VirtualKeyModifiers;
using Color = winrt::Windows::UI::Color;

namespace {

const char* kDefaultText =
    "model {\n  user = person \"Customer\"\n  shop = system \"Shop\"\n  user -> shop \"browses\"\n}\n";

constexpr Color kText{255, 31, 31, 31};
constexpr Color kError{255, 229, 52, 29};
constexpr Color kWarning{255, 224, 138, 0};

std::optional<Color> colour_of(dd::dsl::TokenKind kind) {
  using K = dd::dsl::TokenKind;
  switch (kind) {
    case K::Comment: return Color{255, 142, 142, 147};
    case K::String: return Color{255, 209, 47, 27};
    case K::Number: return Color{255, 194, 107, 0};
    case K::Keyword: return Color{255, 155, 35, 147};
    case K::Kind: return Color{255, 15, 127, 140};
    case K::Definition: return Color{255, 11, 79, 214};
    case K::Operator:
    case K::Bracket: return Color{255, 110, 110, 115};
    case K::Name: return std::nullopt;
  }
  return std::nullopt;
}

/// UTF-16 offset of each UTF-8 byte offset of `text` (what the text box counts).
std::vector<int> utf16_map(const std::string& text) {
  std::vector<int> map(text.size() + 1);
  int unit = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    map[i] = unit;
    auto c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0) != 0x80) unit += c >= 0xF0 ? 2 : 1;
  }
  map[text.size()] = unit;
  return map;
}

MenuFlyoutItem menu_item(const wchar_t* label, std::function<void()> action, VirtualKey key = VirtualKey::None,
                         VirtualKeyModifiers modifiers = VirtualKeyModifiers::Control) {
  MenuFlyoutItem item;
  item.Text(label);
  item.Click([action](auto const&, auto const&) { action(); });
  if (key != VirtualKey::None) {
    KeyboardAccelerator accelerator;
    accelerator.Key(key);
    accelerator.Modifiers(modifiers);
    item.KeyboardAccelerators().Append(accelerator);
  }
  return item;
}

/// "Windows 11 (build 26100)": RtlGetVersion, since GetVersionEx reports what the manifest asks for.
std::string windows_version() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  if (!get || get(&info) != 0) return "Windows";
  // Windows 11 still reports 10.0; its builds start at 22000.
  std::string name = info.dwMajorVersion == 10
                         ? (info.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10")
                         : "Windows " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion);
  return name + " (build " + std::to_string(info.dwBuildNumber) + ")";
}

/// Opens the issue form for `kind` in the browser, with the app, version and Windows version filled in.
void open_feedback(dd::host::FeedbackKind kind, HWND owner) {
  auto url = wide(dd::host::feedback_url(kind, {"Windows", windows_version()}));
  auto result = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) > 32) return;
  // No browser opened: show the address (Ctrl+C copies a message box's text).
  std::wstring text = L"Could not open a web browser. Open this address to report it:\n\n";
  text += url.c_str();
  MessageBoxW(owner, text.c_str(), L"DiaDraw", MB_OK | MB_ICONWARNING);
}

/// The divider in `panes`' middle column: a bar with a grip of three dots in its middle, as on the
/// other platforms. Dragging it sets the width of the column on its left, `left`; a drag leaves the
/// canvas in `right` at least 160 wide.
void add_divider(Grid const& panes, ColumnDefinition const& left, ColumnDefinition const& right) {
  Grid grip;
  grip.Background(Media::SolidColorBrush(Color{255, 0xf3, 0xf3, 0xf3}));
  grip.BorderBrush(Media::SolidColorBrush(Color{255, 0xd0, 0xd0, 0xd0}));
  grip.BorderThickness(ThicknessHelper::FromLengths(1, 0, 1, 0));
  StackPanel dots;
  dots.Spacing(4);
  dots.VerticalAlignment(VerticalAlignment::Center);
  dots.HorizontalAlignment(HorizontalAlignment::Center);
  for (int i = 0; i < 3; ++i) {
    Shapes::Ellipse dot;
    dot.Width(3);
    dot.Height(3);
    dot.Fill(Media::SolidColorBrush(Color{255, 0x70, 0x70, 0x70}));
    dots.Children().Append(dot);
  }
  grip.Children().Append(dots);
  // A protected property: set through its interface, since the grip is no subclass of its own.
  if (auto element = grip.try_as<IUIElementProtected>())
    element.ProtectedCursor(winrt::Microsoft::UI::Input::InputSystemCursor::Create(
        winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
  Grid::SetColumn(grip, 1);
  panes.Children().Append(grip);

  struct Drag {
    bool active = false;
    double x = 0, width = 0;
  };
  auto drag = std::make_shared<Drag>();
  // `panes` owns the grip, so the handlers hold weak references to it (no reference cycle).
  winrt::weak_ref<Grid> weak_panes = panes;
  grip.PointerPressed([drag, weak_panes, left](auto const& sender, PointerRoutedEventArgs const& e) {
    auto owner = weak_panes.get();
    if (!owner) return;
    auto point = e.GetCurrentPoint(owner);
    if (!point.Properties().IsLeftButtonPressed()) return;  // as the canvas: left button only
    sender.as<UIElement>().CapturePointer(e.Pointer());
    *drag = {true, point.Position().X, left.ActualWidth()};
    e.Handled(true);
  });
  grip.PointerMoved([drag, weak_panes, left, right](auto const&, PointerRoutedEventArgs const& e) {
    auto owner = weak_panes.get();
    if (!drag->active || !owner) return;
    constexpr double kCanvasMin = 160;
    double max = std::max(left.MinWidth(), left.ActualWidth() + right.ActualWidth() - kCanvasMin);
    double width = drag->width + e.GetCurrentPoint(owner).Position().X - drag->x;
    left.Width(GridLengthHelper::FromPixels(std::clamp(width, left.MinWidth(), max)));
    e.Handled(true);
  });
  grip.PointerReleased([drag](auto const& sender, PointerRoutedEventArgs const& e) {
    drag->active = false;
    sender.as<UIElement>().ReleasePointerCapture(e.Pointer());
    e.Handled(true);
  });
  grip.PointerCaptureLost([drag](auto const&, auto const&) { drag->active = false; });
}

/// Help › About DiaDraw: the name, version, project page, licence and the open-source software the
/// app ships, with the address of the full list (its Credits page).
void show_about(HWND owner) {
  std::string text = std::string("DiaDraw ") + dd_version() + "\n\n" + dd::host::kSummary + "\n\n" +
                     dd::host::kProjectUrl + "\n\n" + dd::host::kCopyright + "\nFree software under the " +
                     dd::host::kLicense + ".\n\nOpen-source software:\n";
  for (const auto& credit : dd::host::credits_for("winui")) text += "  " + dd::host::credit_label(credit) + "\n";
  text += std::string("\nEverything DiaDraw uses, and what for:\n") + dd::host::kCreditsUrl;
  auto wtext = wide(text);
  MessageBoxW(owner, wtext.c_str(), L"About DiaDraw", MB_OK | MB_ICONINFORMATION);
}

}  // namespace

DocumentWindow::DocumentWindow(AppActions actions, std::string path, const std::string& text)
    : actions_(std::move(actions)), path_(std::move(path)) {
  // Light theme: the syntax colours are chosen for a light background.
  root_.RequestedTheme(ElementTheme::Light);
  root_.Background(Media::SolidColorBrush(Color{255, 255, 255, 255}));
  RowDefinition bar, content, status;
  bar.Height(GridLengthHelper::Auto());
  content.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  status.Height(GridLengthHelper::Auto());
  root_.RowDefinitions().Append(bar);
  root_.RowDefinitions().Append(content);
  root_.RowDefinitions().Append(status);

  auto toolbar = build_toolbar();
  Grid::SetRow(toolbar.as<FrameworkElement>(), 0);
  root_.Children().Append(toolbar);

  // Text | canvas, with a movable divider between them, as the macOS app's split view: the text
  // starts at 440, never drags narrower than 280, and the canvas takes whatever the window gains or
  // loses.
  Grid panes;
  ColumnDefinition left, divider, right;
  left.Width(GridLengthHelper::FromPixels(440));
  left.MinWidth(280);
  divider.Width(GridLengthHelper::FromPixels(8));
  right.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  panes.ColumnDefinitions().Append(left);
  panes.ColumnDefinitions().Append(divider);
  panes.ColumnDefinitions().Append(right);
  text_.FontFamily(Media::FontFamily(L"Consolas"));
  text_.FontSize(13);
  text_.TextWrapping(TextWrapping::NoWrap);
  text_.IsSpellCheckEnabled(false);
  text_.IsTextPredictionEnabled(false);
  text_.ClipboardCopyFormat(RichEditClipboardFormat::PlainText);
  text_.BorderThickness(ThicknessHelper::FromUniformLength(0));
  ScrollViewer::SetHorizontalScrollBarVisibility(text_, ScrollBarVisibility::Auto);
  text_.Document().UndoLimit(0);  // the window keeps the undo history
  // The fold markers in a gutter left of the text.
  Grid text_pane;
  ColumnDefinition gutter_column, text_column;
  gutter_column.Width(GridLengthHelper::Auto());
  text_column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  text_pane.ColumnDefinitions().Append(gutter_column);
  text_pane.ColumnDefinitions().Append(text_column);
  Grid::SetColumn(folds_.gutter(), 0);
  text_pane.Children().Append(folds_.gutter());
  Grid::SetColumn(text_, 1);
  text_pane.Children().Append(text_);
  Grid::SetColumn(text_pane, 0);
  panes.Children().Append(text_pane);
  auto canvas = canvas_.root().as<FrameworkElement>();
  Grid::SetColumn(canvas, 2);
  panes.Children().Append(canvas);
  add_divider(panes, left, right);
  Grid::SetRow(panes, 1);
  root_.Children().Append(panes);

  status_.Margin(ThicknessHelper::FromLengths(10, 4, 10, 4));
  status_.FontSize(12);
  status_.TextTrimming(TextTrimming::CharacterEllipsis);
  Grid::SetRow(status_, 2);
  root_.Children().Append(status_);

  window_.Content(root_);
  window_.AppWindow().Resize(winrt::Windows::Graphics::SizeInt32{1280, 800});
  window_.AppWindow().SetIcon(wide(executable_dir() + "/DiaDraw.ico"));

  // Typing: the document follows after a short pause; Ctrl+Z / Ctrl+Y use the window's history.
  timer_ = window_.DispatcherQueue().CreateTimer();
  timer_.Interval(std::chrono::milliseconds(60));
  timer_.IsRepeating(false);
  timer_.Tick([this](auto const&, auto const&) { sync_from_text(); });
  text_.TextChanged([this](auto const&, auto const&) {
    if (setting_) return;
    timer_.Stop();
    timer_.Start();
  });
  text_.SelectionChanged([this](auto const&, auto const&) {
    if (setting_) return;
    folds_.reveal_caret(true);
    show_caret_diagnostic();
  });
  text_.PreviewKeyDown([this](auto const&, KeyRoutedEventArgs const& e) {
    auto ctrl = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(VirtualKey::Control);
    if ((ctrl & winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) != winrt::Windows::UI::Core::CoreVirtualKeyStates::Down)
      return;
    if (e.Key() == VirtualKey::Z) {
      undo();
      e.Handled(true);
    } else if (e.Key() == VirtualKey::Y) {
      redo();
      e.Handled(true);
    }
  });

  // Canvas gestures become document changes.
  auto& c = canvas_.canvas().callbacks;
  c.selected = [this](const dd::editor::Selection& s) {
    if (s.elements.size() != 1) return;
    auto span = doc_->declaration_of(s.elements[0]);
    if (!span) return;
    const std::string& t = doc_->text();
    auto selection = text_.Document().Selection();
    setting_ = true;
    selection.SetRange(static_cast<int>(dd::utf16_offset(t, span->from)), static_cast<int>(dd::utf16_offset(t, span->to)));
    selection.ScrollIntoView(text::PointOptions::None);
    setting_ = false;
    folds_.reveal_caret();
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
    }
  };
  c.renamed = [this, run](const std::string& id, const std::string& name) { run([&] { return doc_->rename(id, name); }); };
  c.relabelled = [this, run](const std::string& id, const std::optional<std::string>& label) {
    run([&] { return doc_->relabel(id, label); });
  };
  c.deleted = [this, run](const dd::editor::Selection& s) { run([&] { return doc_->remove(s); }); };

  window_.Closed([this](auto const&, auto const&) {
    canvas_.close();
    if (on_closed) on_closed();
  });

  load(text.empty() && path_.empty() ? kDefaultText : normalize_line_breaks(text));
}

UIElement DocumentWindow::build_toolbar() {
  MenuBar menu;
  MenuBarItem file;
  file.Title(L"File");
  file.Items().Append(menu_item(L"New", actions_.new_document, VirtualKey::N));
  file.Items().Append(menu_item(L"Open…", actions_.open, VirtualKey::O));
  file.Items().Append(menu_item(L"Import…", actions_.import, VirtualKey::I,
                                VirtualKeyModifiers::Control | VirtualKeyModifiers::Shift));
  MenuFlyoutSubItem example_menu;
  example_menu.Text(L"Examples");
  for (const auto& e : dd::host::examples()) {
    winrt::hstring label = wide(std::string(e.name));
    example_menu.Items().Append(menu_item(label.c_str(), [this, name = e.name] { actions_.open_example(name); }));
  }
  file.Items().Append(example_menu);
  file.Items().Append(MenuFlyoutSeparator());
  file.Items().Append(menu_item(L"Save", [this] { save(); }, VirtualKey::S));
  file.Items().Append(menu_item(L"Save As…", [this] { save_as(); }, VirtualKey::S,
                                VirtualKeyModifiers::Control | VirtualKeyModifiers::Shift));
  file.Items().Append(menu_item(L"Cloud Storage…", [this] { show_cloud(); }));
  MenuFlyoutSubItem exports;
  exports.Text(L"Export");
  for (auto [label, ext] : {std::pair{L"SVG…", "svg"}, {L"PNG…", "png"}, {L"PDF…", "pdf"}, {L"draw.io…", "drawio"},
                            {L"BPMN…", "bpmn"}})
    exports.Items().Append(menu_item(label, [this, e = std::string(ext)] { export_as(e); }));
  file.Items().Append(exports);
  file.Items().Append(MenuFlyoutSeparator());
  file.Items().Append(menu_item(L"Close", [this] { window_.Close(); }, VirtualKey::W));
  file.Items().Append(menu_item(L"Quit", actions_.quit, VirtualKey::Q));
  menu.Items().Append(file);

  MenuBarItem edit;
  edit.Title(L"Edit");
  edit.Items().Append(menu_item(L"Undo", [this] { undo(); }));
  edit.Items().Append(menu_item(L"Redo", [this] { redo(); }));
  edit.Items().Append(menu_item(L"Delete", [this] { canvas_.delete_selection(); }));
  edit.Items().Append(menu_item(L"Clear Canvas", [this] { ask_clear_canvas(); }));
  // F2, as VS Code and the other apps rename: the element named at the text cursor, or the one
  // selected on the canvas.
  edit.Items().Append(menu_item(L"Rename Element…", [this] { ask_rename_element(); }, VirtualKey::F2,
                                VirtualKeyModifiers::None));
  edit.Items().Append(menu_item(L"Size to Fit Content", [this] {
    std::vector<std::string> elements = canvas_.canvas().selection().elements;
    std::string before = doc_->text();
    apply(doc_->fit(elements), before);
  }));
  edit.Items().Append(MenuFlyoutSeparator());
  // The text: Format Document (Shift+Alt+F, as in VS Code and the web app) and its one setting.
  edit.Items().Append(menu_item(L"Format Document", [this] { format_text(); }, VirtualKey::F,
                                VirtualKeyModifiers::Shift | VirtualKeyModifiers::Menu));
  MenuFlyoutSubItem indent;
  indent.Text(L"Indent Size");
  for (int size : dd::host::kIndentSizes) {
    RadioMenuFlyoutItem item;
    item.Text(winrt::to_hstring(size));
    item.GroupName(L"indent-size");
    item.Click([this, size](auto const&, auto const&) { set_indent_size(size, true); });
    indent.Items().Append(item);
    indent_items_.push_back(item);
  }
  edit.Items().Append(indent);
  set_indent_size(dd::host::read_indent_size(), false);
  menu.Items().Append(edit);

  MenuBarItem view;
  view.Title(L"View");
  view.Items().Append(menu_item(L"Zoom In", [this] { canvas_.set_zoom(canvas_.zoom() * 1.25); }, VirtualKey::Add));
  view.Items().Append(menu_item(L"Zoom Out", [this] { canvas_.set_zoom(canvas_.zoom() / 1.25); }, VirtualKey::Subtract));
  view.Items().Append(menu_item(L"Actual Size", [this] { canvas_.set_zoom(1); }, VirtualKey::Number0));
  view.Items().Append(MenuFlyoutSeparator());
  // No Ctrl+Alt shortcut: Windows reports AltGr as Ctrl+Alt, and AltGr+N types a letter on
  // some layouts (Polish ń).
  view.Items().Append(menu_item(L"New View…", [this] { ask_new_view(); }));
  view.Items().Append(menu_item(L"Rename View…", [this] { ask_rename_view(); }));
  remove_view_item_ = menu_item(L"Remove View", [this] { remove_view(); });
  view.Items().Append(remove_view_item_);
  view.Items().Append(MenuFlyoutSeparator());
  // The grid, every 10 diagram units: drags, resizes and bend points line up with it.
  grid_item_.Text(L"Show Grid");
  grid_item_.Click([this](auto const&, auto const&) {
    // The canvas decides and the check mark follows it, whenever the item flips its own.
    canvas_.canvas().set_grid(canvas_.canvas().grid() > 0 ? 0 : 10);
    grid_item_.IsChecked(canvas_.canvas().grid() > 0);
    canvas_.redraw();
  });
  view.Items().Append(grid_item_);
  menu.Items().Append(view);

  MenuBarItem help;
  help.Title(L"Help");
  help.Items().Append(menu_item(L"Mouse and Keyboard", [this] { show_help(); }, VirtualKey::F1,
                                VirtualKeyModifiers::None));
  help.Items().Append(MenuFlyoutSeparator());
  help.Items().Append(menu_item(L"Report a Bug…", [this] { open_feedback(dd::host::FeedbackKind::Bug, handle_of(window_)); }));
  help.Items().Append(menu_item(L"Give Feedback…", [this] { open_feedback(dd::host::FeedbackKind::Feedback, handle_of(window_)); }));
  help.Items().Append(MenuFlyoutSeparator());
  help.Items().Append(menu_item(L"About DiaDraw", [this] { show_about(handle_of(window_)); }));
  menu.Items().Append(help);

  views_.MinWidth(180);
  views_.SelectionChanged([this](auto const&, auto const&) {
    if (updating_views_) return;
    int i = views_.SelectedIndex();
    if (i >= 0 && i < static_cast<int>(doc_->views().size())) {
      doc_->choose_view(doc_->views()[i]);
      refresh();
    }
  });
  // Beside the picker: add, rename and remove views.
  MenuFlyout view_actions;
  view_actions.Items().Append(menu_item(L"New View…", [this] { ask_new_view(); }));
  view_actions.Items().Append(menu_item(L"Rename View…", [this] { ask_rename_view(); }));
  remove_view_button_item_ = menu_item(L"Remove View", [this] { remove_view(); });
  view_actions.Items().Append(remove_view_button_item_);
  DropDownButton view_button;
  view_button.Content(winrt::box_value(L"\u22EF"));  // ⋯
  view_button.Flyout(view_actions);
  ToolTipService::SetToolTip(view_button, winrt::box_value(L"Add, rename or remove views"));
  // One drop-down button: choosing a kind adds an element of it.
  MenuFlyout kinds;
  for (const auto& k : dd::host::element_kinds()) {
    MenuFlyoutItem item;
    item.Text(wide(k));
    item.Click([this, k](auto const&, auto const&) { add_element(k); });
    kinds.Items().Append(item);
  }
  DropDownButton add;
  add.Content(winrt::box_value(L"Add"));
  add.Flyout(kinds);
  ToolTipService::SetToolTip(add, winrt::box_value(L"Add an element of the chosen kind"));

  StackPanel bar;
  bar.Orientation(Orientation::Horizontal);
  bar.Spacing(8);
  bar.Padding(ThicknessHelper::FromLengths(4, 2, 8, 2));
  bar.Children().Append(menu);
  bar.Children().Append(views_);
  bar.Children().Append(view_button);
  bar.Children().Append(add);

  // Give Feedback: always in sight and one click away, at the right end of the toolbar.
  Button feedback;
  feedback.Content(winrt::box_value(L"Give Feedback"));
  // The accent colour, from the XAML controls' resources the app merges in (main.cpp).
  // Named in full: dd::Style (model.hpp) hides the XAML Style the using-directive brings in.
  if (auto accent = Application::Current().Resources().TryLookup(winrt::box_value(L"AccentButtonStyle")))
    if (auto style = accent.try_as<winrt::Microsoft::UI::Xaml::Style>()) feedback.Style(style);
  feedback.Click([this](auto const&, auto const&) { open_feedback(dd::host::FeedbackKind::Feedback, handle_of(window_)); });
  ToolTipService::SetToolTip(feedback,
                             winrt::box_value(L"Send an idea, a wish or anything that could be easier (opens a form on GitHub)"));
  feedback.VerticalAlignment(VerticalAlignment::Center);

  // Help: what the canvas does with the mouse and the keyboard, beside Give Feedback.
  Button help_button;  // `help` is the Help menu above, in the same scope
  help_button.Content(winrt::box_value(L"Help"));
  help_button.Click([this](auto const&, auto const&) { show_help(); });
  ToolTipService::SetToolTip(help_button, winrt::box_value(L"What the canvas does with the mouse and the keyboard"));
  help_button.VerticalAlignment(VerticalAlignment::Center);
  StackPanel end;
  end.Orientation(Orientation::Horizontal);
  end.Spacing(8);
  end.Children().Append(help_button);
  end.Children().Append(feedback);

  Grid toolbar;
  ColumnDefinition left, right;
  left.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  right.Width(GridLengthHelper::Auto());
  toolbar.ColumnDefinitions().Append(left);
  toolbar.ColumnDefinitions().Append(right);
  toolbar.Padding(ThicknessHelper::FromLengths(0, 0, 8, 0));
  Grid::SetColumn(bar, 0);
  Grid::SetColumn(end, 1);
  toolbar.Children().Append(bar);
  toolbar.Children().Append(end);
  return toolbar;
}

void DocumentWindow::show(bool activate) {
  if (activate) {
    window_.Activate();
  } else {
    // A test run: never take focus, and ignore real pointer input (scripts call in directly).
    root_.IsHitTestVisible(false);
    window_.AppWindow().Show(false);
  }
}

// ── The text ──

std::string DocumentWindow::box_text() const {
  winrt::hstring s;
  text_.Document().GetText(text::TextGetOptions::None, s);
  std::wstring w(s);
  // RichEdit ends with a paragraph mark of its own and separates lines with \r.
  if (!w.empty() && w.back() == L'\r') w.pop_back();
  std::replace(w.begin(), w.end(), L'\r', L'\n');
  return winrt::to_string(w);
}

void DocumentWindow::set_box_text(const std::string& t) {
  setting_ = true;
  text_.Document().SetText(text::TextSetOptions::None, wide(t));
  setting_ = false;
}

void DocumentWindow::load(const std::string& t) {
  std::string file = path_.empty() ? executable_dir() + "/untitled.draw" : path_;
  doc_ = std::make_unique<dd::host::Document>(t, file);
  doc_->attach(canvas_.canvas());
  set_box_text(t);
  committed_ = saved_ = box_text();
  if (committed_ != t) doc_->set_text(committed_);
  undo_.clear();
  redo_.clear();
  update_title();
  refresh();
}

void DocumentWindow::sync_from_text() {
  timer_.Stop();
  std::string t = box_text();
  if (t == committed_) return;
  undo_.push_back(committed_);
  redo_.clear();
  committed_ = t;
  doc_->set_text(t);
  update_title();
  refresh();
}

void DocumentWindow::restore(const std::string& t) {
  int caret = text_.Document().Selection().StartPosition();
  set_box_text(t);
  committed_ = box_text();
  setting_ = true;
  text_.Document().Selection().SetRange(caret, caret);
  setting_ = false;
  doc_->set_text(committed_);
  update_title();
  refresh();
}

void DocumentWindow::undo() {
  sync_from_text();
  if (undo_.empty()) return;
  redo_.push_back(committed_);
  std::string t = std::move(undo_.back());
  undo_.pop_back();
  restore(t);
}

void DocumentWindow::redo() {
  sync_from_text();
  if (redo_.empty()) return;
  undo_.push_back(committed_);
  std::string t = std::move(redo_.back());
  redo_.pop_back();
  restore(t);
}

void DocumentWindow::apply(const Result<std::optional<dd::host::Change>>& result, const std::string& before) {
  if (!result) return flash(result.error().message, true);
  if (!*result) return;
  setting_ = true;
  auto document = text_.Document();
  for (const auto& e : (*result)->edits) {  // last first, so earlier offsets stay valid
    auto range = document.GetRange(static_cast<int>(dd::utf16_offset(before, e.from)),
                                   static_cast<int>(dd::utf16_offset(before, e.to)));
    range.SetText(text::TextSetOptions::None, wide(e.insert));
  }
  setting_ = false;
  undo_.push_back(before);
  redo_.clear();
  committed_ = box_text();
  if (committed_ != doc_->text()) restore(doc_->text());  // the box changed something; trust the document
  update_title();
  refresh();
  if ((*result)->note) flash(*(*result)->note);
}

void DocumentWindow::restyle() {
  auto document = text_.Document();
  setting_ = true;
  document.BatchDisplayUpdates();
  const std::string& t = committed_;
  auto map = utf16_map(t);
  auto all = document.GetRange(0, map[t.size()] + 1);
  all.CharacterFormat().ForegroundColor(kText);
  all.CharacterFormat().Underline(text::UnderlineType::None);
  all.CharacterFormat().Hidden(text::FormatEffect::Off);
  for (const auto& token : dd::dsl::highlight(t))
    if (auto c = colour_of(token.kind)) document.GetRange(map[token.from], map[token.to]).CharacterFormat().ForegroundColor(*c);
  for (const auto& m : marks_) {
    auto format = document.GetRange(m.from, m.to).CharacterFormat();
    format.Underline(text::UnderlineType::Wave);
    format.ForegroundColor(m.error ? kError : kWarning);
  }
  folds_.update(t, map);
  folds_.hide(document);
  document.ApplyDisplayUpdates();
  setting_ = false;
  folds_.redraw();
}

void DocumentWindow::refresh() {
  auto refreshed = doc_->refresh();
  updating_views_ = true;
  views_.Items().Clear();
  for (const auto& id : doc_->views()) views_.Items().Append(winrt::box_value(wide("view " + id)));
  const auto& views = doc_->views();
  auto it = std::find(views.begin(), views.end(), doc_->view());
  views_.SelectedIndex(it == views.end() ? -1 : static_cast<int>(it - views.begin()));
  views_.IsEnabled(views.size() > 1);  // nothing to choose between with one view
  remove_view_item_.IsEnabled(views.size() > 1);  // a document keeps one view
  remove_view_button_item_.IsEnabled(views.size() > 1);
  updating_views_ = false;
  if (refreshed.render) canvas_.show_render(*refreshed.render);
  else canvas_.clear();

  marks_.clear();
  const std::string& t = doc_->text();
  if (t == committed_) {
    auto map = utf16_map(t);
    int length = map[t.size()];
    for (const auto& d : doc_->session().parsed().diagnostics) {
      int from = map[std::min<std::size_t>(d.span.from, t.size())];
      int to = map[std::min<std::size_t>(std::max(d.span.to, d.span.from), t.size())];
      if (to == from) {
        if (to < length) ++to;
        else if (from > 0) --from;
      }
      marks_.push_back({from, to, d.severity == dd::dsl::Severity::Error, d.message});
    }
  }
  restyle();
  summary_ = refreshed.status;
  summary_error_ = refreshed.error;
  showing_mark_ = false;
  flash(summary_, summary_error_);
  show_caret_diagnostic();
}

void DocumentWindow::show_caret_diagnostic() {
  int caret = text_.Document().Selection().StartPosition();
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
  status_.Text(wide(message));
  status_.Foreground(Media::SolidColorBrush(error ? kError : Color{255, 96, 96, 100}));
}

void DocumentWindow::add_element(const std::string& kind) {
  sync_from_text();
  std::string created, before = doc_->text();
  apply(doc_->add(kind, &created), before);
  if (!created.empty()) {
    canvas_.canvas().set_selection({{created}, {}});
    canvas_.redraw();
  }
}

// ── Views ──

void DocumentWindow::new_view(const std::string& id) {
  sync_from_text();
  std::string before = doc_->text();
  apply(doc_->add_view(id), before);
}

void DocumentWindow::rename_view(const std::string& id) {
  sync_from_text();
  std::string before = doc_->text();
  apply(doc_->rename_view(id), before);
}

void DocumentWindow::remove_view() {
  sync_from_text();
  std::string before = doc_->text();
  apply(doc_->remove_view(), before);
}

winrt::fire_and_forget DocumentWindow::ask_clear_canvas() {
  sync_from_text();
  // Saved or untouched, undo is enough; an empty diagram has nothing to clear.
  if (committed_ == saved_ || committed_ == dd::host::Document::kEmptyText) {
    clear_canvas();
    co_return;
  }
  // One dialog at a time, as ask_name: ShowAsync throws while another is open.
  if (asking_) co_return;
  asking_ = true;
  std::weak_ptr<char> alive = alive_;
  ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.Title(winrt::box_value(L"Clear the canvas?"));
  dialog.Content(winrt::box_value(wide(std::string(dd::host::Document::kClearDetail))));
  dialog.PrimaryButtonText(L"Clear");
  dialog.CloseButtonText(L"Cancel");
  dialog.DefaultButton(ContentDialogButton::Close);  // Enter keeps the changes
  ContentDialogResult result = ContentDialogResult::None;
  try {
    result = co_await dialog.ShowAsync();
  } catch (const winrt::hresult_error&) {
  }
  if (alive.expired()) co_return;  // the window closed while the dialog was open
  asking_ = false;
  if (result == ContentDialogResult::Primary) clear_canvas();
}

void DocumentWindow::format_text() {
  sync_from_text();  // format what is typed, not what was last read
  // Another window may have chosen a size since: the settings file has the latest.
  if (!keep_indent_) set_indent_size(dd::host::read_indent_size(), false);
  std::string before = doc_->text();
  apply(doc_->format(indent_size_), before);
}

void DocumentWindow::set_indent_size(int size, bool remember) {
  indent_size_ = size;
  for (std::size_t i = 0; i < indent_items_.size(); ++i)
    indent_items_[i].IsChecked(dd::host::kIndentSizes[i] == size);
  if (remember) keep_indent_ = !dd::host::write_indent_size(size);
}

void DocumentWindow::clear_canvas() {
  sync_from_text();  // the change replaces the whole text, typing included
  std::string before = doc_->text();
  apply(doc_->clear(), before);  // the refresh drops the selection, as nothing selected is left
}

void DocumentWindow::ask_new_view() {
  ask_name(L"New View", L"View name (letters, digits and _)", L"Add", doc_->fresh_view(),
           [this](const std::string& id) { new_view(id); });
}

void DocumentWindow::ask_rename_view() {
  ask_name(L"Rename View", L"View name (letters, digits and _)", L"Rename", doc_->view(),
           [this](const std::string& id) { rename_view(id); });
}

std::size_t DocumentWindow::caret_byte() {
  sync_from_text();
  int caret = std::max(0, text_.Document().Selection().StartPosition());
  return dd::byte_offset(committed_, static_cast<std::size_t>(caret));
}

void DocumentWindow::ask_rename_element() {
  // The text when it has the focus; otherwise the canvas's one selected element, or else the text
  // (a menu click can take the focus from it).
  bool in_text = text_.FocusState() != FocusState::Unfocused;
  std::size_t caret = caret_byte();
  auto element = doc_->element_to_rename(canvas_.canvas().selection(), in_text, caret);
  auto fields = element ? doc_->fields(*element) : std::nullopt;
  if (!fields) {
    flash("Put the cursor on an element's name, or select one element, to rename it", true);
    return;
  }
  ask_name(L"Rename Element", wide("New name for " + *element + " (letters, digits and _)"), L"Rename",
           fields->local_id, [this, id = *element](const std::string& to) { rename_element(id, to); });
}

void DocumentWindow::rename_element(const std::string& element, const std::string& to) {
  sync_from_text();
  std::string before = doc_->text(), renamed;
  auto selection = canvas_.canvas().selection();
  apply(doc_->rename_element(element, to, &renamed), before);
  if (renamed.empty() || renamed == element) return;
  canvas_.canvas().set_selection(doc_->follow_rename(std::move(selection), element, renamed));
  canvas_.redraw();
}

winrt::fire_and_forget DocumentWindow::show_help() {
  // One dialog at a time, as ask_name: ShowAsync throws while another is open.
  if (asking_) co_return;
  asking_ = true;
  std::weak_ptr<char> alive = alive_;
  // A heading and a two-column list per section: the keys, then what they do.
  StackPanel sections;
  sections.Spacing(4);
  bool first = true;
  for (const auto& section : dd::host::canvas_help(dd::host::HelpKeys::windows)) {
    TextBlock heading;
    heading.Text(wide(section.title));
    heading.FontWeight(text::FontWeights::SemiBold());
    if (!first) heading.Margin(ThicknessHelper::FromLengths(0, 12, 0, 0));
    first = false;
    sections.Children().Append(heading);
    Grid rows;
    ColumnDefinition keys, action;
    keys.Width(GridLengthHelper::FromPixels(200));
    action.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    rows.ColumnDefinitions().Append(keys);
    rows.ColumnDefinitions().Append(action);
    rows.ColumnSpacing(16);
    rows.RowSpacing(4);
    int row = 0;
    for (const auto& entry : section.entries) {
      rows.RowDefinitions().Append(RowDefinition());
      TextBlock k, a;
      k.Text(wide(entry.keys));
      k.TextWrapping(TextWrapping::Wrap);
      k.Opacity(0.7);
      a.Text(wide(entry.action));
      a.TextWrapping(TextWrapping::Wrap);
      Grid::SetRow(k, row);
      Grid::SetRow(a, row);
      Grid::SetColumn(a, 1);
      rows.Children().Append(k);
      rows.Children().Append(a);
      ++row;
    }
    sections.Children().Append(rows);
  }
  ScrollViewer scroll;
  scroll.Content(sections);
  scroll.MaxHeight(520);
  ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.Title(winrt::box_value(L"Mouse and Keyboard"));
  dialog.Content(scroll);
  dialog.CloseButtonText(L"Close");
  dialog.DefaultButton(ContentDialogButton::Close);
  try {
    co_await dialog.ShowAsync();
  } catch (const winrt::hresult_error&) {
  }
  if (alive.expired()) co_return;  // the window closed while the dialog was open
  asking_ = false;
}

winrt::fire_and_forget DocumentWindow::ask_name(winrt::hstring title, winrt::hstring header, winrt::hstring verb,
                                                std::string initial, std::function<void(const std::string&)> done) {
  // One dialog at a time: ShowAsync throws while another is open on the same root, and an
  // exception escaping a fire_and_forget coroutine ends the app.
  if (asking_) co_return;
  asking_ = true;
  std::weak_ptr<char> alive = alive_;
  TextBox name;
  name.Text(wide(initial));
  name.Header(winrt::box_value(header));
  name.SelectAll();
  ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.Title(winrt::box_value(title));
  dialog.Content(name);
  dialog.PrimaryButtonText(verb);
  dialog.CloseButtonText(L"Cancel");
  dialog.DefaultButton(ContentDialogButton::Primary);
  ContentDialogResult result = ContentDialogResult::None;
  try {
    result = co_await dialog.ShowAsync();
  } catch (const winrt::hresult_error&) {
  }
  if (alive.expired()) co_return;  // the window closed while the dialog was open
  asking_ = false;
  if (result == ContentDialogResult::Primary) done(narrow(name.Text()));
}

// ── Files ──

void DocumentWindow::update_title() {
  std::string name = cloud_ ? cloud_->name + " (" + std::string(dd::host::cloud::label(cloud_->provider)) + ")"
                     : path_.empty() ? "Untitled"
                                     : path_.substr(path_.find_last_of("/\\") + 1);
  window_.Title(wide((committed_ != saved_ ? "• " : "") + name + " — DiaDraw"));
}

void DocumentWindow::write_to(const std::string& path) {
  sync_from_text();
  std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
  out << committed_;
  if (!out) return flash("Could not save " + path, true);
  path_ = path;
  cloud_.reset();
  saved_ = committed_;
  update_title();
}

void DocumentWindow::save() {
  if (cloud_) {
    namespace cloud = dd::host::cloud;
    sync_from_text();
    std::weak_ptr<char> alive = alive_;
    flash("Saving to " + std::string(cloud::label(cloud_->provider)) + "…");
    cloud_saver_.save(post_to(window_.DispatcherQueue()), *cloud_, committed_,
                      [this, alive](const cloud::SaveOutcome& outcome) {
                        if (alive.expired()) return;
                        // The sign-in ran out: sign in from the cloud window, then save again.
                        if (outcome.needs_sign_in) return show_cloud(outcome.place.provider, outcome.message);
                        if (!outcome.ok) return flash(outcome.message, true);
                        // Typing meanwhile, or a move to another place, keeps the document modified.
                        if (cloud_ && cloud_->provider == outcome.place.provider && cloud_->id == outcome.place.id)
                          saved_ = outcome.text;
                        update_title();
                        flash(outcome.message);
                      });
    return;
  }
  if (path_.empty()) return save_as();
  write_to(path_);
}

void DocumentWindow::set_cloud(const dd::host::cloud::Place& place, const std::string& text) {
  sync_from_text();
  cloud_ = place;
  last_provider_ = place.provider;
  path_.clear();
  saved_ = text;
  update_title();
}

void DocumentWindow::show_cloud(std::optional<dd::host::cloud::Provider> provider, const std::string& message) {
  namespace cloud = dd::host::cloud;
  if (!cloud_window_) {
    CloudWindow::Callbacks callbacks;
    callbacks.text = [this] {
      sync_from_text();
      return committed_;
    };
    callbacks.suggested_name = [this] {
      return cloud_ ? cloud::plain_name(cloud_->name)
                    : path_.empty() ? std::string("Untitled")
                                    : cloud::plain_name(path_.substr(path_.find_last_of("/\\") + 1));
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
      sync_from_text();
      if (path_.empty() && !cloud_ && committed_ == saved_) {
        load(normalize_line_breaks(text));
        set_cloud(place, committed_);
      } else if (actions_.open_text) {
        if (auto* window = actions_.open_text(text)) window->set_cloud(place, window->committed_);
      }
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
        saved_.clear();
        update_title();
      }
    };
    cloud_window_ = std::make_unique<CloudWindow>(std::move(callbacks));
  }
  const auto chosen = provider ? *provider : cloud_ ? cloud_->provider : last_provider_;
  last_provider_ = chosen;
  cloud_window_->show(chosen, message);
}

void DocumentWindow::save_as() {
  std::string name = path_.empty() ? "untitled.draw" : path_.substr(path_.find_last_of("/\\") + 1);
  if (auto path = choose_save(handle_of(window_), name)) write_to(*path);
}

void DocumentWindow::export_as(const std::string& extension) {
  sync_from_text();
  Result<std::string> data = extension == "svg"      ? doc_->svg()
                             : extension == "png"    ? doc_->png()
                             : extension == "pdf"    ? doc_->pdf()
                             : extension == "drawio" ? Result<std::string>(doc_->drawio())
                                                     : Result<std::string>(doc_->bpmn());
  if (!data) return flash(data.error().message, true);
  std::string base = path_.empty() ? "untitled" : path_.substr(path_.find_last_of("/\\") + 1);
  if (auto dot = base.rfind('.'); dot != std::string::npos) base.resize(dot);
  auto path = choose_save(handle_of(window_), base + "." + extension);
  if (!path) return;
  std::ofstream out(std::filesystem::u8path(*path), std::ios::binary);
  out << *data;
  if (!out) flash("Could not write " + *path, true);
}

// ── Scripted tests ──

std::string DocumentWindow::run_script(const std::string& script) {
  std::ostringstream log;
  sync_from_text();
  canvas_.scripted = true;
  std::istringstream lines(script);
  std::string raw;
  while (std::getline(lines, raw)) {
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
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
      auto p = c.center_of(w[1]);
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
    } else if (cmd == "delete" && w.size() > 1) {
      auto p = c.center_of(w[1]);
      if ((ok = p.has_value())) {
        click(*p);
        canvas_.delete_selection();
      }
    } else if (cmd == "grid" && w.size() > 1) {
      // grid <step>: shows the grid with that step in diagram units (0 hides it)
      c.set_grid(std::stod(w[1]));
      grid_item_.IsChecked(c.grid() > 0);
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
      auto element = cmd == "rename-at" ? doc_->element_at(caret_byte())
                                        : doc_->element_to_rename(canvas_.canvas().selection(), false,
                                                                  dd::host::Document::kNoCaret);
      if ((ok = element.has_value())) rename_element(*element, rest(1));
    } else if (cmd == "rename-view" && w.size() > 1) {
      rename_view(w[1]);
    } else if (cmd == "remove-view") {
      remove_view();
    } else if (cmd == "clear-canvas") {
      clear_canvas();
    } else if (cmd == "indent" && w.size() > 1) {
      // indent <size>: chooses Indent Size for this run (the settings file stays as it is)
      keep_indent_ = true;
      set_indent_size(std::stoi(w[1]), false);
    } else if (cmd == "format") {
      format_text();
    } else if (cmd == "fold" && w.size() > 1) {
      // fold <line>: clicks the fold marker on that line (1-based), folding or unfolding its block
      ok = folds_.toggle(std::stoi(w[1]) - 1);
    } else if (cmd == "folds") {
      // folds: logs the lines with a fold marker, the folded ones marked with *
      log << "folds:";
      for (const auto& m : folds_.markers()) log << " " << m.line + 1 << (m.folded ? "*" : "");
      log << "\n";
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
      std::string t = committed_ + rest(1) + "\n";
      int caret = static_cast<int>(dd::utf16_offset(committed_, committed_.size()));
      text_.Document().GetRange(caret, caret).SetText(text::TextSetOptions::None, wide(rest(1) + "\n"));
      sync_from_text();
      ok = committed_ == t;
    } else if (cmd == "caret" && w.size() > 2) {
      int line = std::stoi(w[1]), column = std::stoi(w[2]);
      std::size_t at = 0;
      for (int l = 1; l < line && at < committed_.size(); ++l) {
        std::size_t nl = committed_.find('\n', at);
        at = nl == std::string::npos ? committed_.size() : nl + 1;
      }
      int offset = static_cast<int>(dd::utf16_offset(committed_, at)) + column - 1;
      text_.Document().Selection().SetRange(offset, offset);
      show_caret_diagnostic();
    } else {
      ok = false;
    }
    log << (ok ? "ok   " : "FAIL ") << raw << "\n";
  }
  canvas_.scripted = false;
  log << "--- text\n" << committed_ << "--- status\n" << narrow(status_.Text()) << "\n";
  return log.str();
}

winrt::fire_and_forget DocumentWindow::snapshot(std::string path, std::function<void()> done) {
  using namespace winrt::Windows::Graphics::Imaging;
  using namespace winrt::Windows::Storage::Streams;
  Media::Imaging::RenderTargetBitmap bitmap;
  co_await bitmap.RenderAsync(root_);
  IBuffer pixels = co_await bitmap.GetPixelsAsync();
  InMemoryRandomAccessStream stream;
  BitmapEncoder encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
  encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                       static_cast<uint32_t>(bitmap.PixelWidth()), static_cast<uint32_t>(bitmap.PixelHeight()), 96, 96,
                       winrt::array_view<uint8_t const>(pixels.data(), pixels.data() + pixels.Length()));
  co_await encoder.FlushAsync();
  auto size = static_cast<uint32_t>(stream.Size());
  stream.Seek(0);
  IBuffer png = co_await stream.ReadAsync(Buffer(size), size, InputStreamOptions::None);
  std::ofstream(std::filesystem::u8path(path), std::ios::binary)
      .write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.Length()));
  if (done) done();
}

}  // namespace dd::winui
