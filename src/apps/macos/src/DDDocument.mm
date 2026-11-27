#import "DDDocument.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/message.h>

#include <cmath>
#include <functional>
#include <optional>
#include <memory>

#import "DDCanvasView.h"
#import "DDCloudPanel.h"
#import "DDInspector.h"
#import "DDStylePanel.h"
#import "DDSyntax.h"
#import "DDTextFolds.h"
#include "diadraw/base/text.hpp"
#include "diadraw/dsl/format.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/export/export.hpp"
#include "diadraw/io/bpmn.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/model/views.hpp"
#include "host/document.hpp"
#include "host/libraries.hpp"

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }


// The grid is the person's preference, for every document: whether it shows and its step in
// diagram units. Not in the text, which stays the drawing alone.
NSString* const kShowGrid = @"DDShowGrid";
NSString* const kGridSize = @"DDGridSize";
NSString* const kGridChanged = @"DDGridChanged";

// The toolbar's editing items, centred; Help and Give Feedback follow them at the trailing end.
NSArray<NSToolbarItemIdentifier>* const kEditingItems = @[ @"view", @"viewActions", @"grid", @"add" ];
constexpr double kDefaultGridSize = 10;

double grid_size() {
  double size = [NSUserDefaults.standardUserDefaults doubleForKey:kGridSize];
  return std::isfinite(size) && size > 0 ? size : kDefaultGridSize;
}

/// The step to show the grid with: the chosen size, or 0 while it is hidden.
double shown_grid_step() { return [NSUserDefaults.standardUserDefaults boolForKey:kShowGrid] ? grid_size() : 0; }

// Format Document's indent size, the person's preference for every document.
NSString* const kIndentSize = @"DDIndentSize";

int indent_size() {
  NSInteger size = [NSUserDefaults.standardUserDefaults integerForKey:kIndentSize];
  return size >= dd::dsl::kMinIndentSize && size <= dd::dsl::kMaxIndentSize ? static_cast<int>(size)
                                                                          : dd::dsl::kDefaultIndentSize;
}

NSString* const kDefaultText =
    @"model {\n  user = person \"Customer\"\n  shop = system \"Shop\"\n  user -> shop \"browses\"\n}\n";

}  // namespace

/// The window's split view, with dividers that are easy to see and grab: a bar with a grip of three
/// dots in its middle, the same on every platform.
@interface DDSplitView : NSSplitView
@end

@implementation DDSplitView

- (CGFloat)dividerThickness {
  return 8;
}

- (void)drawDividerInRect:(NSRect)rect {
  NSColor* bar = [NSColor.windowBackgroundColor blendedColorWithFraction:0.03 ofColor:NSColor.labelColor];
  [(bar ?: NSColor.windowBackgroundColor) setFill];
  NSRectFill(rect);
  [NSColor.separatorColor setFill];
  // Over the bar, not instead of it: the separator colour is translucent.
  [NSBezierPath fillRect:NSMakeRect(NSMinX(rect), NSMinY(rect), 1, NSHeight(rect))];
  [NSBezierPath fillRect:NSMakeRect(NSMaxX(rect) - 1, NSMinY(rect), 1, NSHeight(rect))];
  [NSColor.secondaryLabelColor setFill];
  constexpr CGFloat kDot = 3, kGap = 4;
  for (int i = -1; i <= 1; ++i) {
    NSRect dot = NSMakeRect(NSMidX(rect) - kDot / 2, NSMidY(rect) - kDot / 2 + i * (kDot + kGap), kDot, kDot);
    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
  }
}

@end

@interface DDDocument () <NSTextViewDelegate, DDCanvasDelegate, NSSplitViewDelegate, DDCloudPanelDelegate>
/// Makes the document the diagram at `place` in a cloud, last saved as `text`.
- (void)takeCloudPlace:(const dd::host::cloud::Place&)place text:(NSString*)text;
@end

@implementation DDDocument {
  std::unique_ptr<dd::host::Document> _doc;
  NSString* _loadedText;
  NSTextView* _textView;
  /// The fold markers beside the text.
  DDTextFolds* _folds;
  DDCanvasView* _canvas;
  DDInspector* _inspector;
  DDStylePanel* _stylePanel;
  /// What the panel says while several items are selected.
  NSTextField* _panelNote;
  NSScrollView* _inspectorScroll;
  NSPopUpButton* _views;
  NSButton* _gridButton;
  NSTextField* _status;
  NSTimer* _timer;
  /// The indent size a test script chose (`indent`), or 0 for the user defaults'.
  int _scriptIndentSize;
  BOOL _applying;
  /// The diagnostics as placed in the text, the status summary, and whether the status line
  /// currently shows the diagnostic under the caret instead of the summary.
  std::vector<DDMark> _marks;
  NSString* _summary;
  BOOL _showingMark;
  /// Where the document lives when it came from, or was saved to, Google Drive or OneDrive.
  std::optional<dd::host::cloud::Place> _cloud;
  /// Saves to `_cloud` one at a time.
  dd::host::cloud::Saver _cloudSaver;
  DDCloudPanel* _cloudPanel;
  dd::host::cloud::Provider _lastProvider;
}

+ (BOOL)autosavesInPlace {
  return YES;
}

// ── Reading and writing ──

- (BOOL)readFromData:(NSData*)data ofType:(NSString*)type error:(NSError**)error {
  NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (!text) {
    if (error) *error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError userInfo:nil];
    return NO;
  }
  _loadedText = text;
  if (_textView) [self loadText:text];
  return YES;
}

- (NSData*)dataOfType:(NSString*)type error:(NSError**)error {
  NSString* text = _textView ? _textView.string : (_loadedText ?: self.initialText ?: @"");
  return [text dataUsingEncoding:NSUTF8StringEncoding];
}

// ── The window ──

- (void)makeWindowControllers {
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 1280, 800)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.minSize = NSMakeSize(640, 400);
  [window center];
  window.contentView = [self buildContent];
  window.toolbar = [self buildToolbar];
  window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
  NSWindowController* controller = [[NSWindowController alloc] initWithWindow:window];
  controller.shouldCascadeWindows = YES;
  [self addWindowController:controller];
  [self loadText:_loadedText ?: self.initialText ?: kDefaultText];
  [self gridChanged:nil];
  [NSNotificationCenter.defaultCenter addObserver:self
                                         selector:@selector(gridChanged:)
                                             name:kGridChanged
                                           object:nil];
  if (self.importReport.length) [self flash:self.importReport sticky:YES];
}

