#pragma once

// What the inspector's controls need beyond the fields themselves (see host/document.hpp): the
// choices its pickers offer (kinds, shapes, tags, technologies) and the keys of its visual style
// editor. The fields stay text, so the shells only change how they are edited: a picker or the
// style editor writes the same string a person could have typed.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "diadraw/library/library.hpp"
#include "diadraw/model/model.hpp"
#include "diadraw/render/display_list.hpp"

namespace dd::host {

// ── Choices ──────────────────────────────────────────────────────────────────────────────────

/// One entry of a picker.
struct Choice {
  /// What the field is set to: `container`, `cylinder`, `aws.lambda`.
  std::string value;
  /// What the picker shows; the value itself for built-in kinds and shapes.
  std::string label;
  /// What a picker groups the entry under: a library's alias for its kinds and shapes, a section
  /// such as `Crow's foot` for a style choice; none for built-in kinds and shapes.
  std::string group;
  bool operator==(const Choice&) const = default;
};

struct FieldChoices {
  /// The built-in kinds, then each library's kinds (`alias.kind`).
  std::vector<Choice> kinds;
  /// The base shapes, then each library's shapes (`alias.shape`).
  std::vector<Choice> shapes;
  /// Every tag used on an element or a relationship, sorted, without repeats.
  std::vector<std::string> tags;
  /// Every `tech` used on an element or a relationship, sorted, without repeats.
  std::vector<std::string> technologies;
  /// The themes a document can pick: the built-in ones, the libraries' (`alias.name`), then the
  /// ones the document declares (group `Declared`).
  std::vector<Choice> themes;
};

/// The built-in element kinds, in the order a picker lists them.
const std::vector<std::string>& builtin_kinds();
/// The base shapes `shape` takes (docs/dsl.md).
const std::vector<std::string>& base_shapes();
/// The built-in kinds and shapes, and the libraries' (no tags or technologies).
FieldChoices library_choices(const library::LibrarySet& libraries);
/// Adds what a model uses: its tags, technologies and kinds no entry has yet.
void add_model_choices(FieldChoices& choices, const Model& model);
/// The choices for a document's inspector: both of the above.
FieldChoices field_choices(const Model& model, const library::LibrarySet& libraries);

/// A Tags field (`external, legacy`) as a list, trimmed, without empty entries.
std::vector<std::string> split_tags(std::string_view text);
/// The other way: `external, legacy`.
std::string join_tags(const std::vector<std::string>& tags);

// ── Properties ───────────────────────────────────────────────────────────────────────────────

/// A Properties field (one `key=value` per line) as rows for a key/value table, as
/// Document::update reads it: trimmed, a line without `=` a key with an empty value, lines
/// without a key skipped, a repeated key's later value winning in the first one's place.
std::vector<std::pair<std::string, std::string>> property_rows(std::string_view text);
/// The other way. Rows without a key are left out; an `=` in a key and a line break anywhere
/// cannot be written, so they become `_` and a space.
std::string property_text(const std::vector<std::pair<std::string, std::string>>& rows);

// ── Style blocks and themes ──────────────────────────────────────────────────────────────────

/// One line of the Style blocks field (`container: fill=#438dd5`) or of a theme's Node colours
/// (`person: fill=#08427b;text=#ffffff`): what comes before the first colon outside quotes, and
/// the `key=value;…` after it, both trimmed. Empty lines are skipped; a line without a colon is
/// all selector.
struct BlockRow {
  std::string selector;
  std::string style;
  bool operator==(const BlockRow&) const = default;
};
std::vector<BlockRow> block_rows(std::string_view text);
/// The other way, one `selector: style` per line; rows without a selector are left out.
std::string block_text(const std::vector<BlockRow>& rows);
/// Whether a style block's selector picks relationships (`relationship`, `relationship tag "…"`).
StyleTarget selector_target(std::string_view selector);
/// The selectors a block picker offers: `*`, `relationship`, every kind, and `tag "…"` and
/// `relationship tag "…"` for every tag the document uses.
std::vector<std::string> selector_choices(const FieldChoices& choices);

/// Whether a theme-wide key (`background`, `fontFamily`, …) holds a colour.
bool is_color_key(std::string_view key);

// ── Style editor ─────────────────────────────────────────────────────────────────────────────

enum class StyleControl {
  Color,      ///< `#rrggbb` (a colour well)
  Number,     ///< a number between `min` and `max` (a stepper or a slider)
  Flag,       ///< `1` or `0` (a check box)
  Choice,     ///< one of `choices` (a pop-up)
  FontStyle,  ///< draw.io's bit mask: bold 1, italic 2, underline 4 (three toggles;
              ///< strike-through 8 is kept but has no toggle)
};

/// One key the style editor has a control for.
struct StyleKey {
  std::string key;  ///< `fill`, `strokeWidth`, `endArrow`
  std::string label;
  StyleControl control = StyleControl::Color;
  /// For `Choice`: the values, in order, with what the pop-up shows. The first is the default,
  /// which the key's absence means.
  std::vector<Choice> choices;
  /// For `Number`: the range and the step of a stepper or slider.
  double min = 0;
  double max = 0;
  double step = 1;
  /// What the key's absence means, for `Flag` (`0` or `1`) and as a hint for `Number`. A control
  /// set back to it removes the key, so the text stays as short as before.
  std::string fallback;
  /// The section the editor shows it under: `Fill and line`, `Text`, `Arrows`.
  std::string section;
};

/// The key of the Line style pop-up, which stands for two style keys: `dashed` and
/// `dashPattern`. Its values are an empty value (solid), `dashed` (the plain dashes) and the dash patterns.
inline constexpr std::string_view kLineStyleKey = "lineStyle";

/// The keys the editor shows for elements (and decorations) or for relationships.
const std::vector<StyleKey>& style_keys(StyleTarget target);

/// A `key=value;…` style as ordered pairs; a part without `=` has an empty value.
std::vector<std::pair<std::string, std::string>> style_entries(std::string_view style);
std::string style_text(const std::vector<std::pair<std::string, std::string>>& entries);
/// The value of `key` in `style`, or nothing.
std::optional<std::string> style_value(std::string_view style, std::string_view key);
/// `style` with `key` set to `value` (in place when present, appended otherwise), or removed when
/// `value` is nothing. Every other entry stays as written.
std::string with_style_value(std::string_view style, std::string_view key, const std::optional<std::string>& value);
/// What the control for `key` shows for `style`: the key's value, or for the Line style pop-up the
/// value its `dashed` and `dashPattern` add up to; nothing when the key is absent (solid).
std::optional<std::string> style_control_value(std::string_view style, const StyleKey& key);
/// Whether the control for `key` edits the style key `style_key`, so Other leaves it out.
bool style_key_covers(const StyleKey& key, std::string_view style_key);
/// What a control for `key` sets the key to: nothing when `value` is the key's fallback (or, for
/// a choice, its first value), so returning a control to its default removes the key.
std::optional<std::string> style_setting(const StyleKey& key, std::string_view value);
/// `style` with `key` set as its control sets it to `value` (see style_setting). The Line style
/// pop-up sets `dashed` and `dashPattern` together: solid removes both, a pattern sets both.
std::string with_style_setting(std::string_view style, const StyleKey& key, std::string_view value);

/// The line a pop-up entry previews (draw it with LinePreview): one with that end, or in
/// that line style; nothing for keys whose entries have no preview.
std::optional<Style> style_sample(const StyleKey& key, std::string_view value);

/// A pop-up entry's preview (see style_sample), drawn in the shell's colours. It is rendered when
/// first asked for and again only when the colours or the size change, not on every redraw. Not
/// safe to use from two threads at once.
class LinePreview {
 public:
  explicit LinePreview(Style sample) : sample_(std::move(sample)) {}
  /// The line in `ink` (1.25 wide) on `surface`, which also fills hollow ends, in a box of
  /// `width` × `height`; nothing when it cannot be drawn.
  [[nodiscard]] const std::optional<draw::DisplayList>& list(std::string_view ink, std::string_view surface, double width,
                                                             double height);

 private:
  Style sample_;
  bool drawn_ = false;
  std::string ink_, surface_;
  double width_ = 0, height_ = 0;
  std::optional<draw::DisplayList> list_;
};

/// The entries of `style` the editor has no control for, as `key=value;…`, for its Other field.
std::string other_style(std::string_view style, StyleTarget target);
/// `style` with those entries replaced by `other` (typed in the Other field); the entries with
/// a control stay. An entry typed in Other for a key with a control sets that key.
std::string with_other_style(std::string_view style, StyleTarget target, std::string_view other);

/// A colour value as `#rrggbb` (lower case) when it is one: `#abc` and `#AABBCC` are read,
/// draw.io's `none` and anything else is not.
std::optional<std::string> normalized_color(std::string_view value);

/// The bits of a `fontStyle` value (bold 1, italic 2, underline 4); 0 when it is not a number.
int font_style_bits(std::string_view value);

}  // namespace dd::host
