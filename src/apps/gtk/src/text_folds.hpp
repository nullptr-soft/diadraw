#pragma once

// Code folding for the text pane: a gutter left of the text with a marker on every block that
// spans lines (`model`, `view`, elements with children, groups, …, from dd::dsl::fold_ranges).
// Clicking a marker hides the lines between the block's braces, or shows them again. A folded
// block stays folded while the text around it changes, and unfolds when its braces go or the
// caret moves into it (a selection from the canvas, say).

#include <gtkmm.h>

#include <string>
#include <vector>

#include "diadraw/dsl/highlight.hpp"

namespace dd::gtk {

class TextFolds {
 public:
  /// Attaches to `view`, which already has its buffer, and puts the gutter on its left.
  explicit TextFolds(Gtk::TextView& view);
  TextFolds(const TextFolds&) = delete;
  TextFolds& operator=(const TextFolds&) = delete;
  ~TextFolds();

  /// Folds or unfolds the block starting on `line` (0-based); false when no block starts there.
  bool toggle(int line);
  /// Every line with a marker and whether its block is folded, for the gesture tests.
  struct Marker {
    int line = 0;
    bool folded = false;
  };
  std::vector<Marker> markers() const;

 private:
  struct Block {
    dd::dsl::FoldRange range;
    int open = 0;  ///< the `{` as a character offset into the buffer
    Glib::RefPtr<Gtk::TextMark> folded;  ///< at the `{` while folded, else null
  };

  /// Recomputes the blocks after the text changed, keeping the folded ones that remain.
  void update();
  /// Hides the folded blocks' lines and redraws the gutter.
  void apply();
  /// Unfolds every block whose hidden lines hold the caret.
  void reveal_caret();
  void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void clicked(double y);

  Gtk::TextView& view_;
  Glib::RefPtr<Gtk::TextBuffer> buffer_;
  Glib::RefPtr<Gtk::TextTag> hidden_;
  Glib::RefPtr<Gtk::TextTag> folded_line_;
  Gtk::DrawingArea gutter_;
  std::vector<Block> blocks_;
  std::vector<sigc::connection> connections_;
  sigc::connection scrolled_;
  sigc::connection resized_;
};

}  // namespace dd::gtk
