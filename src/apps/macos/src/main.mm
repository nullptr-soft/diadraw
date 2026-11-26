// DiaDraw for macOS: an AppKit shell over the C++ core. The menus are built in code; documents are
// .draw files (DDDocument).

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "DDDocument.h"
#include "host/about.hpp"
#include "host/credits.hpp"
#include "host/document.hpp"
#include "host/examples.hpp"
#include "host/feedback.hpp"
#include "host/libraries.hpp"


@interface DDAppDelegate : NSObject <NSApplicationDelegate>
@end

namespace {

NSMenuItem* item(NSString* title, SEL action, NSString* key, NSEventModifierFlags flags = NSEventModifierFlagCommand) {
  NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
  i.keyEquivalentModifierMask = flags;
  return i;
}

NSMenu* submenu(NSMenu* bar, NSString* title) {
  NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
  top.submenu = menu;
  [bar addItem:top];
  return menu;
}

NSMenu* build_menu() {
  NSMenu* bar = [[NSMenu alloc] init];
  NSString* name = @"DiaDraw";

  NSMenu* app = submenu(bar, name);
  [app addItem:item([@"About " stringByAppendingString:name], @selector(showAbout:), @"")];
  [app addItem:NSMenuItem.separatorItem];
  [app addItem:item([@"Hide " stringByAppendingString:name], @selector(hide:), @"h")];
  [app addItem:item(@"Hide Others", @selector(hideOtherApplications:), @"h",
                    NSEventModifierFlagCommand | NSEventModifierFlagOption)];
  [app addItem:item(@"Show All", @selector(unhideAllApplications:), @"")];
  [app addItem:NSMenuItem.separatorItem];
  [app addItem:item([@"Quit " stringByAppendingString:name], @selector(terminate:), @"q")];

  NSMenu* file = submenu(bar, @"File");
  [file addItem:item(@"New", @selector(newDocument:), @"n")];
  [file addItem:item(@"Open…", @selector(openDocument:), @"o")];
  [file addItem:item(@"Import…", @selector(importDiagram:), @"i", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  NSMenuItem* examplesTop = [[NSMenuItem alloc] initWithTitle:@"Examples" action:nil keyEquivalent:@""];
  NSMenu* examplesMenu = [[NSMenu alloc] initWithTitle:@"Examples"];
  const auto examples = dd::host::examples();
  for (std::size_t i = 0; i < examples.size(); ++i) {
    const auto& e = examples[i];
    NSString* name = [[NSString alloc] initWithBytes:e.name.data() length:e.name.size() encoding:NSUTF8StringEncoding];
    NSMenuItem* entry = item(name, @selector(openExample:), @"");
    entry.tag = static_cast<NSInteger>(i);  // its index in dd::host::examples()
    [examplesMenu addItem:entry];
  }
  examplesTop.submenu = examplesMenu;
  [file addItem:examplesTop];
  [file addItem:NSMenuItem.separatorItem];
  [file addItem:item(@"Close", @selector(performClose:), @"w")];
  [file addItem:item(@"Save…", @selector(saveDocument:), @"s")];
  [file addItem:item(@"Duplicate", @selector(duplicateDocument:), @"s", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  [file addItem:item(@"Revert To…", @selector(revertDocumentToSaved:), @"")];
  [file addItem:item(@"Cloud Storage…", @selector(showCloudStorage:), @"")];
  [file addItem:NSMenuItem.separatorItem];
  NSMenuItem* exportTop = [[NSMenuItem alloc] initWithTitle:@"Export" action:nil keyEquivalent:@""];
  NSMenu* exportMenu = [[NSMenu alloc] initWithTitle:@"Export"];
  [exportMenu addItem:item(@"SVG…", @selector(exportSVG:), @"")];
  [exportMenu addItem:item(@"PNG…", @selector(exportPNG:), @"")];
  [exportMenu addItem:item(@"PDF…", @selector(exportPDF:), @"")];
  [exportMenu addItem:item(@"draw.io…", @selector(exportDrawio:), @"")];
  [exportMenu addItem:item(@"BPMN…", @selector(exportBPMN:), @"")];
  exportTop.submenu = exportMenu;
  [file addItem:exportTop];

  NSMenu* edit = submenu(bar, @"Edit");
  [edit addItem:item(@"Undo", @selector(undo:), @"z")];
  [edit addItem:item(@"Redo", @selector(redo:), @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItem:item(@"Cut", @selector(cut:), @"x")];
  [edit addItem:item(@"Copy", @selector(copy:), @"c")];
  [edit addItem:item(@"Paste", @selector(paste:), @"v")];
  [edit addItem:item(@"Delete", @selector(delete:), @"")];
  [edit addItem:item(@"Select All", @selector(selectAll:), @"a")];
  [edit addItem:item(@"Clear Canvas", @selector(clearCanvas:), @"")];
  [edit addItem:NSMenuItem.separatorItem];
  // F2, as VS Code and the other apps rename: the element named at the text cursor, or the one
  // selected on the canvas.
  unichar f2 = NSF2FunctionKey;
  [edit addItem:item(@"Rename Element…", @selector(renameElement:), [NSString stringWithCharacters:&f2 length:1], 0)];
  [edit addItem:item(@"Size to Fit Content", @selector(fitToContent:), @"=",
                     NSEventModifierFlagCommand | NSEventModifierFlagOption)];
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItem:item(@"Find…", @selector(performFindPanelAction:), @"f")];

  NSMenu* view = submenu(bar, @"View");
  [view addItem:item(@"Zoom In", @selector(zoomIn:), @"+")];
  [view addItem:item(@"Zoom Out", @selector(zoomOut:), @"-")];
  [view addItem:item(@"Actual Size", @selector(zoomActual:), @"0")];
  [view addItem:NSMenuItem.separatorItem];
  [view addItem:item(@"New View…", @selector(newView:), @"n", NSEventModifierFlagCommand | NSEventModifierFlagOption)];
  [view addItem:item(@"Rename View…", @selector(renameView:), @"")];
  [view addItem:item(@"Remove View", @selector(removeView:), @"")];
  [view addItem:NSMenuItem.separatorItem];
  [view addItem:item(@"Show Grid", @selector(toggleGrid:), @"'")];
  NSMenuItem* sizeTop = [[NSMenuItem alloc] initWithTitle:@"Grid Size" action:nil keyEquivalent:@""];
  NSMenu* sizes = [[NSMenu alloc] initWithTitle:@"Grid Size"];
  for (int size : {5, 10, 20, 40}) {
    NSMenuItem* entry = item([NSString stringWithFormat:@"%d", size], @selector(chooseGridSize:), @"");
    entry.tag = size;
    [sizes addItem:entry];
  }
  sizeTop.submenu = sizes;
  [view addItem:sizeTop];
  [view addItem:NSMenuItem.separatorItem];
  [view addItem:item(@"Enter Full Screen", @selector(toggleFullScreen:), @"f",
                     NSEventModifierFlagCommand | NSEventModifierFlagControl)];

  NSMenu* window = submenu(bar, @"Window");
  [window addItem:item(@"Minimize", @selector(performMiniaturize:), @"m")];
  [window addItem:item(@"Zoom", @selector(performZoom:), @"")];
  NSApp.windowsMenu = window;

  NSMenu* help = submenu(bar, @"Help");
  [help addItem:item(@"Report a Bug…", @selector(reportBug:), @"")];
  [help addItem:item(@"Give Feedback…", @selector(sendFeedback:), @"")];
  NSApp.helpMenu = help;
  return bar;
}

}  // namespace

@implementation DDAppDelegate

/// DiaDraw › About DiaDraw: the standard panel (name, version and copyright from the bundle) with
/// the licence and the open-source software the app ships, each linked, as its credits.
- (void)showAbout:(id)sender {
  NSMutableParagraphStyle* centered = [[NSMutableParagraphStyle alloc] init];
  centered.alignment = NSTextAlignmentCenter;
  NSDictionary* body = @{
    NSFontAttributeName : [NSFont systemFontOfSize:NSFont.smallSystemFontSize],
    NSForegroundColorAttributeName : NSColor.labelColor,
    NSParagraphStyleAttributeName : centered,
  };
  NSDictionary* heading = @{
    NSFontAttributeName : [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize],
    NSForegroundColorAttributeName : NSColor.labelColor,
    NSParagraphStyleAttributeName : centered,
  };
  auto text = [](const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; };
  auto append = [](NSMutableAttributedString* out, NSString* s, NSDictionary* attributes) {
    [out appendAttributedString:[[NSAttributedString alloc] initWithString:s attributes:attributes]];
  };
  auto link = [&](NSMutableAttributedString* out, NSString* s, NSString* url) {
    NSMutableDictionary* attributes = [body mutableCopy];
    if (NSURL* u = [NSURL URLWithString:url]) attributes[NSLinkAttributeName] = u;
    append(out, s, attributes);
  };
  NSMutableAttributedString* credits = [[NSMutableAttributedString alloc] init];
  append(credits, [text(dd::host::kSummary) stringByAppendingString:@"\nFree software under the "], body);
  link(credits, text(dd::host::kLicense), text(dd::host::kLicenseUrl));
  append(credits, @".\n\n", body);
  append(credits, @"Open-source software\n", heading);
  for (const auto& credit : dd::host::credits_for("macos")) {
    link(credits, text(dd::host::credit_label(credit)), text(credit.url));
    append(credits, @"\n", body);
  }
  append(credits, @"\n", body);
  link(credits, @"Everything DiaDraw uses, and what for", text(dd::host::kCreditsUrl));
  [NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionCredits : credits}];
}

- (void)applicationWillFinishLaunching:(NSNotification*)note {
  // draw.io's stencil sets ship inside the app.
  NSString* drawio = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"drawio"];
  if ([NSFileManager.defaultManager fileExistsAtPath:[drawio stringByAppendingPathComponent:@"index.json.gz"]])
    dd::host::set_drawio_dir(drawio.UTF8String);
  NSApp.mainMenu = build_menu();
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
  // For tests: DIADRAW_SNAPSHOT=<file.png> writes the front window to a PNG and quits.
  const char* snapshot = getenv("DIADRAW_SNAPSHOT");
  // A normal launch comes to the front with its window key, so the first click or keystroke goes to
  // the diagram. Launch Services only does this for some launches (not for the binary run from a
  // terminal or an IDE, and not always for a window restored minimized). The documents open during
  // launch, so wait one turn of the run loop for their windows. Test runs stay in the background.
  if (!snapshot && !getenv("DIADRAW_SCRIPT"))
    dispatch_async(dispatch_get_main_queue(), ^{ [self bringToFront]; });
  // DIADRAW_SCRIPT=<file> runs canvas gestures from the file and writes a log to <file>.log first.
  if (const char* script = getenv("DIADRAW_SCRIPT")) {
    NSString* path = [NSString stringWithUTF8String:script];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      DDDocument* doc = NSDocumentController.sharedDocumentController.documents.firstObject;
      NSString* log = [doc runScript:[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil]];
      [log writeToFile:[path stringByAppendingString:@".log"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
      if (!snapshot) [NSApp terminate:nil];
    });
  }
  if (!snapshot) return;
  NSString* target = [NSString stringWithUTF8String:snapshot];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    NSWindow* window = NSApp.orderedWindows.firstObject;
    NSView* view = window.contentView;
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:target atomically:YES];
    [NSApp terminate:nil];
  });
}

