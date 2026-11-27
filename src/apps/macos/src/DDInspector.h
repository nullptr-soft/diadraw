// The selected element's or relationship's properties as fields, beside the canvas: pickers for
// the kind, shape and technology (a list to pick from, or type a new one), a token field for the
// tags, the visual style editor (DDStyleEditor) for the style, a key/value table for the
// properties, a growing text view for the description and text fields for the rest.
// Committing a field (Return, leaving it after a change, picking an entry or changing a style
// control) hands every field to `onCommit` (elements) or `onCommitRelationship`, which turn them
// into a text change through dd::host::Document::update.
//
// While a field is typed in, `checkElement` (or `checkRelationship`) says which fields the edit
// would refuse; those get a red outline and the reason as their tooltip.

#pragma once

#import <AppKit/AppKit.h>

#include <optional>
#include <vector>

#include "host/document.hpp"

@interface DDInspector : NSView
/// Called with the fields as typed when one of them is committed.
@property(nonatomic, copy) void (^onCommit)(const dd::host::ElementFields& fields);
@property(nonatomic, copy) void (^onCommitRelationship)(const dd::host::RelationshipFields& fields);
/// The fields the edit as typed would refuse, and why (dd::host::Document::problems).
@property(nonatomic, copy) std::vector<dd::editor::FieldProblem> (^checkElement)(const dd::host::ElementFields& fields);
@property(nonatomic, copy) std::vector<dd::editor::FieldProblem> (^checkRelationship)(
    const dd::host::RelationshipFields& fields);
/// Shows an element's fields; nothing hides the inspector. A field being typed in keeps its text
/// when the same element is shown again.
- (void)showFields:(const std::optional<dd::host::ElementFields>&)fields;
/// What the pickers offer (dd::host::Document::choices); set before showing fields.
- (void)setChoices:(const dd::host::FieldChoices&)choices;
/// The same for a relationship. Either call replaces what the other showed.
- (void)showRelationshipFields:(const std::optional<dd::host::RelationshipFields>&)fields;
/// For scripted tests: types `value` into the field `key` of the panel shown (an element's id,
/// kind, name, technology, shape, tags, description, style, props; a relationship's label, technology,
/// arrow, name, tags, style, props) and commits it; `style.<key>` sets one key through the style
/// editor's control (`style.fill`, `style.lineStyle`, `style.other`). False when the inspector is
/// hidden or the key unknown.
- (BOOL)setField:(NSString*)key to:(NSString*)value;
/// For scripted tests (`enter`): types `value` into the text field `key` as setField:to: does,
/// without committing it. False when the inspector is hidden, the key unknown or not a text field.
- (BOOL)enterField:(NSString*)key to:(NSString*)value;
/// The fields outlined as invalid, with why, in the order shown.
- (std::vector<dd::editor::FieldProblem>)invalidFields;
@end
