#import "DDCloudPanel.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cloud = dd::host::cloud;
using cloud::File;
using cloud::Provider;

void DDPostToMain(std::function<void()> fn) {
  dispatch_async(dispatch_get_main_queue(), ^{
    fn();
  });
}

namespace {

NSString* ns(std::string_view s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

NSTextField* label(NSString* text, BOOL secondary) {
  NSTextField* field = [NSTextField wrappingLabelWithString:text];
  field.textColor = secondary ? NSColor.secondaryLabelColor : NSColor.labelColor;
  field.font = [NSFont systemFontOfSize:secondary ? NSFont.smallSystemFontSize : NSFont.systemFontSize];
  field.selectable = NO;
  return field;
}

/// "today 14:05", or the date and time, in the user's format.
NSString* describe_time(std::int64_t ms) {
  if (ms <= 0) return @"";
  NSDate* date = [NSDate dateWithTimeIntervalSince1970:static_cast<double>(ms) / 1000];
  NSDateFormatter* f = [[NSDateFormatter alloc] init];
  f.doesRelativeDateFormatting = YES;
  f.dateStyle = NSDateFormatterShortStyle;
  f.timeStyle = NSDateFormatterShortStyle;
  return [f stringFromDate:date];
}

}  // namespace

@interface DDCloudPanel () <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation DDCloudPanel {
  __weak id<DDCloudPanelDelegate> _delegate;
  Provider _provider;
  std::vector<File> _files;
  NSSegmentedControl* _tabs;
  NSTextField* _account;
  NSButton* _accountButton;
  NSTextField* _name;
  NSButton* _save;
  NSStackView* _saveRow;
  NSTextField* _error;
  NSTextField* _empty;
  NSTableView* _table;
  NSScrollView* _scroll;
  NSStackView* _rowButtons;
  NSButton* _open;
  NSButton* _rename;
  NSButton* _delete;
  NSTextField* _note;
  /// Set while a sign-in waits for the browser; Cancel sets it.
  std::shared_ptr<std::atomic<bool>> _cancelSignIn;
  int _busy;
  /// Bumped on every refresh, so a slow list from an earlier tab is dropped.
  int _generation;
}

- (instancetype)initWithDelegate:(id<DDCloudPanelDelegate>)delegate {
  NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 480)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:YES];
  panel.title = @"Cloud Storage";
  panel.minSize = NSMakeSize(420, 360);
  if (!(self = [super initWithWindow:panel])) return nil;
  _delegate = delegate;
  _provider = Provider::GoogleDrive;
  [self build];
  return self;
}