- (NSView*)buildContent {
  NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 800)];

  NSSplitView* split = [[DDSplitView alloc] initWithFrame:NSZeroRect];
  split.vertical = YES;
  split.delegate = self;
  split.translatesAutoresizingMaskIntoConstraints = NO;

  NSScrollView* textScroll = [NSTextView scrollableTextView];
  _textView = (NSTextView*)textScroll.documentView;
  _textView.font = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
  _textView.richText = NO;
  _textView.allowsUndo = YES;
  _textView.automaticQuoteSubstitutionEnabled = NO;
  _textView.automaticDashSubstitutionEnabled = NO;
  _textView.automaticTextReplacementEnabled = NO;
  _textView.automaticSpellingCorrectionEnabled = NO;
  _textView.textContainerInset = NSMakeSize(8, 8);
  // Source code: no soft wrapping, scroll sideways instead.
  textScroll.hasHorizontalScroller = YES;
  _textView.horizontallyResizable = YES;
  _textView.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
  _textView.textContainer.widthTracksTextView = NO;
  _textView.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
  _textView.delegate = self;
  _folds = [[DDTextFolds alloc] initWithTextView:_textView];

  NSScrollView* canvasScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  canvasScroll.hasVerticalScroller = YES;
  canvasScroll.hasHorizontalScroller = YES;
  canvasScroll.drawsBackground = NO;
  _canvas = [[DDCanvasView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
  _canvas.delegate = self;
  canvasScroll.documentView = _canvas;

  // The properties panel, on the right and always there, so selecting never moves the canvas:
  // the selected element's or relationship's properties, the document's theme and styles while
  // nothing is selected, or a note while several items are.
  NSScrollView* inspectorScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  inspectorScroll.hasVerticalScroller = YES;
  inspectorScroll.drawsBackground = NO;
  _inspector = [[DDInspector alloc] initWithFrame:NSMakeRect(0, 0, 260, 600)];
  _inspector.translatesAutoresizingMaskIntoConstraints = NO;
  _stylePanel = [[DDStylePanel alloc] initWithFrame:NSMakeRect(0, 0, 260, 600)];
  _stylePanel.translatesAutoresizingMaskIntoConstraints = NO;
  _panelNote = [NSTextField wrappingLabelWithString:@""];
  _panelNote.translatesAutoresizingMaskIntoConstraints = NO;
  _panelNote.textColor = NSColor.secondaryLabelColor;
  _panelNote.font = [NSFont systemFontOfSize:12];
  NSView* noteBox = [[NSView alloc] initWithFrame:NSZeroRect];
  noteBox.translatesAutoresizingMaskIntoConstraints = NO;
  [noteBox addSubview:_panelNote];
  [NSLayoutConstraint activateConstraints:@[
    [_panelNote.topAnchor constraintEqualToAnchor:noteBox.topAnchor constant:12],
    [_panelNote.bottomAnchor constraintEqualToAnchor:noteBox.bottomAnchor constant:-12],
    [_panelNote.leadingAnchor constraintEqualToAnchor:noteBox.leadingAnchor constant:10],
    [_panelNote.trailingAnchor constraintEqualToAnchor:noteBox.trailingAnchor constant:-10],
  ]];
  noteBox.hidden = YES;
  NSStackView* panels = [NSStackView stackViewWithViews:@[ _inspector, _stylePanel, noteBox ]];
  panels.orientation = NSUserInterfaceLayoutOrientationVertical;
  panels.alignment = NSLayoutAttributeLeading;
  panels.spacing = 0;
  panels.translatesAutoresizingMaskIntoConstraints = NO;
  inspectorScroll.documentView = panels;
  [panels.leadingAnchor constraintEqualToAnchor:inspectorScroll.contentView.leadingAnchor].active = YES;
  [panels.trailingAnchor constraintEqualToAnchor:inspectorScroll.contentView.trailingAnchor].active = YES;
  [panels.topAnchor constraintEqualToAnchor:inspectorScroll.contentView.topAnchor].active = YES;
  [_inspector.widthAnchor constraintEqualToAnchor:panels.widthAnchor].active = YES;
  [_stylePanel.widthAnchor constraintEqualToAnchor:panels.widthAnchor].active = YES;
  [noteBox.widthAnchor constraintEqualToAnchor:panels.widthAnchor].active = YES;
  __weak DDDocument* weakSelf = self;
  _inspector.onCommit = ^(const dd::host::ElementFields& fields) {
    DDDocument* me = weakSelf;
    if (!me) return;
    std::string renamed;
    [me change:[&] { return me->_doc->update(fields, &renamed); }];
    // A new id: keep the element selected under it.
    if (renamed != fields.id) {
      me->_canvas.selection = {{renamed}, {}};
      [me inspect];
    }
  };
  _inspector.checkElement = ^std::vector<dd::editor::FieldProblem>(const dd::host::ElementFields& fields) {
    DDDocument* me = weakSelf;
    if (!me || !me->_doc) return {};
    return me->_doc->problems(fields);
  };
  _inspector.checkRelationship = ^std::vector<dd::editor::FieldProblem>(const dd::host::RelationshipFields& fields) {
    DDDocument* me = weakSelf;
    if (!me || !me->_doc) return {};
    return me->_doc->problems(fields);
  };
  _stylePanel.onCommit = ^(const dd::host::DocumentFields& fields) {
    DDDocument* me = weakSelf;
    if (!me) return;
    [me change:[&]() -> dd::Result<std::optional<dd::host::Change>> {
      auto op = dd::host::document_operation(me->_doc->session().model(), me->_doc->view(), fields);
      if (!op) return op.error();
      if (!*op) return std::optional<dd::host::Change>{};
      return me->_doc->apply(**op);
    }];
  };
  _inspector.onCommitRelationship = ^(const dd::host::RelationshipFields& fields) {
    DDDocument* me = weakSelf;
    if (!me) return;
    std::string renamed;
    [me change:[&] { return me->_doc->update(fields, &renamed); }];
    // A new name is a new id: keep the line selected under it.
    if (renamed != fields.id) {
      me->_canvas.selection = {{}, {renamed}};
      [me inspect];
    }
  };
  _inspectorScroll = inspectorScroll;

  [split addArrangedSubview:textScroll];
  [split addArrangedSubview:canvasScroll];
  [split addArrangedSubview:inspectorScroll];
  [root addSubview:split];

  _status = [NSTextField labelWithString:@""];
  _status.translatesAutoresizingMaskIntoConstraints = NO;
  _status.textColor = NSColor.secondaryLabelColor;
  _status.font = [NSFont systemFontOfSize:11];
  _status.lineBreakMode = NSLineBreakByTruncatingTail;
  [root addSubview:_status];

  [NSLayoutConstraint activateConstraints:@[
    [split.topAnchor constraintEqualToAnchor:root.topAnchor],
    [split.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
    [split.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
    [split.bottomAnchor constraintEqualToAnchor:_status.topAnchor constant:-4],
    [_status.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:10],
    [_status.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-10],
    [_status.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-4],
    [textScroll.widthAnchor constraintGreaterThanOrEqualToConstant:280],
    [inspectorScroll.widthAnchor constraintGreaterThanOrEqualToConstant:240],
  ]];
  // The panel keeps its width when the window resizes; the canvas takes the difference.
  NSLayoutConstraint* preferred = [inspectorScroll.widthAnchor constraintEqualToConstant:280];
  preferred.priority = NSLayoutPriorityDefaultLow;
  preferred.active = YES;
  [split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:1];
  [split setHoldingPriority:NSLayoutPriorityDefaultLow + 10 forSubviewAtIndex:2];
  [split setPosition:440 ofDividerAtIndex:0];
  return root;
}

- (NSToolbar*)buildToolbar {
  NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"DiaDrawToolbar"];
  toolbar.delegate = (id<NSToolbarDelegate>)self;
  toolbar.displayMode = NSToolbarDisplayModeIconOnly;
  // The editing items (view and its menu, grid, Add) sit together in the middle of the toolbar;
  // Help and Give Feedback stay at the trailing end.
  toolbar.centeredItemIdentifiers = [NSSet setWithArray:kEditingItems];
  return toolbar;
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
  return [kEditingItems arrayByAddingObjectsFromArray:@[ NSToolbarFlexibleSpaceItemIdentifier, @"help", @"feedback" ]];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
  return [self toolbarDefaultItemIdentifiers:toolbar];
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)flag {
  NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
  if ([identifier isEqualToString:@"view"]) {
    _views = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 180, 24) pullsDown:NO];
    _views.target = self;
    _views.action = @selector(viewChosen:);
    item.view = _views;
    item.label = @"View";
  } else if ([identifier isEqualToString:@"viewActions"]) {
    // A pull-down beside the picker: its title is the first item (an icon), then the view actions.
    NSPopUpButton* actions = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 44, 24) pullsDown:YES];
    actions.bezelStyle = NSBezelStyleTexturedRounded;
    [actions addItemWithTitle:@""];
    actions.itemArray.firstObject.image = [NSImage imageWithSystemSymbolName:@"ellipsis.circle"
                                                    accessibilityDescription:@"View actions"];
    for (NSArray* entry in @[ @[ @"New View…", @"newView:" ], @[ @"Rename View…", @"renameView:" ],
                              @[ @"Remove View", @"removeView:" ] ]) {
      NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:entry[0] action:NSSelectorFromString(entry[1]) keyEquivalent:@""];
      i.target = self;
      [actions.menu addItem:i];
    }
    actions.toolTip = @"Add, rename or remove views";
    item.view = actions;
    item.label = @"Views";
  } else if ([identifier isEqualToString:@"grid"]) {
    NSImage* image = [NSImage imageWithSystemSymbolName:@"grid" accessibilityDescription:@"Grid"];
    _gridButton = [NSButton buttonWithImage:image target:self action:@selector(toggleGrid:)];
    _gridButton.buttonType = NSButtonTypePushOnPushOff;
    _gridButton.bezelStyle = NSBezelStyleTexturedRounded;
    _gridButton.state = shown_grid_step() > 0 ? NSControlStateValueOn : NSControlStateValueOff;
    _gridButton.toolTip = @"Show the grid; dragged elements line up with it";
    item.view = _gridButton;
    item.label = @"Grid";
  } else if ([identifier isEqualToString:@"add"]) {
    // One pull-down: its title is the first item, and choosing a kind adds an element of it.
    NSPopUpButton* add = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 80, 24) pullsDown:YES];
    [add addItemWithTitle:@"Add"];
    for (const auto& k : dd::host::element_kinds()) {
      NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:ns(k) action:@selector(addElement:) keyEquivalent:@""];
      entry.target = self;
      [add.menu addItem:entry];
    }
    add.toolTip = @"Add an element of the chosen kind";
    item.view = add;
    item.label = @"Add";
  } else if ([identifier isEqualToString:@"help"]) {
    // Help: what the canvas does with the mouse and the keyboard. No target: the app delegate
    // answers showCanvasHelp:, as it does for the Help menu's item.
    NSImage* image = [NSImage imageWithSystemSymbolName:@"questionmark.circle" accessibilityDescription:@"Help"];
    NSButton* help = [NSButton buttonWithImage:image target:nil action:@selector(showCanvasHelp:)];
    help.bezelStyle = NSBezelStyleTexturedRounded;
    help.toolTip = @"What the canvas does with the mouse and the keyboard";
    item.view = help;
    item.label = @"Help";
  } else if ([identifier isEqualToString:@"feedback"]) {
    // Give Feedback: always in sight and one click away. No target: the app delegate answers
    // sendFeedback:, as it does for the Help menu's item.
    NSImage* image = [NSImage imageWithSystemSymbolName:@"bubble.left" accessibilityDescription:nil];
    NSButton* feedback = [NSButton buttonWithTitle:@"Give Feedback"
                                             image:image
                                            target:nil
                                            action:@selector(sendFeedback:)];
    // A push button (what buttonWithTitle: makes), so bezelColor tints it with the accent colour.
    feedback.imagePosition = NSImageLeading;
    feedback.bezelColor = NSColor.controlAccentColor;
    feedback.toolTip = @"Send an idea, a wish or anything that could be easier (opens a form on GitHub)";
    item.view = feedback;
    item.label = @"Give Feedback";
  }
  return item;
}

