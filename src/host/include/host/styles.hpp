#pragma once

// `style` blocks and themes as inspector fields, and the operations that write the fields back.
//
// The element inspector's Shared styles field lists the blocks that apply to the element; the
// document inspector (nothing selected) has the document's and the view's theme, every block,
// and the keys and node colours of the declared theme the view is drawn with. Each field is text,
// as in the element inspector; committing one becomes one operation (one undo step) that patches
// only the blocks, entries and statements that changed.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/model/operations.hpp"

namespace dd::host {

/// Style blocks as text, one `selector: key=value;…` per line in document order:
/// `container: fill=#438dd5;stroke=#3c7fc0`, `tag "external": dashed=1`, `relationship: …`.
std::string style_lines(const std::vector<StyleRule>& rules, const std::vector<std::size_t>& shown);
/// Reads lines written as `style_lines` writes them; empty lines are skipped.
Result<std::vector<StyleRule>> parse_style_lines(std::string_view text);

/// The operation that turns the blocks at `shown` (indices into `rules`, ascending) into
/// `wanted`: blocks are matched by selector, changed ones are updated in place, dropped ones
/// removed and new ones added after the last shown block. Nothing when nothing changes.
std::optional<Operation> style_operation(const std::vector<StyleRule>& rules, const std::vector<std::size_t>& shown,
                                         std::vector<StyleRule> wanted);

/// The indices of the blocks that apply to an element, in document order.
std::vector<std::size_t> styles_for(const Model& model, const Element& element);
/// The element inspector's Shared styles field.
std::string shared_styles(const Model& model, const Element& element);
/// The operation for a typed Shared styles field; an error when a line does not apply to the
/// element or cannot be read.
Result<std::optional<Operation>> shared_styles_operation(const Model& model, const Element& element,
                                                         std::string_view typed);

/// The document inspector's fields, as text.
struct DocumentFields {
  /// The document's `theme`; empty when it has none.
  std::string theme;
  /// The current view's own `theme`; empty when it uses the document's.
  std::string view_theme;
  /// Every `style` block, as `style_lines` writes them.
  std::string styles;
  /// The declared theme the current view is drawn with, whose keys follow; empty when the view
  /// uses a built-in or library theme. Not editable.
  std::string declared;
  /// Its `extends`.
  std::string extends;
  /// Its theme-wide keys, one `key=value` per line.
  std::string theme_keys;
  /// Its node colours, one `kind: fill=…;stroke=…;text=…` per line.
  std::string theme_nodes;
  bool operator==(const DocumentFields&) const = default;
};

/// The theme a view is drawn with: its own, else the document's; nothing for `default`.
std::optional<std::string> view_theme(const Model& model, const std::string& view);
DocumentFields document_fields(const Model& model, const std::string& view);
/// The operation for typed document fields (compared with the model's). A theme name that is
/// neither built in, declared nor a library's declares a new theme extending the one it replaces.
Result<std::optional<Operation>> document_operation(const Model& model, const std::string& view,
                                                    const DocumentFields& typed);

}  // namespace dd::host