- (void)bringToFront {
  NSWindow* window = NSDocumentController.sharedDocumentController.currentDocument.windowForSheet;
  if (!window) window = NSDocumentController.sharedDocumentController.documents.firstObject.windowForSheet;
  if (window.miniaturized) [window deminiaturize:nil];
  [window makeKeyAndOrderFront:nil];
  if (@available(macOS 14.0, *))
    [NSApp activate];
  else
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication*)sender {
  return YES;
}

/// Opens the issue form for `kind` in the browser, with the app, version and macOS version filled in.
- (void)openFeedback:(dd::host::FeedbackKind)kind {
  NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
  std::string os = "macOS " + std::to_string(v.majorVersion) + "." + std::to_string(v.minorVersion);
  if (v.patchVersion) os += "." + std::to_string(v.patchVersion);
  NSString* url = [NSString stringWithUTF8String:dd::host::feedback_url(kind, {"macOS", os}).c_str()];
  NSURL* link = [NSURL URLWithString:url];
  if (link && [NSWorkspace.sharedWorkspace openURL:link]) return;
  // No browser opened: show the address so it can be copied.
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Could not open a web browser";
  alert.informativeText = [@"Open this address to report it:\n" stringByAppendingString:url];
  [alert addButtonWithTitle:@"Copy Address"];
  [alert addButtonWithTitle:@"Close"];
  if ([alert runModal] == NSAlertFirstButtonReturn) {
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:url forType:NSPasteboardTypeString];
  }
}

