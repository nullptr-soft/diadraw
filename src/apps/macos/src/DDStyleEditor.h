// A visual editor for an inline `style "k=v;…"`: a control per key the renderer knows (colour
// wells, steppers, check boxes, pop-ups, bold/italic/underline), from dd::host::style_keys, and an
// Other field for the keys it has no control for. It edits the same text the Style field held:
// every control writes one key and leaves the rest of the text as it was.

#pragma once

#import <AppKit/AppKit.h>

#include "diadraw/model/model.hpp"

@interface DDStyleEditor : NSView
- (instancetype)initWithTarget:(dd::StyleTarget)target;
/// Called after a control changed the style.
@property(nonatomic, copy) void (^onChange)(void);
/// The style as edited.
@property(nonatomic, readonly) NSString* style;
/// True while one of its text fields is being typed in.
@property(nonatomic, readonly) BOOL editing;
/// Shows a style; a field being typed in keeps its text when `keepTyping` is true.
- (void)showStyle:(NSString*)style keepTyping:(BOOL)keepTyping;
/// Closes the colour panel's link to a well, so it does not recolour the next item shown.
- (void)endColorEditing;
/// For scripted tests: sets one key as its control would (`fill`, `lineStyle`, `fontStyle`, …;
/// `other` for the Other field); an empty value puts the control back to its default. False for
/// a key without a control.
- (BOOL)setKey:(NSString*)key to:(NSString*)value;
@end
