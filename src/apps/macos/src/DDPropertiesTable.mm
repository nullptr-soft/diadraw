#import "DDPropertiesTable.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "host/field_editors.hpp"

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string str(NSString* s) { return s.UTF8String ?: ""; }

/// True while a field editor inside `view` is first responder.
BOOL typingIn(NSView* view) {
  NSResponder* focus = view.window.firstResponder;
  return [focus isKindOfClass:NSTextView.class] && [(NSView*)focus isDescendantOf:view];
}

}  // namespace

// ── DDPropertiesTable ──

@interface DDPropertiesTable () <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@end

@implementation DDPropertiesTable {
  std::vector<std::pair<std::string, std::string>> _rows;
  std::string _shown;
  NSTableView* _table;
  NSScrollView* _scroll;
  NSLayoutConstraint* _height;
  NSSegmentedControl* _buttons;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;

  _table = [[NSTableView alloc] initWithFrame:NSZeroRect];
  if (@available(macOS 11.0, *)) _table.style = NSTableViewStylePlain;
  _table.rowHeight = 20;
  _table.usesAlternatingRowBackgroundColors = YES;
  _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
  _table.allowsEmptySelection = YES;
  _table.dataSource = self;
  _table.delegate = self;
  for (NSString* column in @[ @"key", @"value" ]) {
    NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:column];
    c.title = [column isEqualToString:@"key"] ? @"Key" : @"Value";
    c.resizingMask = NSTableColumnAutoresizingMask;
    c.width = 100;
    [_table addTableColumn:c];
  }

  _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  _scroll.translatesAutoresizingMaskIntoConstraints = NO;
  _scroll.documentView = _table;
  _scroll.hasVerticalScroller = NO;
  _scroll.borderType = NSBezelBorder;
  _scroll.verticalScrollElasticity = NSScrollElasticityNone;
  _scroll.horizontalScrollElasticity = NSScrollElasticityNone;

  _buttons = [NSSegmentedControl segmentedControlWithImages:@[
    [NSImage imageNamed:NSImageNameAddTemplate], [NSImage imageNamed:NSImageNameRemoveTemplate]
  ]
                                               trackingMode:NSSegmentSwitchTrackingMomentary
                                                     target:self
                                                     action:@selector(addOrRemove:)];
  _buttons.controlSize = NSControlSizeSmall;
  _buttons.translatesAutoresizingMaskIntoConstraints = NO;
  [_buttons setToolTip:@"Add a property" forSegment:0];
  [_buttons setToolTip:@"Remove the selected property" forSegment:1];

  [self addSubview:_scroll];
  [self addSubview:_buttons];
  _height = [_scroll.heightAnchor constraintEqualToConstant:44];
  [NSLayoutConstraint activateConstraints:@[
    _height,
    [_scroll.topAnchor constraintEqualToAnchor:self.topAnchor],
    [_scroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [_scroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
    [_buttons.topAnchor constraintEqualToAnchor:_scroll.bottomAnchor constant:-1],
    [_buttons.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [_buttons.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
  ]];
  [self reload];
  return self;
}

- (NSString*)text {
  return ns(_shown);
}

- (BOOL)editing {
  return typingIn(self);
}

- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping {
  if (keepTyping && self.editing) return;
  _shown = str(text);
  _rows = dd::host::property_rows(_shown);
  [self reload];
}

- (void)reload {
  [_table reloadData];
  // Tall enough for every row (and one empty row), so the panel scrolls instead of the table.
  NSInteger rows = std::max<NSInteger>(1, (NSInteger)_rows.size());
  CGFloat header = NSHeight(_table.headerView.frame);
  _height.constant = header + rows * (_table.rowHeight + _table.intercellSpacing.height) + 3;
  [_buttons setEnabled:_table.selectedRow >= 0 forSegment:1];
}

/// Writes the rows back as text and reports a change.
- (void)commitRows {
  std::string text = dd::host::property_text(_rows);
  if (text == _shown) return;
  _shown = text;
  if (self.onChange) self.onChange();
}

- (void)addOrRemove:(NSSegmentedControl*)sender {
  if (sender.selectedSegment == 0) {
    [self.window makeFirstResponder:nil];  // commits a cell being typed in
    _rows.emplace_back("", "");
    [self reload];
    NSInteger row = (NSInteger)_rows.size() - 1;
    [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    [_table editColumn:0 row:row withEvent:nil select:YES];
  } else {
    NSInteger row = _table.selectedRow;
    if (row < 0 || row >= (NSInteger)_rows.size()) return;
    [self.window makeFirstResponder:nil];
    _rows.erase(_rows.begin() + row);
    [self reload];
    [self commitRows];
  }
}

// ── Table ──

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  return (NSInteger)_rows.size();
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  NSTextField* field = [tableView makeViewWithIdentifier:column.identifier owner:self];
  if (!field) {
    field = [NSTextField textFieldWithString:@""];
    field.identifier = column.identifier;
    field.bordered = NO;
    field.drawsBackground = NO;
    field.font = [NSFont systemFontOfSize:12];
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.placeholderString = [column.identifier isEqualToString:@"key"] ? @"key" : @"value";
    field.delegate = self;
  }
  const auto& r = _rows[(std::size_t)row];
  field.stringValue = ns([column.identifier isEqualToString:@"key"] ? r.first : r.second);
  return field;
}

- (void)tableViewSelectionDidChange:(NSNotification*)note {
  [_buttons setEnabled:_table.selectedRow >= 0 forSegment:1];
}

/// Leaving a cell after typing writes it.
- (void)controlTextDidEndEditing:(NSNotification*)note {
  NSTextField* field = note.object;
  NSInteger row = [_table rowForView:field];
  if (row < 0 || row >= (NSInteger)_rows.size()) return;
  auto& r = _rows[(std::size_t)row];
  if ([field.identifier isEqualToString:@"key"])
    r.first = str(field.stringValue);
  else
    r.second = str(field.stringValue);
  [self commitRows];
}

/// Escape puts every cell back as the field was.
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:)) return NO;
  [self.window makeFirstResponder:nil];
  [self showText:ns(_shown) keepTyping:NO];
  return YES;
}

