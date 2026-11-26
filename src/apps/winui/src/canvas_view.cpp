#include "canvas_view.hpp"

#include <algorithm>

#include "painter.hpp"

namespace dd::winui {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using winrt::Windows::System::VirtualKey;
using winrt::Windows::System::VirtualKeyModifiers;

CanvasView::CanvasView() {
  // DoubleTapped comes after PointerPressed has reported the second click as a single press.
  canvas_.set_double_click_repeats_press(true);
  // A grid stacks the drawing and the overlay that holds the inline editor.
  layers_.HorizontalAlignment(HorizontalAlignment::Left);
  layers_.VerticalAlignment(VerticalAlignment::Top);
  layers_.Children().Append(control_);
  layers_.Children().Append(overlay_);
  scroll_.Content(layers_);
  scroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroll_.ZoomMode(ZoomMode::Disabled);
  scroll_.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{255, 248, 250, 252}));

  control_.IsTabStop(true);
  control_.ClearColor(winrt::Windows::UI::Color{255, 248, 250, 252});
  control_.Draw([this](auto const&, winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasDrawEventArgs const& args) {
    auto ds = args.DrawingSession();
    if (!canvas_.list()) return;
    float z = static_cast<float>(canvas_.zoom());
    ds.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_scale(z));
    paint(ds, canvas_.backdrop(canvas_.width(), canvas_.height()));
    paint(ds, *canvas_.list());
    paint(ds, canvas_.overlay());
  });

  control_.PointerPressed([this](auto const&, PointerRoutedEventArgs const& e) {
    auto point = e.GetCurrentPoint(control_);
    if (!point.Properties().IsLeftButtonPressed() || edit_) return;
    control_.CapturePointer(e.Pointer());
    control_.Focus(FocusState::Pointer);
    pressed_ = true;
    press(point.Position().X, point.Position().Y, 1, modifiers_of(e.KeyModifiers()));
    e.Handled(true);
  });
  control_.PointerMoved([this](auto const&, PointerRoutedEventArgs const& e) {
    if (!pressed_) return;
    auto p = e.GetCurrentPoint(control_).Position();
    drag_to(p.X, p.Y, modifiers_of(e.KeyModifiers()));
    e.Handled(true);
  });
  control_.PointerReleased([this](auto const&, PointerRoutedEventArgs const& e) {
    if (!pressed_) return;
    pressed_ = false;
    auto p = e.GetCurrentPoint(control_).Position();
    control_.ReleasePointerCapture(e.Pointer());
    release(p.X, p.Y, modifiers_of(e.KeyModifiers()));
    e.Handled(true);
  });
  control_.PointerCaptureLost([this](auto const&, auto const&) { pressed_ = false; });
  // Double-click: rename or relabel in place.
  control_.DoubleTapped([this](auto const&, DoubleTappedRoutedEventArgs const& e) {
    if (edit_) return;
    auto p = e.GetPosition(control_);
    press(p.X, p.Y, 2, {});
    e.Handled(true);
  });
  // Ctrl+wheel zooms.
  control_.PointerWheelChanged([this](auto const&, PointerRoutedEventArgs const& e) {
    if ((e.KeyModifiers() & VirtualKeyModifiers::Control) != VirtualKeyModifiers::Control) return;
    int delta = e.GetCurrentPoint(control_).Properties().MouseWheelDelta();
    set_zoom(zoom() * (delta > 0 ? 1.1 : 1 / 1.1));
    e.Handled(true);
  });
  control_.KeyDown([this](auto const&, KeyRoutedEventArgs const& e) {
    if (e.Key() == VirtualKey::Delete || e.Key() == VirtualKey::Back) {
      delete_selection();
      e.Handled(true);
    } else if (e.Key() == VirtualKey::Escape) {
      canvas_.escape();
      redraw();
      e.Handled(true);
    }
  });

  // The inline editor: Enter commits (a new line in a name, where Ctrl+Enter commits), Escape
  // cancels, leaving it commits. A multi-line TextBox handles Enter itself, so the keys are read
  // before it sees them.
  editor_.Visibility(Visibility::Collapsed);
  overlay_.Children().Append(editor_);
  editor_.PreviewKeyDown([this](auto const&, KeyRoutedEventArgs const& e) {
    if (e.Key() == VirtualKey::Enter) {
      auto ctrl = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(VirtualKey::Control);
      bool ctrl_down = (ctrl & winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) ==
                       winrt::Windows::UI::Core::CoreVirtualKeyStates::Down;
      if (edit_ && edit_->multiline() && !ctrl_down) return;  // the TextBox inserts the line break
      finish_editing(true);
      e.Handled(true);
    } else if (e.Key() == VirtualKey::Escape) {
      finish_editing(false);
      e.Handled(true);
    }
  });
  editor_.LostFocus([this](auto const&, auto const&) {
    if (!scripted) finish_editing(true);
  });
}

