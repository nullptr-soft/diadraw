#pragma once

// JSON forms of the model, identical in shape to the TypeScript objects (`JSON.stringify` of a
// `Model`, an `Operation` or a `ResolvedView`). Used by the C ABI, the WASM bindings and the
// parity tests against the TypeScript implementation.

#include "diadraw/base/json.hpp"
#include "diadraw/model/operations.hpp"
#include "diadraw/model/views.hpp"

namespace dd {

Json to_json(const Element& e);
Json to_json(const Relationship& r);
Json to_json(const Group& g);
Json to_json(const LayoutHint& h);
Json to_json(const SequenceItem& item);
Json to_json(const Decoration& d);
Json to_json(const View& v);
Json to_json(const Model& m);
/// A theme declaration: `name`, `extends`, theme keys, and `nodes` as kind → fill, stroke, text.
/// Library manifests use the same shape (keyed by name, without `name`).
Json to_json(const ThemeDef& t);
Result<ThemeDef> theme_from_json(const Json& j);
/// A `style` block: `target` (`elements` or `relationships`), `kind`, `tag` and `style`.
Json to_json(const StyleRule& rule);
Result<StyleRule> style_rule_from_json(const Json& j);
Json to_json(const Operation& op);
Json to_json(const ResolvedView& rv);

Result<Element> element_from_json(const Json& j);
Result<Relationship> relationship_from_json(const Json& j);
Result<Group> group_from_json(const Json& j);
Result<LayoutHint> hint_from_json(const Json& j);
Result<SequenceItem> sequence_item_from_json(const Json& j);
Result<Decoration> decoration_from_json(const Json& j);
Result<View> view_from_json(const Json& j);
Result<Model> model_from_json(const Json& j);
Result<Operation> operation_from_json(const Json& j);

}  // namespace dd
