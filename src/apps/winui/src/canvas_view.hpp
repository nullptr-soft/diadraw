// The diagram as an editor: a WinUI host for dd::canvas::Canvas. A Win2D CanvasControl paints the
// core's display list and overlay; a TextBox over it does inline renaming (on several lines) and
// relabelling. Pointer and key events go to the shared canvas, which reports gestures through its
// callbacks (wired by the window).

#pragma once

#include "winrt.hpp"

#include <optional>
#include <string>

#include "diadraw/canvas/canvas.hpp"

namespace dd::winui {

class CanvasView {
 public:
  CanvasView();

  /// The element to put in the window (a scroll viewer around the canvas).
  [[nodiscard]] winrt::Microsoft::UI::Xaml::UIElement root() const { return scroll_; }
  dd::canvas::Canvas& canvas() { return canvas_; }

  void show_render(const dd::editor::ViewRender& render);
  void clear();
  void set_zoom(double zoom);
  [[nodiscard]] double zoom() const { return canvas_.zoom(); }
  void redraw() { control_.Invalidate(); }
  /// Win2D's control must leave the visual tree when its window closes.
  void close();

  // What a pointer does, in canvas coordinates; the input events and test scripts both call these.
  void press(double x, double y, int clicks, dd::canvas::Modifiers modifiers);
  void drag_to(double x, double y, dd::canvas::Modifiers modifiers);
  void release(double x, double y, dd::canvas::Modifiers modifiers);
  void delete_selection();

  [[nodiscard]] bool editing() const { return edit_.has_value(); }
  /// Finishes an open inline edit with `text` (for test scripts); false when none is open.
  bool commit_editing(const std::string& text);
  /// While a test script runs, focus changes do not end inline editing.
  bool scripted = false;

 private:
  void update_size();
  void start_editing(const dd::canvas::Edit& edit);
  void finish_editing(bool save);
  static dd::canvas::Modifiers modifiers_of(winrt::Windows::System::VirtualKeyModifiers keys);

  dd::canvas::Canvas canvas_;
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroll_;
  winrt::Microsoft::UI::Xaml::Controls::Grid layers_;
  winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl control_;
  winrt::Microsoft::UI::Xaml::Controls::Canvas overlay_;
  winrt::Microsoft::UI::Xaml::Controls::TextBox editor_;
  std::optional<dd::canvas::Edit> edit_;
  bool pressed_ = false;
};

}  // namespace dd::winui