// ── Session ──

- (void)loadText:(NSString*)text {
  // Libraries resolve as the CLI does: lockfile, cache, built-ins, draw.io sets.
  NSString* path = self.fileURL.path ?: [NSFileManager.defaultManager.currentDirectoryPath
                                            stringByAppendingPathComponent:@"untitled.draw"];
  std::string file = path.UTF8String;
  _doc = std::make_unique<dd::host::Document>(text.UTF8String, file);
  _applying = YES;
  _textView.string = text;
  _applying = NO;
  DDColourSyntax(_textView);
  [_folds textDidChange];
  [self.undoManager removeAllActions];
  [self refresh];
}

- (void)textDidChange:(NSNotification*)note {
  DDColourSyntax(_textView);
  [_folds textDidChange];
  if (_applying) return;
  [_timer invalidate];
  _timer = [NSTimer scheduledTimerWithTimeInterval:0.06
                                           repeats:NO
                                             block:^(NSTimer*) {
                                               [self syncFromText];
                                             }];
}

- (void)syncFromText {
  [_timer invalidate];  // what it would read is read now, so it must not refresh again later
  _timer = nil;
  std::string text = _textView.string.UTF8String;
  _doc->set_text(text);
  [self refresh];
}

/// The views, the canvas and the status line, from the session.
- (void)refresh {
  auto refreshed = _doc->refresh();
  const auto& views = _doc->views();
  [_views removeAllItems];
  for (const auto& view_id : views) [_views addItemWithTitle:ns("view " + view_id)];
  if (!_doc->view().empty())
    [_views selectItemAtIndex:std::find(views.begin(), views.end(), _doc->view()) - views.begin()];
  _views.enabled = views.size() > 1;  // nothing to choose between with one view
  if (refreshed.render) [_canvas showRender:*refreshed.render];
  else [_canvas clear];
  [self inspect];
  _marks = DDMarkDiagnostics(_textView, _doc->text(), _doc->session().parsed().diagnostics);
  _summary = ns(refreshed.status);
  _showingMark = NO;
  [self flash:_summary sticky:YES];
  [self showCaretDiagnostic];
}

/// The properties panel shows the one selected element or relationship, as the text has it now;
/// with nothing selected, the document's theme and styles; with several, a note saying so.
- (void)inspect {
  dd::editor::Selection selection = _canvas.selection;
  std::optional<dd::host::ElementFields> element;
  std::optional<dd::host::RelationshipFields> relationship;
  if (selection.elements.size() == 1 && selection.relationships.empty())
    element = _doc->fields(selection.elements[0]);
  else if (selection.relationships.size() == 1 && selection.elements.empty())
    relationship = _doc->relationship_fields(selection.relationships[0]);
  if (element || relationship) [_inspector setChoices:_doc->choices()];
  if (relationship)
    [_inspector showRelationshipFields:relationship];
  else
    [_inspector showFields:element];
  std::size_t selected = selection.elements.size() + selection.relationships.size();
  std::optional<dd::host::DocumentFields> document;
  if (selected == 0) document = dd::host::document_fields(_doc->session().model(), _doc->view());
  if (document) [_stylePanel setChoices:_doc->choices()];
  [_stylePanel showFields:document];
  bool note = !element && !relationship && !document;
  _panelNote.superview.hidden = !note;
  if (note)
    _panelNote.stringValue =
        selected > 1 ? [NSString stringWithFormat:@"%zu items selected. Select one element or "
                                                  @"relationship to edit its properties.",
                                                  selected]
                     : @"Nothing to edit here.";
}

