// Code folding for the text pane: a gutter left of the text box with a marker on every block that
// spans lines (`model`, `view`, elements with children, groups, …, from dd::dsl::fold_ranges).
// Clicking a marker hides the lines between the block's braces (RichEdit's hidden text, so the
// text itself never changes), or shows them again. A folded block stays folded while the text
// around it changes (its `{` is a RichEdit range, which moves with edits), and unfolds when its
// braces go or the caret moves into it.

#pragma once

#include "winrt.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dd::winui {

class TextFolds {
 public:
  /// `changed` runs when a block folds or unfolds: the window restyles, which calls `hide`.
  TextFolds(winrt::Microsoft::UI::Xaml::Controls::RichEditBox text, std::function<void()> changed);
  // XAML handlers hold `this`.
  TextFolds(const TextFolds&) = delete;
  TextFolds& operator=(const TextFolds&) = delete;

  /// The gutter, to place left of the text box.
  [[nodiscard]] winrt::Microsoft::UI::Xaml::FrameworkElement gutter() const { return gutter_; }

  /// Recomputes the blocks for `text` (what the box holds, lines ending in \n), keeping the folded
  /// blocks that remain; `utf16` maps its byte offsets to the box's. Then `hide` and `redraw`.
  void update(const std::string& text, const std::vector<int>& utf16);
  /// Hides the folded blocks' lines; call while restyling, after the formats were reset.
  void hide(winrt::Microsoft::UI::Text::RichEditTextDocument const& document);
  /// Unfolds the blocks holding the caret; true if any did. `typing`: the text may have changed
  /// since the last `update`.
  bool reveal_caret(bool typing = false);
  /// Folds or unfolds the block starting on `line` (0-based); false when no block starts there.
  bool toggle(int line);
  /// Every line with a marker and whether its block is folded, for the gesture tests.
  struct Marker {
    int line = 0;
    bool folded = false;
  };
  [[nodiscard]] std::vector<Marker> markers() const;
  /// Places the markers beside their lines.
  void redraw();

 private:
  struct Block {
    int line = 0;  ///< of the `{`, 0-based
    int open = 0;  ///< the `{`, UTF-16
    int from = 0;  ///< the hidden lines, line breaks included: the `{` and `}` lines stay
    int to = 0;
    /// The `{` while folded: a range RichEdit keeps in step with edits.
    winrt::Microsoft::UI::Text::ITextRange folded{nullptr};
  };

  [[nodiscard]] bool hidden(int offset) const;
  static winrt::Microsoft::UI::Text::ITextRange anchor(winrt::Microsoft::UI::Text::RichEditTextDocument const& document,
                                                       int open);

  winrt::Microsoft::UI::Xaml::Controls::RichEditBox text_;
  winrt::Microsoft::UI::Xaml::Controls::Canvas gutter_;
  std::function<void()> changed_;
  std::vector<Block> blocks_;
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller_{nullptr};
};

}  // namespace dd::winui