dd::canvas::Modifiers CanvasView::modifiers_of(VirtualKeyModifiers keys) {
  auto has = [&](VirtualKeyModifiers m) { return (keys & m) == m; };
  return {has(VirtualKeyModifiers::Shift), has(VirtualKeyModifiers::Menu), has(VirtualKeyModifiers::Control)};
}

void CanvasView::show_render(const dd::editor::ViewRender& render) {
  canvas_.show(render);
  update_size();
  redraw();
}

void CanvasView::clear() {
  canvas_.clear();
  update_size();
  redraw();
}

void CanvasView::set_zoom(double zoom) {
  canvas_.set_zoom(zoom);
  update_size();
  redraw();
}

void CanvasView::close() { control_.RemoveFromVisualTree(); }

void CanvasView::update_size() {
  double w = std::max(1.0, canvas_.width()), h = std::max(1.0, canvas_.height());
  control_.Width(w);
  control_.Height(h);
  overlay_.Width(w);
  overlay_.Height(h);
}

void CanvasView::press(double x, double y, int clicks, dd::canvas::Modifiers modifiers) {
  std::optional<dd::canvas::Edit> edit;
  if (canvas_.press(x, y, clicks, modifiers, &edit)) redraw();
  if (edit) start_editing(*edit);
}

void CanvasView::drag_to(double x, double y, dd::canvas::Modifiers modifiers) {
  if (canvas_.drag(x, y, modifiers)) redraw();
}

void CanvasView::release(double x, double y, dd::canvas::Modifiers modifiers) {
  if (canvas_.release(x, y, modifiers)) redraw();
}

void CanvasView::delete_selection() {
  if (canvas_.delete_selection()) redraw();
}

void CanvasView::start_editing(const dd::canvas::Edit& edit) {
  edit_ = edit;
  // A name takes several lines and wraps to the field; a label stays on its one line.
  editor_.AcceptsReturn(edit.multiline());
  editor_.TextWrapping(edit.multiline() ? TextWrapping::Wrap : TextWrapping::NoWrap);
  editor_.Text(wide(edit.value));
  editor_.Width(std::max(120.0, edit.width));
  Controls::Canvas::SetLeft(editor_, std::max(0.0, edit.x));
  Controls::Canvas::SetTop(editor_, std::max(0.0, edit.y + edit.height / 2 - 16));
  editor_.Visibility(Visibility::Visible);
  editor_.Focus(FocusState::Programmatic);
  editor_.SelectAll();
}

void CanvasView::finish_editing(bool save) {
  if (!edit_) return;
  auto edit = std::move(*edit_);
  edit_.reset();
  std::string value = narrow(editor_.Text());
  editor_.Visibility(Visibility::Collapsed);
  control_.Focus(FocusState::Programmatic);
  if (save) canvas_.commit_edit(edit, value);
}

bool CanvasView::commit_editing(const std::string& text) {
  if (!edit_) return false;
  editor_.Text(wide(text));
  finish_editing(true);
  return true;
}

}  // namespace dd::winui