/// The status line shows the message of the diagnostic under the caret, if there is one.
- (void)showCaretDiagnostic {
  NSUInteger caret = _textView.selectedRange.location;
  for (const auto& m : _marks)
    if (caret >= m.range.location && caret <= NSMaxRange(m.range)) {
      [self flash:[(m.error ? @"error: " : @"warning: ") stringByAppendingString:m.message] sticky:YES];
      _showingMark = YES;
      return;
    }
  if (_showingMark) [self flash:_summary sticky:YES];
  _showingMark = NO;
}

- (void)textViewDidChangeSelection:(NSNotification*)note {
  [_folds selectionDidChange];
  [self showCaretDiagnostic];
}

- (void)flash:(NSString*)text sticky:(BOOL)sticky {
  _status.stringValue = text ?: @"";
  _status.textColor = [text containsString:@"error"] || [text hasPrefix:@"Layout failed"] ? NSColor.systemRedColor
                                                                                          : NSColor.secondaryLabelColor;
}

- (void)viewChosen:(id)sender {
  NSInteger i = _views.indexOfSelectedItem;
  if (i >= 0 && i < (NSInteger)_doc->views().size()) _doc->choose_view(_doc->views()[i]);
  [self refresh];
}

/// Applies a session update to the text view as one undoable change.
/// Applies a document change to the text view as one undo step.
- (void)apply:(const dd::Result<std::optional<dd::host::Change>>&)result before:(const std::string&)before {
  if (!result) return [self flash:ns(result.error().message) sticky:NO];
  if (!*result) return;
  const dd::host::Change& change = **result;
  NSTextStorage* storage = _textView.textStorage;
  _applying = YES;
  [_textView breakUndoCoalescing];
  [self.undoManager beginUndoGrouping];
  // Offsets are UTF-8 in the core, UTF-16 here; the edits come last first.
  for (const auto& e : change.edits) {
    NSUInteger from = dd::utf16_offset(before, e.from), to = dd::utf16_offset(before, e.to);
    NSRange range = NSMakeRange(from, to - from);
    NSString* insert = ns(e.insert);
    if ([_textView shouldChangeTextInRange:range replacementString:insert]) {
      [storage replaceCharactersInRange:range withString:insert];
      [_textView didChangeText];
    }
  }
  [self.undoManager endUndoGrouping];
  _applying = NO;
  [self refresh];
  if (change.note) [self flash:ns(*change.note) sticky:NO];
}

/// Runs a document gesture and applies its change.
- (void)change:(const std::function<dd::Result<std::optional<dd::host::Change>>()>&)run {
  std::string before = _doc->text();
  [self apply:run() before:before];
}

- (void)addElement:(NSMenuItem*)sender {
  [self addElementOfKind:sender.title];
}

- (void)addElementOfKind:(NSString*)title {
  std::string kind = title.UTF8String;
  std::string created;
  [self change:[&] { return _doc->add(kind, &created); }];
  if (!created.empty()) _canvas.selection = {{created}, {}};
  [self inspect];
}

// ── Views ──

static NSString* const kNameRules = @"Letters, digits and _, not starting with a digit.";

/// Asks for a name in a sheet, starting from `initial`; `done` gets what was typed.
- (void)askName:(NSString*)title
           info:(NSString*)info
           verb:(NSString*)verb
        initial:(const std::string&)initial
           then:(void (^)(NSString*))done {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = title;
  alert.informativeText = info;
  [alert addButtonWithTitle:verb];
  [alert addButtonWithTitle:@"Cancel"];
  NSTextField* field = [NSTextField textFieldWithString:ns(initial)];
  field.frame = NSMakeRect(0, 0, 260, 24);
  alert.accessoryView = field;
  alert.window.initialFirstResponder = field;
  [alert beginSheetModalForWindow:self.windowForSheet
                completionHandler:^(NSModalResponse response) {
                  if (response == NSAlertFirstButtonReturn) done(field.stringValue);
                }];
}

- (IBAction)newView:(id)sender {
  [self askName:@"New View"
           info:kNameRules
           verb:@"Add"
        initial:_doc->fresh_view()
           then:^(NSString* name) {
             [self addViewNamed:name];
           }];
}

- (IBAction)renameView:(id)sender {
  [self askName:@"Rename View"
           info:kNameRules
           verb:@"Rename"
        initial:_doc->view()
           then:^(NSString* name) {
             [self renameViewTo:name];
           }];
}

- (IBAction)removeView:(id)sender {
  [self change:[&] { return _doc->remove_view(); }];
}

// ── Clear Canvas ──

/// Edit › Clear Canvas: makes the diagram empty as one undo step, asking first when there are
/// unsaved changes.
- (IBAction)clearCanvas:(id)sender {
  [self syncPendingText];
  // Saved or untouched, undo is enough; an empty diagram has nothing to clear.
  if (!self.isDocumentEdited || _doc->text() == dd::host::Document::kEmptyText) return [self clearCanvasNow];
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Clear the canvas?";
  alert.informativeText = ns(std::string(dd::host::Document::kClearDetail));
  NSButton* clear = [alert addButtonWithTitle:@"Clear"];
  clear.hasDestructiveAction = YES;
  clear.keyEquivalent = @"";        // no default button: Return cannot clear, Clear takes a click
  [alert addButtonWithTitle:@"Cancel"];  // keeps NSAlert's Escape for Cancel
  __weak DDDocument* weak = self;
  [alert beginSheetModalForWindow:self.windowForSheet
                completionHandler:^(NSModalResponse response) {
                  if (response == NSAlertFirstButtonReturn) [weak clearCanvasNow];
                }];
}

/// Typing still waiting for its sync goes in now (a fired one-shot timer is no longer valid).
- (void)syncPendingText {
  if (_timer.valid) [self syncFromText];
}

- (void)clearCanvasNow {
  [self syncPendingText];  // the change replaces the whole text, typing included
  [self change:[&] { return _doc->clear(); }];  // the refresh drops the selection, as nothing selected is left
}

// ── Formatting ──

/// Edit › Format Document: reindents the text with the chosen indent size, as one undo step.
- (IBAction)formatDocument:(id)sender {
  [self formatWithIndentSize:_scriptIndentSize > 0 ? _scriptIndentSize : indent_size()];
}

- (void)formatWithIndentSize:(int)size {
  [self syncPendingText];  // format what is typed, not what was last read
  [self change:[&] { return _doc->format(size); }];
}

- (IBAction)chooseIndentSize:(NSMenuItem*)sender {
  [NSUserDefaults.standardUserDefaults setInteger:sender.tag forKey:kIndentSize];
}

- (void)addViewNamed:(NSString*)name {
  std::string view_id = name.UTF8String;
  [self change:[&] { return _doc->add_view(view_id); }];
}

- (void)renameViewTo:(NSString*)name {
  std::string view_id = name.UTF8String;
  [self change:[&] { return _doc->rename_view(view_id); }];
}

// ── Renaming elements ──

/// The text cursor in UTF-8 bytes, once a text change still waiting for its sync is in, so the
/// cursor and the parse agree.
- (std::size_t)caretByte {
  [self syncPendingText];
  return dd::byte_offset(_doc->text(), _textView.selectedRange.location);
}

/// The element Rename Element… renames (see Document::element_to_rename).
- (std::optional<std::string>)elementToRename {
  if (!_doc) return std::nullopt;
  BOOL inText = self.windowForSheet.firstResponder == _textView;
  std::size_t caret = [self caretByte];
  return _doc->element_to_rename(_canvas.selection, inText, caret);
}

