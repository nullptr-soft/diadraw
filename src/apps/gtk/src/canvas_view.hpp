// The diagram as an editor: a GTK host for dd::canvas::Canvas. A drawing area paints the core's
// display list and overlay; a text view over it does inline renaming and relabelling. Pointer and
// key events go to the shared canvas, which reports gestures through its callbacks (wired by the
// window).

#pragma once

#include <gtkmm.h>

#include <optional>
#include <string>

#include "diadraw/canvas/canvas.hpp"

namespace dd::gtk {

class CanvasView : public Gtk::Overlay {
 public:
  CanvasView();

  dd::canvas::Canvas& canvas() { return canvas_; }
  void show_render(const dd::editor::ViewRender& render);
  void clear();
  void set_zoom(double zoom);
  [[nodiscard]] double zoom() const { return canvas_.zoom(); }
  void redraw() { area_.queue_draw(); }

  // What a pointer does, in canvas coordinates; the gestures and test scripts both call these.
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
  void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void update_size();
  void start_editing(const dd::canvas::Edit& edit);
  void finish_editing(bool save);
  static dd::canvas::Modifiers modifiers_of(Gdk::ModifierType state);

  dd::canvas::Canvas canvas_;
  Gtk::DrawingArea area_;
  Gtk::Frame editor_frame_;
  Gtk::TextView editor_;
  std::optional<dd::canvas::Edit> edit_;
  bool composing_ = false;  ///< an input method has a composition open in editor_
  double drag_x_ = 0, drag_y_ = 0;
};

}  // namespace dd::gtk