- (void)build {
  _tabs = [NSSegmentedControl segmentedControlWithLabels:@[ @"Google Drive", @"OneDrive" ]
                                            trackingMode:NSSegmentSwitchTrackingSelectOne
                                                  target:self
                                                  action:@selector(tabChosen:)];
  _account = label(@"", NO);
  _accountButton = [NSButton buttonWithTitle:@"Sign In" target:self action:@selector(accountClicked:)];
  NSStackView* accountRow = [NSStackView stackViewWithViews:@[ _account, _accountButton ]];
  [_account setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

  _name = [NSTextField textFieldWithString:@""];
  _name.placeholderString = @"Name";
  _name.target = self;
  _name.action = @selector(saveClicked:);
  _save = [NSButton buttonWithTitle:@"Save Current Diagram" target:self action:@selector(saveClicked:)];
  _saveRow = [NSStackView stackViewWithViews:@[ _name, _save ]];
  [_name setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

  _error = label(@"", NO);
  _error.textColor = NSColor.systemRedColor;
  _empty = label(@"", YES);

  _table = [[NSTableView alloc] init];
  NSTableColumn* name = [[NSTableColumn alloc] initWithIdentifier:@"name"];
  name.title = @"Name";
  name.resizingMask = NSTableColumnAutoresizingMask;
  NSTableColumn* time = [[NSTableColumn alloc] initWithIdentifier:@"time"];
  time.title = @"Modified";
  time.width = 150;
  [_table addTableColumn:name];
  [_table addTableColumn:time];
  _table.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
  _table.dataSource = self;
  _table.delegate = self;
  _table.target = self;
  _table.doubleAction = @selector(openClicked:);
  _table.usesAlternatingRowBackgroundColors = YES;
  _scroll = [[NSScrollView alloc] init];
  _scroll.documentView = _table;
  _scroll.hasVerticalScroller = YES;
  _scroll.borderType = NSBezelBorder;
  [_scroll setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];

  _open = [NSButton buttonWithTitle:@"Open" target:self action:@selector(openClicked:)];
  _rename = [NSButton buttonWithTitle:@"Rename…" target:self action:@selector(renameClicked:)];
  _delete = [NSButton buttonWithTitle:@"Delete…" target:self action:@selector(deleteClicked:)];
  NSButton* close = [NSButton buttonWithTitle:@"Close" target:self action:@selector(closeClicked:)];
  close.keyEquivalent = @"\e";
  NSView* spacer = [[NSView alloc] init];
  [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  _rowButtons = [NSStackView stackViewWithViews:@[ _open, _rename, _delete ]];
  NSStackView* bottom = [NSStackView stackViewWithViews:@[ _rowButtons, spacer, close ]];

  _note = label(@"", YES);

  NSStackView* stack = [NSStackView stackViewWithViews:@[ _tabs, accountRow, _saveRow, _error, _empty, _scroll, bottom, _note ]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 10;
  stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
  for (NSView* v in @[ accountRow, _saveRow, _scroll, bottom, _note, _error, _empty ])
    [v.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-32].active = YES;
  [_scroll.heightAnchor constraintGreaterThanOrEqualToConstant:180].active = YES;
  self.window.contentView = stack;
}

- (void)showOn:(NSWindow*)window provider:(Provider)provider message:(NSString*)message {
  _provider = provider;
  _tabs.selectedSegment = provider == Provider::GoogleDrive ? 0 : 1;
  _name.stringValue = [_delegate cloudSuggestedName] ?: @"Untitled";
  if (!self.window.sheetParent) [window beginSheet:self.window completionHandler:nil];
  [self refresh];
  if (message.length) [self showError:message];
}

- (void)closeClicked:(id)sender {
  if (_cancelSignIn) _cancelSignIn->store(true);
  if (self.window.sheetParent) [self.window.sheetParent endSheet:self.window];
}

/// Runs `work` off the main thread and `done` back on it, unless the panel is gone by then.
- (void)run:(std::function<void()>)work then:(std::function<void()>)done {
  [self setBusy:YES];
  __weak DDCloudPanel* weak = self;
  cloud::in_background<bool>(
      DDPostToMain,
      [work] {
        work();
        return dd::Result<bool>(true);
      },
      [weak, done](dd::Result<bool>) {
        DDCloudPanel* panel = weak;
        if (!panel) return;
        [panel setBusy:NO];
        done();
      });
}

- (void)setBusy:(BOOL)busy {
  _busy = busy ? _busy + 1 : std::max(_busy - 1, 0);
  BOOL idle = _busy == 0;
  _save.enabled = idle;
  _table.enabled = idle;
  _tabs.enabled = idle;
  // The account button stays live while signing in: it cancels.
  _accountButton.enabled = idle || _cancelSignIn != nullptr;
  [self updateRowButtons];
}

- (void)updateRowButtons {
  BOOL chosen = _busy == 0 && _table.selectedRow >= 0;
  _open.enabled = chosen;
  _rename.enabled = chosen;
  _delete.enabled = chosen;
}

- (void)showError:(NSString*)message {
  _error.stringValue = message ?: @"";
  _error.hidden = message.length == 0;
}

- (void)tabChosen:(id)sender {
  Provider next = _tabs.selectedSegment == 0 ? Provider::GoogleDrive : Provider::OneDrive;
  if (next == _provider) return;
  _provider = next;
  [self refresh];
}

- (void)refresh {
  const int generation = ++_generation;
  const cloud::PanelState state = cloud::panel_state(_provider, _cancelSignIn != nullptr);
  const bool signedIn = state.signed_in;
  [self showError:nil];
  _files.clear();
  [_table reloadData];
  _account.stringValue = ns(state.account_text);
  // Title case on macOS buttons.
  std::string title = state.button_text;
  if (title.rfind("Sign in", 0) == 0) title.replace(0, 7, "Sign In");
  else if (title == "Sign out") title = "Sign Out";
  _accountButton.title = ns(title);
  _accountButton.hidden = !state.show_button;
  _accountButton.keyEquivalent = state.button_primary ? @"\r" : @"";
  _saveRow.hidden = !signedIn;
  _save.toolTip = ns(state.save_tooltip);
  _delete.toolTip = ns(state.remove_tooltip);
  _scroll.hidden = !signedIn;
  _rowButtons.hidden = !signedIn;
  _note.stringValue = ns(state.note);
  _empty.hidden = !signedIn;
  [self updateRowButtons];
  if (!signedIn) return;
  _empty.stringValue = ns(state.loading_text);
  NSString* emptyText = ns(state.empty_text);
  const Provider provider = _provider;
  auto files = std::make_shared<dd::Result<std::vector<File>>>(dd::fail(""));
  __weak DDCloudPanel* weak = self;
  std::function<void()> work = [provider, files] { *files = cloud::drive(provider).list(); };
  std::function<void()> done = [weak, generation, files, emptyText] {
    DDCloudPanel* panel = weak;
    if (!panel || generation != panel->_generation) return;
    [panel showList:*files emptyText:emptyText];
  };
  [self run:work then:done];
}

- (void)showList:(const dd::Result<std::vector<File>>&)files emptyText:(NSString*)emptyText {
  if (!files) {
    _empty.hidden = YES;
    // A sign-in that ran out shows the Sign In button again.
    if (!cloud::drive(_provider).account().signed_in()) [self refresh];
    return [self showError:ns(files.error().message)];
  }
  _files = *files;
  _empty.stringValue = emptyText;
  _empty.hidden = !_files.empty();
  [_table reloadData];
  if (const DDCloudPlace* editing = [_delegate cloudEditing]; editing && editing->provider == _provider) {
    for (std::size_t i = 0; i < _files.size(); ++i)
      if (_files[i].id == editing->id) [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
  }
  [self updateRowButtons];
}

// ── Signing in ──

- (void)accountClicked:(id)sender {
  if (_cancelSignIn) {
    _cancelSignIn->store(true);
  } else if (cloud::drive(_provider).account().signed_in()) {
    cloud::drive(_provider).account().sign_out();
    [self refresh];
  } else {
    [self signIn];
  }
}

- (void)signIn {
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  _cancelSignIn = cancel;
  [self refresh];
  const Provider provider = _provider;
  auto result = std::make_shared<dd::Result<bool>>(true);
  // Opens the sign-in page in the browser.
  auto open = [](const std::string& url) {
    DDPostToMain([url] {
      if (NSURL* link = [NSURL URLWithString:ns(url)]) [NSWorkspace.sharedWorkspace openURL:link];
    });
  };
  __weak DDCloudPanel* weak = self;
  std::function<void()> work = [provider, open, cancel, result] {
    *result = cloud::drive(provider).account().sign_in(open, *cancel);
  };
  std::function<void()> done = [weak, cancel, result] {
    DDCloudPanel* panel = weak;
    if (!panel) return;
    if (panel->_cancelSignIn == cancel) panel->_cancelSignIn.reset();
    [panel refresh];
    if (!*result && !cancel->load()) [panel showError:ns(result->error().message)];
    // Back to DiaDraw from the browser.
    if (@available(macOS 14, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
  };
  [self run:work then:done];
}

// ── Saving and the list ──

- (void)saveClicked:(id)sender {
  NSString* name = [_name.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  if (!name.length || _busy) return;
  const Provider provider = _provider;
  NSString* text = [_delegate cloudText] ?: @"";
  const std::string bytes = text.UTF8String;
  const std::string chosen = name.UTF8String;
  auto file = std::make_shared<dd::Result<File>>(dd::fail(""));
  __weak DDCloudPanel* weak = self;
  std::function<void()> work = [provider, bytes, chosen, file] { *file = cloud::drive(provider).create(chosen, bytes); };
  std::function<void()> done = [weak, provider, text, file] {
    DDCloudPanel* panel = weak;
    if (!panel) return;
    if (!*file) return [panel showError:ns(file->error().message)];
    id<DDCloudPanelDelegate> delegate = panel->_delegate;
    [panel closeClicked:nil];
    [delegate cloudSaved:DDCloudPlace{provider, (*file)->id, (*file)->name} text:text];
  };
  [self run:work then:done];
}

- (const File*)chosen {
  NSInteger row = _table.selectedRow;
  return row >= 0 && static_cast<std::size_t>(row) < _files.size() ? &_files[static_cast<std::size_t>(row)] : nullptr;
}

- (void)openClicked:(id)sender {
  const File* chosen = [self chosen];
  if (!chosen || _busy) return;
  const Provider provider = _provider;
  const std::string id = chosen->id;
  auto contents = std::make_shared<dd::Result<std::optional<cloud::Contents>>>(dd::fail(""));
  __weak DDCloudPanel* weak = self;
  std::function<void()> work = [provider, id, contents] { *contents = cloud::drive(provider).read(id); };
  std::function<void()> done = [weak, provider, contents] {
    DDCloudPanel* panel = weak;
    if (!panel) return;
    if (!*contents) return [panel showError:ns(contents->error().message)];
    if (!contents->value()) {
      [panel refresh];
      return [panel showError:[@"That diagram is no longer in " stringByAppendingString:ns(cloud::label(provider))]];
    }
    const cloud::Contents& c = *contents->value();
    id<DDCloudPanelDelegate> delegate = panel->_delegate;
    [panel closeClicked:nil];
    [delegate cloudOpened:DDCloudPlace{provider, c.file.id, c.file.name} text:ns(c.text)];
  };
  [self run:work then:done];
}

- (void)renameClicked:(id)sender {
  const File* chosen = [self chosen];
  if (!chosen || _busy) return;
  const File file = *chosen;
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Rename";
  [alert addButtonWithTitle:@"Rename"];
  [alert addButtonWithTitle:@"Cancel"];
  NSTextField* field = [NSTextField textFieldWithString:ns(cloud::plain_name(file.name))];
  field.frame = NSMakeRect(0, 0, 260, 24);
  alert.accessoryView = field;
  alert.window.initialFirstResponder = field;
  const Provider provider = _provider;
  __weak DDCloudPanel* weak = self;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse response) {
                  DDCloudPanel* panel = weak;
                  NSString* name = [field.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
                  if (!panel || response != NSAlertFirstButtonReturn || !name.length) return;
                  const std::string chosenName = name.UTF8String;
                  if (chosenName == cloud::plain_name(file.name)) return;
                  auto renamed = std::make_shared<dd::Result<File>>(dd::fail(""));
                  std::function<void()> work = [provider, file, chosenName, renamed] {
                    *renamed = cloud::drive(provider).rename(file.id, chosenName);
                  };
                  std::function<void()> done = [weak, provider, renamed] {
                    DDCloudPanel* p = weak;
                    if (!p) return;
                    if (!*renamed) return [p showError:ns(renamed->error().message)];
                    [p->_delegate cloudRenamed:DDCloudPlace{provider, (*renamed)->id, (*renamed)->name}];
                    [p refresh];
                  };
                  [panel run:work then:done];
                }];
}

- (void)deleteClicked:(id)sender {
  const File* chosen = [self chosen];
  if (!chosen || _busy) return;
  const File file = *chosen;
  const Provider provider = _provider;
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = [NSString stringWithFormat:@"Delete “%@”?", ns(cloud::plain_name(file.name))];
  alert.informativeText = ns(cloud::remove_detail(provider));
  [alert addButtonWithTitle:@"Delete"];
  [alert addButtonWithTitle:@"Cancel"];
  __weak DDCloudPanel* weak = self;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse response) {
                  DDCloudPanel* panel = weak;
                  if (!panel || response != NSAlertFirstButtonReturn) return;
                  auto removed = std::make_shared<dd::Result<bool>>(true);
                  std::function<void()> work = [provider, file, removed] {
                    *removed = cloud::drive(provider).remove(file.id);
                  };
                  std::function<void()> done = [weak, provider, file, removed] {
                    DDCloudPanel* p = weak;
                    if (!p) return;
                    if (!*removed) return [p showError:ns(removed->error().message)];
                    [p->_delegate cloudRemoved:DDCloudPlace{provider, file.id, file.name}];
                    [p refresh];
                  };
                  [panel run:work then:done];
                }];
}

// ── The table ──

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  return static_cast<NSInteger>(_files.size());
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  if (row < 0 || static_cast<std::size_t>(row) >= _files.size()) return nil;
  const File& file = _files[static_cast<std::size_t>(row)];
  BOOL isName = [column.identifier isEqualToString:@"name"];
  NSTextField* cell = [NSTextField labelWithString:isName ? ns(cloud::plain_name(file.name)) : describe_time(file.updated)];
  cell.lineBreakMode = NSLineBreakByTruncatingTail;
  if (!isName) cell.textColor = NSColor.secondaryLabelColor;
  const DDCloudPlace* editing = [_delegate cloudEditing];
  if (isName && editing && editing->provider == _provider && editing->id == file.id) {
    cell.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    cell.toolTip = @"The diagram in this window";
  }
  return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
  [self updateRowButtons];
}

@end