- (IBAction)renameElement:(id)sender {
  auto element = [self elementToRename];
  auto fields = element ? _doc->fields(*element) : std::nullopt;
  if (!fields) {
    [self flash:@"Put the cursor on an element's name, or select one element, to rename it" sticky:NO];
    return;
  }
  std::string id = *element;
  [self askName:[NSString stringWithFormat:@"Rename %@", ns(id)]
           info:[@"Every reference to it changes too. " stringByAppendingString:kNameRules]
           verb:@"Rename"
        initial:fields->local_id
           then:^(NSString* name) {
             [self renameElement:id to:name];
           }];
}

/// Renames an element and every reference to it; the selection follows.
- (void)renameElement:(const std::string&)element to:(NSString*)name {
  std::string to = name.UTF8String ?: "";
  std::string renamed;
  dd::editor::Selection selection = _canvas.selection;
  [self change:[&] { return _doc->rename_element(element, to, &renamed); }];
  if (renamed.empty() || renamed == element) return;
  _canvas.selection = _doc->follow_rename(std::move(selection), element, renamed);
  [_canvas setNeedsDisplay:YES];
  [self inspect];
}

// ── Canvas gestures ──

- (void)canvas:(DDCanvasView*)canvas didSelect:(const dd::editor::Selection&)selection {
  [self inspect];
  // One element selected on the canvas: show its declaration in the text.
  if (selection.elements.size() != 1) return;
  auto span = _doc->declaration_of(selection.elements[0]);
  if (!span) return;
  const std::string& text = _doc->text();
  NSUInteger from = dd::utf16_offset(text, span->from), to = dd::utf16_offset(text, span->to);
  NSRange range = NSMakeRange(from, to - from);
  _textView.selectedRange = range;
  [_textView scrollRangeToVisible:range];
}

- (void)canvas:(DDCanvasView*)canvas didDrop:(const std::string&)element box:(const dd::Box&)box freeform:(BOOL)freeform {
  [self change:[&] { return _doc->drop(element, box, freeform); }];
}

- (void)canvas:(DDCanvasView*)canvas didMove:(const std::vector<std::string>&)elements by:(const dd::Point&)offset {
  [self change:[&] { return _doc->move(elements, offset); }];
}

- (void)canvas:(DDCanvasView*)canvas didResize:(const std::string&)element box:(const dd::Box&)box {
  [self change:[&] { return _doc->resize(element, box); }];
}

- (void)canvas:(DDCanvasView*)canvas didFit:(const std::string&)element {
  [self change:[&] { return _doc->fit({element}); }];
}

- (IBAction)fitToContent:(id)sender {
  std::vector<std::string> elements = _canvas.selection.elements;
  [self change:[&] { return _doc->fit(elements); }];
}

- (void)canvas:(DDCanvasView*)canvas didRotate:(const std::string&)element degrees:(double)degrees {
  [self change:[&] { return _doc->rotate(element, degrees); }];
}

- (void)canvas:(DDCanvasView*)canvas didReroute:(const std::string&)relationship
         points:(const std::vector<dd::Point>&)points {
  [self change:[&] { return _doc->reroute(relationship, points); }];
}

- (void)canvas:(DDCanvasView*)canvas didConnect:(const std::string&)source to:(const std::string&)target {
  [self change:[&] { return _doc->connect(source, target); }];
}

- (void)canvas:(DDCanvasView*)canvas
    didReconnect:(const std::string&)relationship
             end:(dd::editor::End)end
              to:(const std::string&)element {
  std::string after;
  [self change:[&] { return _doc->reconnect(relationship, end, element, &after); }];
  // The line stays selected under its new id (a derived id follows its endpoints).
  if (!after.empty()) _canvas.selection = {{}, {after}};
  [self inspect];
}

- (void)canvas:(DDCanvasView*)canvas didRename:(const std::string&)element to:(const std::string&)name {
  [self change:[&] { return _doc->rename(element, name); }];
}

- (void)canvas:(DDCanvasView*)canvas didRelabel:(const std::string&)relationship to:(const std::optional<std::string>&)label {
  [self change:[&] { return _doc->relabel(relationship, label); }];
}

- (void)canvas:(DDCanvasView*)canvas didDelete:(const dd::editor::Selection&)selection {
  [self change:[&] { return _doc->remove(selection); }];
}

- (std::optional<dd::LayoutHint>)canvas:(DDCanvasView*)canvas
                            previewDrop:(const std::string&)element
                                    box:(const dd::Box&)box
                               freeform:(BOOL)freeform {
  return _doc->preview_drop(element, box, freeform);
}

- (std::string)canvas:(DDCanvasView*)canvas nameOf:(const std::string&)element {
  return _doc->name_of(element);
}

- (std::string)canvas:(DDCanvasView*)canvas labelOf:(const std::string&)relationship {
  return _doc->label_of(relationship);
}

// ── Scripted tests ──

/// Sends a mouse event to the canvas at a point in its own coordinates.
- (void)mouse:(NSEventType)type at:(NSPoint)p clicks:(NSInteger)clicks flags:(NSEventModifierFlags)flags {
  NSWindow* window = _canvas.window;
  NSEvent* e = [NSEvent mouseEventWithType:type
                                  location:[_canvas convertPoint:p toView:nil]
                             modifierFlags:flags
                                 timestamp:NSProcessInfo.processInfo.systemUptime
                              windowNumber:window.windowNumber
                                   context:nil
                               eventNumber:0
                                clickCount:clicks
                                  pressure:1];
  if (type == NSEventTypeLeftMouseDown) [_canvas mouseDown:e];
  else if (type == NSEventTypeLeftMouseDragged) [_canvas mouseDragged:e];
  else [_canvas mouseUp:e];
}

- (void)dragFrom:(NSPoint)a to:(NSPoint)b flags:(NSEventModifierFlags)flags {
  [self mouse:NSEventTypeLeftMouseDown at:a clicks:1 flags:flags];
  for (int i = 1; i <= 5; ++i)
    [self mouse:NSEventTypeLeftMouseDragged
             at:NSMakePoint(a.x + (b.x - a.x) * i / 5, a.y + (b.y - a.y) * i / 5)
         clicks:1
          flags:flags];
  [self mouse:NSEventTypeLeftMouseUp at:b clicks:1 flags:flags];
}

