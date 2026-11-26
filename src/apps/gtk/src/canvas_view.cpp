#include "canvas_view.hpp"

#include <algorithm>

#include "painter.hpp"

namespace dd::gtk {

CanvasView::CanvasView() {
  // The double-click below comes after the drag gesture's own press for the second click.
  canvas_.set_double_click_repeats_press(true);
  set_child(area_);
  area_.set_focusable(true);
  area_.set_draw_func(sigc::mem_fun(*this, &CanvasView::draw));
  area_.set_halign(Gtk::Align::START);
  area_.set_valign(Gtk::Align::START);

  // Press, move and release; a drag starts at once and the canvas applies its own threshold.
  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this, drag](double x, double y) {
    if (edit_) return;
    area_.grab_focus();
    drag_x_ = x;
    drag_y_ = y;
    press(x, y, 1, modifiers_of(drag->get_current_event_state()));
  });
  drag->signal_drag_update().connect([this, drag](double dx, double dy) {
    if (!edit_) drag_to(drag_x_ + dx, drag_y_ + dy, modifiers_of(drag->get_current_event_state()));
  });
  drag->signal_drag_end().connect([this, drag](double dx, double dy) {
    if (!edit_) release(drag_x_ + dx, drag_y_ + dy, modifiers_of(drag->get_current_event_state()));
  });
  area_.add_controller(drag);

  // Double-click: rename or relabel in place.
  auto click = Gtk::GestureClick::create();
  click->set_button(GDK_BUTTON_PRIMARY);
  click->signal_pressed().connect([this](int n, double x, double y) {
    if (n == 2 && !edit_) press(x, y, 2, {});
  });
  area_.add_controller(click);

  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint key, guint, Gdk::ModifierType state) {
        if ((key == GDK_KEY_a || key == GDK_KEY_A) &&
            (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{}) {
          if (canvas_.select_all()) redraw();
          return true;
        }
        if (key == GDK_KEY_Delete || key == GDK_KEY_BackSpace || key == GDK_KEY_KP_Delete) {
          delete_selection();
          return true;
        }
        if (key == GDK_KEY_Escape) {
          canvas_.escape();
          redraw();
          return true;
        }
        return false;
      },
      false);
  area_.add_controller(keys);

  // Ctrl+scroll and pinch zoom.
  auto scroll = Gtk::EventControllerScroll::create();
  scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
  scroll->signal_scroll().connect(
      [this, scroll](double, double dy) {
        if ((scroll->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType{})
          return false;
        set_zoom(zoom() * (dy < 0 ? 1.1 : 1 / 1.1));
        return true;
      },
      false);
  area_.add_controller(scroll);
  auto pinch = Gtk::GestureZoom::create();
  auto start_zoom = std::make_shared<double>(1);
  pinch->signal_begin().connect([this, start_zoom](Gdk::EventSequence*) { *start_zoom = zoom(); });
  pinch->signal_scale_changed().connect([this, start_zoom](double scale) { set_zoom(*start_zoom * scale); });
  area_.add_controller(pinch);

  // The inline editor: Enter commits (a new line in a name, where Ctrl+Enter commits), Escape
  // cancels, leaving it commits.
  editor_.set_accepts_tab(false);
  editor_.set_left_margin(4);
  editor_.set_right_margin(4);
  editor_.set_top_margin(3);
  editor_.set_bottom_margin(3);
  editor_frame_.set_child(editor_);
  editor_frame_.set_visible(false);
  editor_frame_.set_halign(Gtk::Align::START);
  editor_frame_.set_valign(Gtk::Align::START);
  add_overlay(editor_frame_);
  // While an input method composes, Enter and Escape are its own (they end or drop the
  // composition); the keys below are read before the text view's input method sees them.
  g_signal_connect(editor_.gobj(), "preedit-changed",
                   G_CALLBACK(+[](GtkTextView*, const char* text, gpointer self) {
                     static_cast<CanvasView*>(self)->composing_ = text && *text;
                   }),
                   this);
  auto editor_keys = Gtk::EventControllerKey::create();
  editor_keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  editor_keys->signal_key_pressed().connect(
      [this](guint key, guint, Gdk::ModifierType state) {
        if (composing_) return false;
        if (key == GDK_KEY_Escape) {
          finish_editing(false);
          return true;
        }
        if (key != GDK_KEY_Return && key != GDK_KEY_KP_Enter && key != GDK_KEY_ISO_Enter) return false;
        const bool ctrl = (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        if (edit_ && edit_->multiline() && !ctrl) return false;  // the text view inserts the line break
        finish_editing(true);
        return true;
      },
      false);
  editor_.add_controller(editor_keys);
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect([this] {
    if (!scripted) finish_editing(true);
  });
  editor_.add_controller(focus);
}

dd::canvas::Modifiers CanvasView::modifiers_of(Gdk::ModifierType state) {
  auto has = [&](Gdk::ModifierType m) { return (state & m) != Gdk::ModifierType{}; };
  return {has(Gdk::ModifierType::SHIFT_MASK), has(Gdk::ModifierType::ALT_MASK), has(Gdk::ModifierType::CONTROL_MASK)};
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

void CanvasView::update_size() {
  area_.set_content_width(std::max(1, static_cast<int>(canvas_.width() + 0.5)));
  area_.set_content_height(std::max(1, static_cast<int>(canvas_.height() + 0.5)));
}

void CanvasView::draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
  cairo_t* c = cr->cobj();
  cairo_set_source_rgb(c, 0.973, 0.98, 0.988);
  cairo_paint(c);
  if (!canvas_.list()) return;
  cairo_save(c);
  cairo_scale(c, canvas_.zoom(), canvas_.zoom());
  paint(c, canvas_.backdrop(width, height));
  paint(c, *canvas_.list());
  paint(c, canvas_.overlay());
  cairo_restore(c);
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
  composing_ = false;
  // A name wraps to the field's width; a label stays on its one line.
  editor_.set_wrap_mode(edit.multiline() ? Gtk::WrapMode::WORD_CHAR : Gtk::WrapMode::NONE);
  auto buffer = editor_.get_buffer();
  buffer->set_text(edit.value);
  // A one-line field sits on the middle of what it edits; a name's grows downward from there.
  editor_frame_.set_margin_start(std::max(0, static_cast<int>(edit.x)));
  editor_frame_.set_margin_top(std::max(0, static_cast<int>(edit.y + edit.height / 2 - 14)));
  editor_frame_.set_size_request(std::max(120, static_cast<int>(edit.width)), -1);
  editor_frame_.set_visible(true);
  editor_.grab_focus();
  buffer->select_range(buffer->begin(), buffer->end());
}

void CanvasView::finish_editing(bool save) {
  if (!edit_) return;
  auto edit = std::move(*edit_);
  edit_.reset();
  std::string value = editor_.get_buffer()->get_text();
  editor_frame_.set_visible(false);
  area_.grab_focus();
  if (save) canvas_.commit_edit(edit, value);
}

bool CanvasView::commit_editing(const std::string& text) {
  if (!edit_) return false;
  editor_.get_buffer()->set_text(text);
  finish_editing(true);
  return true;
}

}  // namespace dd::gtk
