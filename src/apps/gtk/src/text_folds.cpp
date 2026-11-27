#include "text_folds.hpp"

#include <algorithm>
#include <cmath>

namespace dd::gtk {

namespace {

constexpr int kGutterWidth = 16;

}  // namespace

TextFolds::TextFolds(Gtk::TextView& view) : view_(view), buffer_(view.get_buffer()) {
  hidden_ = buffer_->create_tag();
  hidden_->property_invisible() = true;
  // A folded block's `{` line is shaded, so what is hidden shows where the marker is not seen.
  folded_line_ = buffer_->create_tag();
  folded_line_->property_paragraph_background_rgba() = Gdk::RGBA("rgba(127,127,127,0.14)");

  gutter_.set_content_width(kGutterWidth);
  gutter_.set_cursor(Gdk::Cursor::create("pointer"));
  gutter_.set_tooltip_text("Fold or unfold a block");
  gutter_.set_draw_func(sigc::mem_fun(*this, &TextFolds::draw));
  auto click = Gtk::GestureClick::create();
  click->signal_pressed().connect([this](int, double, double y) { clicked(y); });
  gutter_.add_controller(click);
  view_.set_gutter(Gtk::TextWindowType::LEFT, gutter_);

  connections_.push_back(buffer_->signal_changed().connect([this] {
    update();
    reveal_caret();
  }));
  connections_.push_back(
      buffer_->property_cursor_position().signal_changed().connect(sigc::mem_fun(*this, &TextFolds::reveal_caret)));
  // The markers follow the text as it scrolls; the scroll adjustment arrives with the
  // ScrolledWindow, after this.
  auto follow = [this] {
    scrolled_.disconnect();
    resized_.disconnect();
    if (auto adjustment = view_.get_vadjustment()) {
      scrolled_ = adjustment->signal_value_changed().connect([this] { gutter_.queue_draw(); });
      resized_ = adjustment->signal_changed().connect([this] { gutter_.queue_draw(); });
    }
  };
  connections_.push_back(view_.property_vadjustment().signal_changed().connect(follow));
  follow();
  update();
}

TextFolds::~TextFolds() {
  scrolled_.disconnect();
  resized_.disconnect();
  for (auto& c : connections_) c.disconnect();
  for (auto& b : blocks_)
    if (b.folded) buffer_->delete_mark(b.folded);
  gtk_text_view_set_gutter(view_.gobj(), GTK_TEXT_WINDOW_LEFT, nullptr);
}

void TextFolds::update() {
  std::string text = buffer_->get_text(true);
  auto ranges = dd::dsl::fold_ranges(text);
  // The folded blocks, by where their `{` is now (marks move with the text).
  std::vector<Glib::RefPtr<Gtk::TextMark>> folded;
  for (auto& b : blocks_)
    if (b.folded) folded.push_back(std::move(b.folded));

  std::vector<Block> blocks;
  blocks.reserve(ranges.size());
  for (const auto& r : ranges) {
    // GTK counts lines as the core does, and finds the `{` by its byte index in its line.
    auto open = buffer_->get_iter_at_line_index(static_cast<int>(r.start_line), static_cast<int>(r.open - r.line));
    Block b{r, open.get_offset(), nullptr};
    auto it = std::find_if(folded.begin(), folded.end(), [&](const auto& m) { return m && m->get_iter() == open; });
    if (it != folded.end()) b.folded = std::move(*it);
    blocks.push_back(std::move(b));
  }
  for (auto& m : folded)
    if (m) buffer_->delete_mark(m);  // its block is gone
  blocks_ = std::move(blocks);
  apply();
}

void TextFolds::apply() {
  // Always, even with nothing folded: text pasted from this buffer brings its tags along.
  buffer_->remove_tag(hidden_, buffer_->begin(), buffer_->end());
  buffer_->remove_tag(folded_line_, buffer_->begin(), buffer_->end());
  // Whole lines hide, line breaks included, so the `{` and `}` lines stay as they are.
  for (const auto& b : blocks_)
    if (b.folded) {
      auto start = buffer_->get_iter_at_line(static_cast<int>(b.range.start_line));
      auto first_hidden = buffer_->get_iter_at_line(static_cast<int>(b.range.start_line) + 1);
      buffer_->apply_tag(folded_line_, start, first_hidden);
      buffer_->apply_tag(hidden_, first_hidden, buffer_->get_iter_at_line(static_cast<int>(b.range.end_line) + 1));
    }
  gutter_.queue_draw();
}

bool TextFolds::toggle(int line) {
  auto it = std::find_if(blocks_.begin(), blocks_.end(),
                         [&](const Block& b) { return static_cast<int>(b.range.start_line) == line; });
  if (it == blocks_.end()) return false;
  if (it->folded) {
    buffer_->delete_mark(it->folded);
    it->folded = nullptr;
  } else {
    // A caret in the lines going out of sight moves to the end of the `{` line.
    int caret = buffer_->get_iter_at_mark(buffer_->get_insert()).get_line();
    if (caret > line && caret <= static_cast<int>(it->range.end_line)) {
      auto end = buffer_->get_iter_at_line(line);
      end.forward_to_line_end();
      buffer_->place_cursor(end);
    }
    // Right gravity: text typed just before the `{` keeps the mark on it.
    it->folded = buffer_->create_mark(buffer_->get_iter_at_offset(it->open), false);
  }
  apply();
  return true;
}

void TextFolds::reveal_caret() {
  auto caret = buffer_->get_iter_at_mark(buffer_->get_insert());
  int line = caret.get_line();
  bool changed = false;
  for (auto& b : blocks_)
    if (b.folded && line > static_cast<int>(b.range.start_line) && line <= static_cast<int>(b.range.end_line)) {
      buffer_->delete_mark(b.folded);
      b.folded = nullptr;
      changed = true;
    }
  if (changed) apply();
}

std::vector<TextFolds::Marker> TextFolds::markers() const {
  std::vector<Marker> out;
  for (const auto& b : blocks_) out.push_back({static_cast<int>(b.range.start_line), b.folded != nullptr});
  return out;
}

void TextFolds::draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
  Gdk::RGBA colour = gutter_.get_color();
  for (const auto& b : blocks_) {
    auto it = buffer_->get_iter_at_line(static_cast<int>(b.range.start_line));
    if (it.has_tag(hidden_)) continue;  // inside a folded block
    int y = 0, h = 0, wx = 0, wy = 0;
    view_.get_line_yrange(it, y, h);
    view_.buffer_to_window_coords(Gtk::TextWindowType::LEFT, 0, y, wx, wy);
    if (wy + h < 0 || wy > height) continue;
    double cx = width / 2.0, cy = wy + h / 2.0, r = 3.5;
    cr->set_source_rgba(colour.get_red(), colour.get_green(), colour.get_blue(), b.folded ? 0.9 : 0.5);
    if (b.folded) {  // ▸
      cr->move_to(cx - r * 0.6, cy - r);
      cr->line_to(cx + r * 0.9, cy);
      cr->line_to(cx - r * 0.6, cy + r);
    } else {  // ▾
      cr->move_to(cx - r, cy - r * 0.6);
      cr->line_to(cx + r, cy - r * 0.6);
      cr->line_to(cx, cy + r * 0.9);
    }
    cr->close_path();
    cr->fill();
  }
}

void TextFolds::clicked(double y) {
  int bx = 0, by = 0, top = 0;
  view_.window_to_buffer_coords(Gtk::TextWindowType::LEFT, 0, static_cast<int>(std::floor(y)), bx, by);
  Gtk::TextIter it;
  view_.get_line_at_y(it, by, top);
  toggle(it.get_line());
}

}  // namespace dd::gtk
