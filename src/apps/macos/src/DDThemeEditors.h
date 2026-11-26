// The Theme and styles panel's editors (see DDStylePanel). Each shows one of the panel's text
// fields (dd::host::DocumentFields) as controls and writes the same text back:
//
// - DDColorField: a colour button with a button that clears it; its value is `#rrggbb` or empty.
//   The colour button opens the preset swatches (host/swatches.hpp), Custom… and Default.
// - DDStyleBlocks: the Style blocks field (`selector: k=v;…` per line) as a list of blocks, each
//   a selector picker and, when opened, the visual style editor (DDStyleEditor).
// - DDThemeKeys: a declared theme's keys (`key=value` per line), a colour button per colour key and a
//   text field for the font keys. Keys without a control are kept.
// - DDThemeNodes: its node colours (`kind: fill=…;stroke=…;text=…` per line), a row per kind with
//   three colour buttons.
//
// The list editors have the same small interface: `text`, `showText:keepTyping:`, `editing` and
// `onChange`.

#pragma once

#import <AppKit/AppKit.h>

@interface DDColorField : NSView
- (instancetype)initCompact:(BOOL)compact;
/// `#rrggbb`, another value the button cannot show (kept as text), or empty for the default.
@property(nonatomic, copy) NSString* value;
@property(nonatomic, copy) void (^onChange)(void);
- (void)endColorEditing;
@end

@protocol DDTextEditor <NSObject>
@property(nonatomic, copy) void (^onChange)(void);
@property(nonatomic, readonly) NSString* text;
@property(nonatomic, readonly) BOOL editing;
- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping;
@end

@interface DDStyleBlocks : NSView <DDTextEditor>
/// What the selector pickers list (dd::host::selector_choices).
- (void)setSelectors:(NSArray<NSString*>*)selectors;
@end

@interface DDThemeKeys : NSView <DDTextEditor>
@end

@interface DDThemeNodes : NSView <DDTextEditor>
/// What the Add picker lists: the document's kinds.
- (void)setKinds:(NSArray<NSString*>*)kinds;
@end
