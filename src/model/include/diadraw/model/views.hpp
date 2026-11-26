#pragma once

// View resolution, a port of packages/core/src/views.ts.

#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/model/model.hpp"

namespace dd {

/// The elements and relationships a view shows, ready for layout.
struct ResolvedView {
  View view;
  std::vector<Element> elements;
  std::vector<Relationship> relationships;
  /// Groups that contain at least one visible element, outermost first.
  std::vector<Group> groups;
  /// The theme to draw with: the view's own, else the document's; the default theme when absent.
  std::optional<std::string> theme;
  /// The document's `style` blocks.
  std::vector<StyleRule> styles;
  /// Themes the document declares.
  std::vector<ThemeDef> themes;
};

/// Resolves a view's include/exclude selectors against the model.
///
/// `*` means the direct children of the view's scope, or every element when the view has no
/// scope. Relationships are shown when both ends are visible; a relationship between hidden
/// descendants is shown between their nearest visible ancestors (implied, as in C4).
Result<ResolvedView> resolve_view(const Model& model, std::string_view view_id);

inline constexpr std::string_view kDefaultViewId = "default";

/// The model itself when it has views; otherwise the model plus one implicit view of everything.
Model with_default_view(const Model& model);
/// The same for a model the caller gives up: no copy when it already has a view.
Model with_default_view(Model&& model);

}  // namespace dd