- (NSString*)runScript:(NSString*)script {
  NSMutableString* log = [NSMutableString string];
  [self syncFromText];
  // The script never activates the app, so keystrokes meant for other apps cannot reach it; in
  // turn, focus changes must not close a field the script opened.
  _canvas.scripted = YES;
  // Scripts start without the grid, whatever the person chose; `grid <step>` shows it.
  _canvas.grid = 0;
  // Each step is its own undo group, as each gesture is its own event in real use.
  self.undoManager.groupsByEvent = NO;
  for (NSString* raw in [script componentsSeparatedByString:@"\n"]) {
    NSString* line = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (line.length == 0 || [line hasPrefix:@"#"]) continue;
    NSArray<NSString*>* w = [line componentsSeparatedByString:@" "];
    NSString* cmd = w[0];
    auto center = [&](NSString* id) { return [_canvas centerOf:id.UTF8String]; };
    BOOL ok = YES;
    if ([cmd isEqualToString:@"view"] && w.count > 1) {
      _doc->choose_view(w[1].UTF8String);
      [self refresh];
    } else if ([cmd isEqualToString:@"select"] && w.count > 1) {
      // An element by its id, or a relationship (clicked on the middle of its line).
      auto p = center(w[1]);
      if (!p) p = [_canvas labelPointOf:w[1].UTF8String];
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*p clicks:1 flags:0];
      }
    } else if ([cmd isEqualToString:@"add-select"] && w.count > 1) {
      // add-select <id>: Command-click, adding an element or relationship to the selection (or taking it out)
      auto p = center(w[1]);
      if (!p) p = [_canvas labelPointOf:w[1].UTF8String];
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:1 flags:NSEventModifierFlagCommand];
        [self mouse:NSEventTypeLeftMouseUp at:*p clicks:1 flags:NSEventModifierFlagCommand];
      }
    } else if ([cmd isEqualToString:@"band"] && w.count > 4) {
      // band <x1> <y1> <x2> <y2> [shift]: drags a selection band between two points in diagram units
      NSPoint a = [_canvas viewPointFor:w[1].doubleValue y:w[2].doubleValue];
      NSPoint b = [_canvas viewPointFor:w[3].doubleValue y:w[4].doubleValue];
      [self dragFrom:a to:b flags:w.count > 5 ? NSEventModifierFlagShift : 0];
    } else if ([cmd isEqualToString:@"select-all"]) {
      [_canvas selectAll:nil];
    } else if ([cmd isEqualToString:@"delete-selection"]) {
      [_canvas delete:nil];
    } else if ([cmd isEqualToString:@"selection"]) {
      // selection: logs what is selected
      dd::editor::Selection s = _canvas.selection;
      [log appendString:@"selected:"];
      for (const auto& id : s.elements) [log appendFormat:@" %@", ns(id)];
      for (const auto& id : s.relationships) [log appendFormat:@" %@", ns(id)];
      [log appendString:@"\n"];
    } else if ([cmd isEqualToString:@"drag"] && w.count > 3) {
      // drag <id> <dx> <dy> [option]: in diagram units
      auto p = center(w[1]);
      if ((ok = p.has_value())) {
        CGFloat scale = 1 / [_canvas unitsPerPoint];
        NSPoint to = NSMakePoint(p->x + w[2].doubleValue * scale, p->y + w[3].doubleValue * scale);
        [self dragFrom:*p to:to flags:w.count > 4 ? NSEventModifierFlagOption : 0];
      }
    } else if ([cmd isEqualToString:@"connect"] && w.count > 2) {
      auto a = center(w[1]);
      if ((ok = a.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*a clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*a clicks:1 flags:0];
        auto h = [_canvas handlePoint];
        auto b = center(w[2]);
        if ((ok = h && b)) [self dragFrom:*h to:*b flags:0];
      }
    } else if ([cmd isEqualToString:@"reconnect"] && w.count > 3) {
      // reconnect <relationship> source|target <element>: select the line, drag that end.
      auto l = [_canvas labelPointOf:w[1].UTF8String];
      BOOL source = [w[2] isEqualToString:@"source"];
      if ((ok = l.has_value() && (source || [w[2] isEqualToString:@"target"]))) {
        [self mouse:NSEventTypeLeftMouseDown at:*l clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*l clicks:1 flags:0];
        auto a = [_canvas endPoint:source ? dd::editor::End::Source : dd::editor::End::Target];
        auto b = center(w[3]);
        if ((ok = a && b)) [self dragFrom:*a to:*b flags:0];
      }
    } else if ([cmd isEqualToString:@"resize"] && w.count > 3) {
      // resize <id> <dx> <dy>: select it and drag its bottom-right corner, in diagram units.
      auto p = center(w[1]);
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*p clicks:1 flags:0];
        auto corner = [_canvas resizePoint:2];
        if ((ok = corner.has_value())) {
          CGFloat scale = 1 / [_canvas unitsPerPoint];
          [self dragFrom:*corner
                      to:NSMakePoint(corner->x + w[2].doubleValue * scale, corner->y + w[3].doubleValue * scale)
                   flags:0];
        }
      }
    } else if ([cmd isEqualToString:@"fit"] && w.count > 1) {
      // fit <id>: select it and double-click its top-left resize corner.
      auto p = center(w[1]);
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*p clicks:1 flags:0];
        auto corner = [_canvas resizePoint:0];
        if ((ok = corner.has_value())) {
          [self mouse:NSEventTypeLeftMouseDown at:*corner clicks:1 flags:0];
          [self mouse:NSEventTypeLeftMouseUp at:*corner clicks:1 flags:0];
          [self mouse:NSEventTypeLeftMouseDown at:*corner clicks:2 flags:0];
          [self mouse:NSEventTypeLeftMouseUp at:*corner clicks:2 flags:0];
        }
      }
    } else if ([cmd isEqualToString:@"rename"] && w.count > 2) {
      auto p = center(w[1]);
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:2 flags:0];
        ok = [_canvas commitEditing:[[w subarrayWithRange:NSMakeRange(2, w.count - 2)] componentsJoinedByString:@" "]];
      }
    } else if ([cmd isEqualToString:@"relabel"] && w.count > 1) {
      auto p = [_canvas labelPointOf:w[1].UTF8String];
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:2 flags:0];
        ok = [_canvas commitEditing:[[w subarrayWithRange:NSMakeRange(2, w.count - 2)] componentsJoinedByString:@" "]];
      }
    } else if ([cmd isEqualToString:@"delete"] && w.count > 1) {
      auto p = center(w[1]);
      if ((ok = p.has_value())) {
        [self mouse:NSEventTypeLeftMouseDown at:*p clicks:1 flags:0];
        [self mouse:NSEventTypeLeftMouseUp at:*p clicks:1 flags:0];
        [_canvas delete:nil];
      }
    } else if ([cmd isEqualToString:@"set"] && w.count > 1) {
      // set <field> <value…>: types into the inspector of the selected element or relationship, or
      // into the theme and styles panel when nothing is selected, and commits.
      NSString* value = w.count > 2 ? [[w subarrayWithRange:NSMakeRange(2, w.count - 2)] componentsJoinedByString:@" "] : @"";
      value = [value stringByReplacingOccurrencesOfString:@"\\n" withString:@"\n"];
      ok = _inspector.hidden ? [_stylePanel setField:w[1] to:value] : [_inspector setField:w[1] to:value];
    } else if ([cmd isEqualToString:@"enter"] && w.count > 1) {
      // enter <field> <value…>: types into an inspector field without committing it
      NSString* value = w.count > 2 ? [[w subarrayWithRange:NSMakeRange(2, w.count - 2)] componentsJoinedByString:@" "] : @"";
      ok = [_inspector enterField:w[1] to:value];
    } else if ([cmd isEqualToString:@"invalid"]) {
      // invalid: logs the inspector fields outlined as invalid, with why
      auto invalid = [_inspector invalidFields];
      [log appendString:@"invalid:"];
      for (const auto& p : invalid) [log appendFormat:@" %@", ns(p.field)];
      [log appendString:@"\n"];
      for (const auto& p : invalid) [log appendFormat:@"  %@: %@\n", ns(p.field), ns(p.message)];
    } else if ([cmd isEqualToString:@"styles"]) {
      // styles: clears the selection, so the panel shows the document's theme and styles.
      _canvas.selection = dd::editor::Selection{};
      [_canvas setNeedsDisplay:YES];
      [self inspect];
    } else if ([cmd isEqualToString:@"grid"] && w.count > 1) {
      // grid <step>: shows the grid with that step in diagram units (0 hides it)
      _canvas.grid = w[1].doubleValue;
    } else if ([cmd isEqualToString:@"add"] && w.count > 1) {
      [self addElementOfKind:w[1]];
    } else if ([cmd isEqualToString:@"caret"] && w.count > 2) {
      // caret <line> <column>: 1-based, the column in UTF-16 units
      NSString* text = _textView.string;
      NSUInteger at = 0;
      for (NSInteger line = 1; line < w[1].integerValue && at < text.length; ++line) {
        NSRange nl = [text rangeOfString:@"\n" options:0 range:NSMakeRange(at, text.length - at)];
        at = nl.location == NSNotFound ? text.length : NSMaxRange(nl);
      }
      at = MIN(at + w[2].integerValue - 1, text.length);
      [_canvas.window makeFirstResponder:_textView];
      _textView.selectedRange = NSMakeRange(at, 0);
    } else if ([cmd isEqualToString:@"new-view"] && w.count > 1) {
      [self addViewNamed:w[1]];
    } else if (([cmd isEqualToString:@"rename-at"] || [cmd isEqualToString:@"rename-selected"]) && w.count > 1) {
      // rename-at <name>: Rename Element… in the text, on the element named at the cursor;
      // rename-selected <name>: on the canvas, on the one selected element
      auto element = [cmd isEqualToString:@"rename-at"]
                         ? _doc->element_at([self caretByte])
                         : _doc->element_to_rename(_canvas.selection, NO, dd::host::Document::kNoCaret);
      if ((ok = element.has_value()))
        [self renameElement:*element to:[[w subarrayWithRange:NSMakeRange(1, w.count - 1)] componentsJoinedByString:@" "]];
    } else if ([cmd isEqualToString:@"rename-view"] && w.count > 1) {
      [self renameViewTo:w[1]];
    } else if ([cmd isEqualToString:@"remove-view"]) {
      [self removeView:nil];
    } else if ([cmd isEqualToString:@"clear-canvas"]) {
      [self clearCanvasNow];
    } else if ([cmd isEqualToString:@"indent"] && w.count > 1) {
      // indent <size>: chooses Indent Size for this run (the user defaults stay as they are)
      _scriptIndentSize = static_cast<int>(w[1].integerValue);
    } else if ([cmd isEqualToString:@"format"]) {
      [self formatDocument:nil];
    } else if ([cmd isEqualToString:@"fold"] && w.count > 1) {
      // fold <line>: clicks the fold marker on that line (1-based), folding or unfolding its block
      ok = [_folds toggleLine:w[1].integerValue - 1];
    } else if ([cmd isEqualToString:@"folds"]) {
      // folds: logs the lines with a fold marker, the folded ones marked with *
      [log appendString:@"folds:"];
      for (const auto& m : [_folds foldMarkers]) [log appendFormat:@" %ld%@", (long)(m.line + 1), m.folded ? @"*" : @""];
      [log appendString:@"\n"];
    } else if ([cmd isEqualToString:@"views"]) {
      // views: logs the views, the one on the canvas marked with *
      [log appendString:@"views:"];
      for (const auto& id : _doc->views()) [log appendFormat:@" %@%@", ns(id), id == _doc->view() ? @"*" : @""];
      [log appendString:@"\n"];
    } else if ([cmd isEqualToString:@"undo"]) {
      [self.undoManager undo];
      [self syncFromText];
    } else if ([cmd isEqualToString:@"redo"]) {
      [self.undoManager redo];
      [self syncFromText];
    } else if ([cmd isEqualToString:@"type"] && w.count > 1) {
      // type <text>: appends a line at the end of the text, as typing would.
      NSString* text = [[[w subarrayWithRange:NSMakeRange(1, w.count - 1)] componentsJoinedByString:@" "]
          stringByAppendingString:@"\n"];
      NSRange end = NSMakeRange(_textView.string.length, 0);
      [self.undoManager beginUndoGrouping];
      if ([_textView shouldChangeTextInRange:end replacementString:text]) {
        [_textView.textStorage replaceCharactersInRange:end withString:text];
        [_textView didChangeText];
      }
      [self.undoManager endUndoGrouping];
      [self syncFromText];
    } else {
      ok = NO;
    }
    [log appendFormat:@"%@ %@\n", ok ? @"ok  " : @"FAIL", line];
  }
  self.undoManager.groupsByEvent = YES;
  _canvas.scripted = NO;
  [log appendFormat:@"--- text\n%@--- status\n%@\n", _textView.string, _status.stringValue];
  return log;
}

