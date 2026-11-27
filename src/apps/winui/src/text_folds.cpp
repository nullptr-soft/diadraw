#include "text_folds.hpp"

#include <algorithm>

#include "diadraw/dsl/highlight.hpp"

namespace dd::winui {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
namespace text = winrt::Microsoft::UI::Text;

namespace {

constexpr double kGutterWidth = 16;

/// The text box's own scroller, from its template.
ScrollViewer find_scroller(DependencyObject const& root) {
  int n = Media::VisualTreeHelper::GetChildrenCount(root);
  for (int i = 0; i < n; ++i) {
    auto child = Media::VisualTreeHelper::GetChild(root, i);
    if (auto s = child.try_as<ScrollViewer>()) return s;
    if (auto s = find_scroller(child)) return s;
  }
  return nullptr;
}

}  // namespace

TextFolds::TextFolds(RichEditBox text, std::function<void()> changed)
    : text_(std::move(text)), changed_(std::move(changed)) {
  gutter_.Width(kGutterWidth);
  gutter_.Background(Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
  // The markers follow the text as it scrolls and resizes.
  text_.Loaded([this](auto const&, auto const&) {
    if (!scroller_ && (scroller_ = find_scroller(text_)))
      scroller_.ViewChanged([this](auto const&, auto const&) { redraw(); });
    redraw();
  });
  text_.SizeChanged([this](auto const&, auto const&) { redraw(); });
}

void TextFolds::update(const std::string& t, const std::vector<int>& utf16) {
  // The folded blocks, by where their `{` is now.
  std::vector<int> folded;
  for (const auto& b : blocks_)
    if (b.folded) folded.push_back(b.folded.StartPosition());

  auto document = text_.Document();
  std::vector<Block> blocks;
  for (const auto& r : dd::dsl::fold_ranges(t)) {
    Block b{static_cast<int>(r.start_line), utf16[r.open], utf16[r.first], utf16[r.last]};
    if (std::find(folded.begin(), folded.end(), b.open) != folded.end()) b.folded = anchor(document, b.open);
    blocks.push_back(std::move(b));
  }
  blocks_ = std::move(blocks);
}

text::ITextRange TextFolds::anchor(text::RichEditTextDocument const& document, int open) {
  // The `{` itself, not an empty range at it: text typed just before the `{` then moves it.
  return document.GetRange(open, open + 1);
}

void TextFolds::hide(text::RichEditTextDocument const& document) {
  for (const auto& b : blocks_)
    if (b.folded) document.GetRange(b.from, b.to).CharacterFormat().Hidden(text::FormatEffect::On);
}

bool TextFolds::hidden(int offset) const {
  return std::any_of(blocks_.begin(), blocks_.end(),
                     [&](const Block& b) { return b.folded && offset >= b.from && offset < b.to; });
}

bool TextFolds::reveal_caret(bool typing) {
  auto selection = text_.Document().Selection();
  int caret = selection.StartPosition();
  bool changed = false;
  for (auto& b : blocks_) {
    if (!b.folded) continue;
    // While typing, the blocks are as of the last sync: shift them as far as their `{` moved, and
    // leave the `{` line alone, where text typed at its end lands until the next sync.
    bool in = false;
    if (!typing) {
      in = caret >= b.from && caret < b.to;
    } else if (selection.GetIndex(text::TextRangeUnit::Paragraph) != b.folded.GetIndex(text::TextRangeUnit::Paragraph)) {
      int shift = b.folded.StartPosition() - b.open;
      in = caret >= b.from + shift && caret < b.to + shift;
    }
    if (in) {
      b.folded = nullptr;
      changed = true;
    }
  }
  if (changed) changed_();
  return changed;
}

bool TextFolds::toggle(int line) {
  auto it = std::find_if(blocks_.begin(), blocks_.end(), [&](const Block& b) { return b.line == line; });
  if (it == blocks_.end()) return false;
  if (it->folded) {
    it->folded = nullptr;
  } else {
    it->folded = anchor(text_.Document(), it->open);
    // A caret in the lines going out of sight moves to the end of the `{` line.
    auto selection = text_.Document().Selection();
    if (selection.StartPosition() >= it->from && selection.StartPosition() < it->to)
      selection.SetRange(it->from - 1, it->from - 1);
  }
  changed_();
  return true;
}

std::vector<TextFolds::Marker> TextFolds::markers() const {
  std::vector<Marker> out;
  for (const auto& b : blocks_) out.push_back({b.line, b.folded != nullptr});
  return out;
}

void TextFolds::redraw() {
  gutter_.Children().Clear();
  auto document = text_.Document();
  double height = text_.ActualHeight();
  for (std::size_t i = 0; i < blocks_.size(); ++i) {
    const auto& b = blocks_[i];
    if (hidden(b.open)) continue;  // inside a folded block
    winrt::Windows::Foundation::Rect rect{};
    int hit = 0;
    document.GetRange(b.open, b.open).GetRect(text::PointOptions::ClientCoordinates, rect, hit);
    if (rect.Y + rect.Height < 0 || rect.Y > height) continue;
    TextBlock marker;
    marker.Text(b.folded ? L"▸" : L"▾");  // ▸ folded, ▾ open
    marker.FontSize(11);
    marker.Width(kGutterWidth);
    marker.TextAlignment(TextAlignment::Center);
    marker.Opacity(b.folded ? 0.9 : 0.5);
    marker.Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{255, 96, 96, 100}));
    Canvas::SetTop(marker, rect.Y + (rect.Height - 15) / 2);
    int line = b.line;
    marker.Tapped([this, line](auto const&, TappedRoutedEventArgs const& e) {
      toggle(line);
      e.Handled(true);
    });
    gutter_.Children().Append(marker);
  }
}

}  // namespace dd::winui