- (IBAction)reportBug:(id)sender {
  [self openFeedback:dd::host::FeedbackKind::Bug];
}

- (IBAction)sendFeedback:(id)sender {
  [self openFeedback:dd::host::FeedbackKind::Feedback];
}

/// File › Examples: the example opens as a new, untitled document.
- (IBAction)openExample:(NSMenuItem*)sender {
  const auto examples = dd::host::examples();
  if (sender.tag < 0 || static_cast<std::size_t>(sender.tag) >= examples.size()) return;
  const dd::host::Example& example = examples[static_cast<std::size_t>(sender.tag)];
  [self openUntitledWithText:[[NSString alloc] initWithBytes:example.source.data()
                                                       length:example.source.size()
                                                     encoding:NSUTF8StringEncoding]
                      report:nil];
}

/// A new, untitled document showing `text`, with an import report in its status line if any.
- (void)openUntitledWithText:(NSString*)text report:(NSString*)report {
  NSError* error = nil;
  DDDocument* doc = [NSDocumentController.sharedDocumentController makeUntitledDocumentOfType:@"com.diadraw.draw"
                                                                                        error:&error];
  if (!doc) return;
  doc.initialText = text;
  if (report) doc.importReport = report;
  [NSDocumentController.sharedDocumentController addDocument:doc];
  [doc makeWindowControllers];
  [doc showWindows];
}