@end

// ── DDTextArea ──

@interface DDTextArea () <NSTextViewDelegate>
@end

@implementation DDTextArea {
  NSTextView* _view;
  NSScrollView* _scroll;
  NSLayoutConstraint* _height;
  NSString* _committed;
  BOOL _reverting;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  _committed = @"";
  _scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 200, 44)];
  _scroll.translatesAutoresizingMaskIntoConstraints = NO;
  _scroll.hasVerticalScroller = YES;
  _scroll.autohidesScrollers = YES;
  _scroll.borderType = NSBezelBorder;
  NSSize size = _scroll.contentSize;
  _view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
  _view.minSize = NSMakeSize(0, size.height);
  _view.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
  _view.verticallyResizable = YES;
  _view.horizontallyResizable = NO;
  _view.autoresizingMask = NSViewWidthSizable;
  _view.textContainer.widthTracksTextView = YES;
  _view.textContainerInset = NSMakeSize(2, 3);
  _view.richText = NO;
  _view.allowsUndo = YES;
  _view.automaticQuoteSubstitutionEnabled = NO;
  _view.automaticDashSubstitutionEnabled = NO;
  _view.automaticTextReplacementEnabled = NO;
  _view.font = [NSFont systemFontOfSize:12];
  _view.delegate = self;
  _scroll.documentView = _view;
  [self addSubview:_scroll];
  _height = [_scroll.heightAnchor constraintEqualToConstant:44];
  [NSLayoutConstraint activateConstraints:@[
    _height,
    [_scroll.topAnchor constraintEqualToAnchor:self.topAnchor],
    [_scroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [_scroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
    [_scroll.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
  ]];
  return self;
}

- (NSString*)text {
  return _view.string ?: @"";
}

- (void)setText:(NSString*)text {
  _committed = [text copy] ?: @"";
  _view.string = _committed;
  [self fit];
}

- (BOOL)editing {
  return self.window.firstResponder == _view;
}

/// Grows with the text, from three lines up to about twelve; past that it scrolls.
- (void)fit {
  [_view.layoutManager ensureLayoutForTextContainer:_view.textContainer];
  CGFloat used = NSHeight([_view.layoutManager usedRectForTextContainer:_view.textContainer]);
  _height.constant = std::clamp<CGFloat>(used + 2 * _view.textContainerInset.height + 4, 52, 180);
}

- (void)textDidChange:(NSNotification*)note {
  [self fit];
}

- (void)textDidEndEditing:(NSNotification*)note {
  if (_reverting || [self.text isEqualToString:_committed]) return;
  _committed = [self.text copy];
  if (self.onCommit) self.onCommit();
}

- (BOOL)textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:)) return NO;
  _reverting = YES;
  _view.string = _committed;
  [self.window makeFirstResponder:nil];
  _reverting = NO;
  [self fit];
  return YES;
}

@end
