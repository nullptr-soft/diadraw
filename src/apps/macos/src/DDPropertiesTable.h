// Small editors the inspector uses in place of multi-line text fields:
//
// DDPropertiesTable shows a Properties field (one `key=value` per line) as a two-column table
// with + and − buttons; editing a cell, adding or removing a row writes the same text back.
// DDTextArea is a text view that grows with its text, where Return starts a new line; leaving it
// after a change commits it and Escape puts it back.

#pragma once

#import <AppKit/AppKit.h>

@interface DDPropertiesTable : NSView
/// Called after a cell, an added or a removed row changed the text.
@property(nonatomic, copy) void (^onChange)(void);
/// The rows as text, one `key=value` per line.
@property(nonatomic, readonly) NSString* text;
/// True while a cell is being typed in.
@property(nonatomic, readonly) BOOL editing;
/// Shows a Properties field; a cell being typed in keeps its text when `keepTyping` is true.
- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping;
@end

@interface DDTextArea : NSView
@property(nonatomic, copy) void (^onCommit)(void);
@property(nonatomic, copy) NSString* text;
@property(nonatomic, copy) NSString* placeholder;
/// True while it is being typed in.
@property(nonatomic, readonly) BOOL editing;
@end
