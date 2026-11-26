// File › Cloud Storage…: diagrams in Google Drive and OneDrive (host/cloud.hpp), one tab each, in a
// sheet on the document window, the same as the web app's storage dialog. It signs in and out,
// lists, renames and deletes by itself; opening a diagram and saving the current one go to the
// document through its delegate.

#pragma once

#import <AppKit/AppKit.h>

#include <functional>
#include <string>

#include "host/cloud.hpp"

using DDCloudPlace = dd::host::cloud::Place;

/// Runs `fn` on the main queue; safe to call from any thread.
void DDPostToMain(std::function<void()> fn);

@protocol DDCloudPanelDelegate <NSObject>
/// The current diagram's text and a name to suggest for it.
- (NSString*)cloudText;
- (NSString*)cloudSuggestedName;
/// The diagram being edited, when it came from a cloud (highlighted in the list).
- (const DDCloudPlace*)cloudEditing;
/// A diagram was opened (with its text), saved as a new file, renamed, or deleted.
- (void)cloudOpened:(const DDCloudPlace&)place text:(NSString*)text;
- (void)cloudSaved:(const DDCloudPlace&)place text:(NSString*)text;
- (void)cloudRenamed:(const DDCloudPlace&)place;
- (void)cloudRemoved:(const DDCloudPlace&)place;
@end

@interface DDCloudPanel : NSWindowController
- (instancetype)initWithDelegate:(id<DDCloudPanelDelegate>)delegate;
/// Shows the panel as a sheet on `window`, on `provider`, with `message` above the list.
- (void)showOn:(NSWindow*)window provider:(dd::host::cloud::Provider)provider message:(NSString*)message;
@end