// ── Google Drive and OneDrive ──

- (IBAction)showCloudStorage:(id)sender {
  [self showCloud:std::nullopt message:nil];
}

- (void)showCloud:(std::optional<dd::host::cloud::Provider>)provider message:(NSString*)message {
  if (!_cloudPanel) {
    _cloudPanel = [[DDCloudPanel alloc] initWithDelegate:self];
    _lastProvider = dd::host::cloud::Provider::GoogleDrive;
  }
  const auto chosen = provider ? *provider : _cloud ? _cloud->provider : _lastProvider;
  _lastProvider = chosen;
  [_cloudPanel showOn:self.windowForSheet provider:chosen message:message];
}

- (NSString*)displayName {
  if (!_cloud) return super.displayName;
  return [NSString stringWithFormat:@"%@ (%@)", ns(dd::host::cloud::plain_name(_cloud->name)),
                                    ns(std::string(dd::host::cloud::label(_cloud->provider)))];
}

- (void)takeCloudPlace:(const dd::host::cloud::Place&)place text:(NSString*)text {
  _cloud = place;
  _lastProvider = place.provider;
  if ([_textView.string isEqualToString:text]) [self updateChangeCount:NSChangeCleared];
  for (NSWindowController* controller in self.windowControllers) [controller synchronizeWindowTitleWithDocumentName];
}

/// Saves the text to the document's place in a cloud, in the background; `done` gets whether it did.
- (void)saveToCloud:(std::function<void(bool)>)done {
  namespace cloud = dd::host::cloud;
  NSString* text = [_textView.string copy] ?: @"";
  [self flash:[NSString stringWithFormat:@"Saving to %@…", ns(std::string(cloud::label(_cloud->provider)))] sticky:NO];
  __weak DDDocument* weak = self;
  _cloudSaver.save(DDPostToMain, *_cloud, text.UTF8String ?: "", [weak, done](const cloud::SaveOutcome& outcome) {
    DDDocument* doc = weak;
    if (!doc) return;
    if (outcome.needs_sign_in) {
      // The sign-in ran out: sign in from the panel, then save again.
      [doc showCloud:outcome.place.provider message:ns(outcome.message)];
    } else {
      // Typing meanwhile, or a move to another place, keeps the document edited.
      if (outcome.ok && doc->_cloud && doc->_cloud->provider == outcome.place.provider &&
          doc->_cloud->id == outcome.place.id && std::string(doc->_textView.string.UTF8String ?: "") == outcome.text)
        [doc updateChangeCount:NSChangeCleared];
      [doc flash:ns(outcome.message) sticky:NO];
    }
    if (done) done(outcome.ok);
  });
}

- (IBAction)saveDocument:(id)sender {
  if (!_cloud) return [super saveDocument:sender];
  [self saveToCloud:nullptr];
}

