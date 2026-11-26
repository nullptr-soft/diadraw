// A .draw document: its text is the source of truth. The window shows the text beside the canvas;
// typing updates the core session, and canvas gestures come back from the session as minimal text
// edits applied to the text view, so one undo history covers both panes.

#pragma once

#import <AppKit/AppKit.h>

@interface DDDocument : NSDocument
/// The text a new document starts with (set before the window is made, e.g. by Import).
@property(nonatomic, copy) NSString* initialText;
/// A report to show once the window is up (what an import could not carry over).
@property(nonatomic, copy) NSString* importReport;
/// File › Cloud Storage…: Google Drive and OneDrive in a sheet.
- (IBAction)showCloudStorage:(id)sender;
/// Runs a test script against the canvas (see DIADRAW_SCRIPT in main.mm); returns a log.
- (NSString*)runScript:(NSString*)script;
@end