- (IBAction)importDiagram:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.message = @"Import a Mermaid, PlantUML, Structurizr, draw.io or BPMN file";
  if ([panel runModal] != NSModalResponseOK) return;
  auto result = dd::host::import_file(panel.URL.path.UTF8String);
  if (!result) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithUTF8String:result.error().message.c_str()];
    [alert runModal];
    return;
  }
  [self openUntitledWithText:[NSString stringWithUTF8String:result->text.c_str()]
                      report:result->report.empty() ? nil : [NSString stringWithUTF8String:result->report.c_str()]];
}

@end

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    // Test runs (a script or a snapshot) happen while someone may be typing in another app: run in
    // the background, never take focus, and ignore real keyboard and mouse input. Scripts post
    // their events straight to the views, so they are not affected.
    if (getenv("DIADRAW_SCRIPT") || getenv("DIADRAW_SNAPSHOT")) {
      app.activationPolicy = NSApplicationActivationPolicyAccessory;
      [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskAny
                                            handler:^NSEvent*(NSEvent* event) {
                                              switch (event.type) {
                                                case NSEventTypeKeyDown:
                                                case NSEventTypeKeyUp:
                                                case NSEventTypeFlagsChanged:
                                                case NSEventTypeLeftMouseDown:
                                                case NSEventTypeLeftMouseUp:
                                                case NSEventTypeLeftMouseDragged:
                                                case NSEventTypeRightMouseDown:
                                                case NSEventTypeRightMouseUp:
                                                case NSEventTypeScrollWheel:
                                                case NSEventTypeMagnify: return nil;
                                                default: return event;
                                              }
                                            }];
    }
    DDAppDelegate* delegate = [[DDAppDelegate alloc] init];
    app.delegate = delegate;
    if (!getenv("DIADRAW_SCRIPT") && !getenv("DIADRAW_SNAPSHOT"))
      [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    return NSApplicationMain(argc, argv);
  }
}