// A diagram from a cloud with unsaved changes: offer to save it there, not to a file.
- (void)canCloseDocumentWithDelegate:(id)delegate shouldCloseSelector:(SEL)selector contextInfo:(void*)context {
  if (!_cloud || !self.isDocumentEdited)
    return [super canCloseDocumentWithDelegate:delegate shouldCloseSelector:selector contextInfo:context];
  __weak DDDocument* weak = self;
  std::function<void(bool)> reply = [delegate, selector, context, weak](bool close) {
    DDDocument* doc = weak;
    if (doc && delegate && selector)
      ((void (*)(id, SEL, NSDocument*, BOOL, void*))objc_msgSend)(delegate, selector, doc, close ? YES : NO, context);
  };
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = [NSString stringWithFormat:@"Save changes to “%@” in %@?",
                                                 ns(dd::host::cloud::plain_name(_cloud->name)),
                                                 ns(std::string(dd::host::cloud::label(_cloud->provider)))];
  alert.informativeText = @"Your changes are lost if you don't save them.";
  [alert addButtonWithTitle:@"Save"];
  [alert addButtonWithTitle:@"Cancel"];
  [alert addButtonWithTitle:@"Don't Save"];
  [alert beginSheetModalForWindow:self.windowForSheet
                completionHandler:^(NSModalResponse response) {
                  DDDocument* doc = weak;
                  if (!doc || response == NSAlertSecondButtonReturn) return reply(false);
                  if (response == NSAlertThirdButtonReturn) return reply(true);
                  [doc saveToCloud:reply];
                }];
}

- (NSString*)cloudText {
  return _textView.string ?: @"";
}

- (NSString*)cloudSuggestedName {
  if (_cloud) return ns(dd::host::cloud::plain_name(_cloud->name));
  return self.fileURL ? self.fileURL.lastPathComponent.stringByDeletingPathExtension : @"Untitled";
}

- (const dd::host::cloud::Place*)cloudEditing {
  return _cloud ? &*_cloud : nullptr;
}

- (void)cloudOpened:(const dd::host::cloud::Place&)place text:(NSString*)text {
  _lastProvider = place.provider;
  // An untouched new window takes the diagram; otherwise it opens in a window of its own, as Open
  // does.
  if (!self.fileURL && !_cloud && !self.isDocumentEdited) {
    [self loadText:text];
    [self takeCloudPlace:place text:text];
    return;
  }
  NSError* error = nil;
  DDDocument* doc = [NSDocumentController.sharedDocumentController makeUntitledDocumentOfType:@"com.diadraw.draw"
                                                                                        error:&error];
  if (!doc) return;
  doc.initialText = text;
  [NSDocumentController.sharedDocumentController addDocument:doc];
  [doc makeWindowControllers];
  [doc takeCloudPlace:place text:text];
  [doc showWindows];
}

- (void)cloudSaved:(const dd::host::cloud::Place&)place text:(NSString*)text {
  [self takeCloudPlace:place text:text];
  [self flash:[NSString stringWithFormat:@"Saved %@ in %@", ns(place.name),
                                         ns(std::string(dd::host::cloud::label(place.provider)))]
       sticky:NO];
}

- (void)cloudRenamed:(const dd::host::cloud::Place&)place {
  if (!_cloud || _cloud->provider != place.provider || _cloud->id != place.id) return;
  _cloud->name = place.name;
  for (NSWindowController* controller in self.windowControllers) [controller synchronizeWindowTitleWithDocumentName];
}

- (void)cloudRemoved:(const dd::host::cloud::Place&)place {
  if (!_cloud || _cloud->provider != place.provider || _cloud->id != place.id) return;
  // The text stays, unsaved: saving again asks where.
  _cloud.reset();
  [self updateChangeCount:NSChangeDone];
  for (NSWindowController* controller in self.windowControllers) [controller synchronizeWindowTitleWithDocumentName];
}

// ── Export ──

- (void)saveExport:(NSString*)extension data:(NSData*)data {
  NSSavePanel* panel = [NSSavePanel savePanel];
  NSString* base = self.displayName.stringByDeletingPathExtension;
  panel.nameFieldStringValue = [base stringByAppendingPathExtension:extension];
  UTType* type = [UTType typeWithFilenameExtension:extension];
  if (type) panel.allowedContentTypes = @[ type ];
  [panel beginSheetModalForWindow:self.windowControllers.firstObject.window
                completionHandler:^(NSModalResponse response) {
                  if (response == NSModalResponseOK) [data writeToURL:panel.URL atomically:YES];
                }];
}

- (void)export:(NSString*)extension result:(const dd::Result<std::string>&)result {
  if (!result) return [self flash:ns(result.error().message) sticky:NO];
  [self saveExport:extension data:[NSData dataWithBytes:result->data() length:result->size()]];
}

- (IBAction)exportSVG:(id)sender {
  [self export:@"svg" result:_doc->svg()];
}
- (IBAction)exportPNG:(id)sender {
  [self export:@"png" result:_doc->png()];
}
- (IBAction)exportPDF:(id)sender {
  [self export:@"pdf" result:_doc->pdf()];
}
- (IBAction)exportDrawio:(id)sender {
  [self export:@"drawio" result:_doc->drawio()];
}
- (IBAction)exportBPMN:(id)sender {
  [self export:@"bpmn" result:_doc->bpmn()];
}

// ── Grid ──

- (IBAction)toggleGrid:(id)sender {
  [NSUserDefaults.standardUserDefaults setBool:shown_grid_step() == 0 forKey:kShowGrid];
  [NSNotificationCenter.defaultCenter postNotificationName:kGridChanged object:nil];
}

- (IBAction)chooseGridSize:(NSMenuItem*)sender {
  [NSUserDefaults.standardUserDefaults setDouble:sender.tag forKey:kGridSize];
  // Choosing a size is asking for the grid.
  [NSUserDefaults.standardUserDefaults setBool:YES forKey:kShowGrid];
  [NSNotificationCenter.defaultCenter postNotificationName:kGridChanged object:nil];
}

- (void)gridChanged:(NSNotification*)note {
  if (_canvas.scripted) return;  // a test script sets its own grid
  _canvas.grid = shown_grid_step();
  _gridButton.state = _canvas.grid > 0 ? NSControlStateValueOn : NSControlStateValueOff;
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item {
  if (item.action == @selector(toggleGrid:)) {
    if ([(id)item isKindOfClass:NSMenuItem.class])
      ((NSMenuItem*)item).state = shown_grid_step() > 0 ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
  }
  if (item.action == @selector(fitToContent:)) return !_canvas.selection.elements.empty();
  // Rename Element… stays enabled: with nothing to rename it says how to pick an element.
  if (item.action == @selector(renameElement:)) return _doc != nullptr;
  if (item.action == @selector(removeView:)) return _doc && _doc->views().size() > 1;  // a document keeps one view
  if (item.action == @selector(formatDocument:)) return _doc != nullptr;
  if (item.action == @selector(chooseIndentSize:)) {
    if ([(id)item isKindOfClass:NSMenuItem.class])
      ((NSMenuItem*)item).state = item.tag == indent_size() ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
  }
  if (item.action == @selector(chooseGridSize:)) {
    if ([(id)item isKindOfClass:NSMenuItem.class])
      ((NSMenuItem*)item).state = item.tag == grid_size() ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
  }
  return [super validateUserInterfaceItem:item];
}

- (IBAction)zoomIn:(id)sender {
  _canvas.zoom = _canvas.zoom * 1.25;
}
- (IBAction)zoomOut:(id)sender {
  _canvas.zoom = _canvas.zoom / 1.25;
}
- (IBAction)zoomActual:(id)sender {
  _canvas.zoom = 1;
}

@end
