// The document's theme and `style` blocks as fields (see dd::host::DocumentFields), in place of
// the inspector while nothing is selected and the Styles button is on. The themes are combo boxes
// of the themes there are; the style blocks, a declared theme's keys and its node colours are
// edited visually (DDThemeEditors.h). Every change hands all the fields to `onCommit`.

#pragma once

#import <AppKit/AppKit.h>

#include <optional>

#include "host/field_editors.hpp"
#include "host/styles.hpp"

@interface DDStylePanel : NSView
/// Called with the fields as edited when one of them changes.
@property(nonatomic, copy) void (^onCommit)(const dd::host::DocumentFields& fields);
/// What the theme pickers, the style blocks' selector pickers and the node colours' kinds offer.
- (void)setChoices:(const dd::host::FieldChoices&)choices;
/// Shows the fields; nothing hides the panel. A field being typed in keeps its text.
- (void)showFields:(const std::optional<dd::host::DocumentFields>&)fields;
/// For scripted tests: sets the field `key` (theme, view-theme, styles, extends, theme-keys,
/// theme-nodes) to the whole text `value` and commits it. False when the field is hidden or the
/// key unknown.
- (BOOL)setField:(NSString*)key to:(NSString*)value;
@end
