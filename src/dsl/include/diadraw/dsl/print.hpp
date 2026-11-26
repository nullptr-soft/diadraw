#pragma once

// Canonical DiaDraw text from a model, a port of packages/dsl/src/print.ts.

#include <string>
#include <string_view>
#include <vector>

#include "diadraw/model/model.hpp"

namespace dd::dsl {

/// `[A-Za-z_][A-Za-z0-9_]*`: an id that can be written as a name (a relationship's `name = …`).
bool is_identifier(std::string_view s);
/// The identifier an element is declared with inside its parent's body.
std::string local_name(const Element& element);
std::string print_use(const LibraryUse& use);
std::string print_endpoint(const Endpoint& e);
/// An element's declaration without its body. `indent` is the indentation of its line, for a
/// description that spans lines.
std::string print_element_head(const Element& element, std::string_view indent = {});
std::string print_relationship(const Relationship& r);
std::string print_hint(const LayoutHint& hint);
void print_sequence(const std::vector<SequenceItem>& items, int depth, std::vector<std::string>& out);
std::string print_layout(const View& view);
std::string print_view(const View& view);
/// What a `style` block applies to, as written after `style`: `*`, `container`,
/// `tag "external"`, `container tag "legacy"`, `relationship`, `relationship tag "async"`.
std::string print_style_selector(const StyleRule& rule);
/// A `style` block.
std::string print_style_rule(const StyleRule& rule);
/// A `theme name extends base { … }` declaration.
std::string print_theme_def(const ThemeDef& theme);

/// Prints a model in canonical form: elements nested under their parents, groups as blocks,
/// relationships after the elements with absolute references, then one block per view.
std::string print(const Model& model);

}  // namespace dd::dsl
