#import "DDThemeEditors.h"

#import "DDStyleEditor.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "diadraw/render/svg.hpp"
#include "host/field_editors.hpp"
#include "host/swatches.hpp"

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string str(NSString* s) { return s.UTF8String ?: ""; }

NSColor* colorOf(const std::string& value) {
  auto c = dd::host::normalized_color(value);
  if (!c) return nil;
  unsigned v = (unsigned)std::stoul(c->substr(1), nullptr, 16);
  return [NSColor colorWithSRGBRed:((v >> 16) & 0xff) / 255.0
                             green:((v >> 8) & 0xff) / 255.0
                              blue:(v & 0xff) / 255.0
                             alpha:1];
}

std::string hexOf(NSColor* color) {
  NSColor* rgb = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (!rgb) return "";
  auto byte = [](CGFloat f) { return (int)std::lround(std::clamp<CGFloat>(f, 0, 1) * 255); };
  char out[8];
  std::snprintf(out, sizeof out, "#%02x%02x%02x", byte(rgb.redComponent), byte(rgb.greenComponent),
                byte(rgb.blueComponent));
  return out;
}

NSTextField* smallLabel(NSString* text) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:11];
  label.textColor = NSColor.secondaryLabelColor;
  return label;
}

NSButton* iconButton(NSString* symbol, NSString* tip, id target, SEL action) {
  NSButton* b = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:tip]
                                   target:target
                                   action:action];
  b.bordered = NO;
  b.toolTip = tip;
  return b;
}

NSStackView* column() {
  NSStackView* s = [[NSStackView alloc] initWithFrame:NSZeroRect];
  s.orientation = NSUserInterfaceLayoutOrientationVertical;
  s.alignment = NSLayoutAttributeLeading;
  s.spacing = 4;
  s.translatesAutoresizingMaskIntoConstraints = NO;
  return s;
}

NSStackView* row(NSArray<NSView*>* views) {
  NSStackView* s = [NSStackView stackViewWithViews:views];
  s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  s.spacing = 4;
  s.alignment = NSLayoutAttributeCenterY;
  return s;
}

BOOL typingIn(NSView* view) {
  NSResponder* focus = view.window.firstResponder;
  return [focus isKindOfClass:NSTextView.class] && [(NSView*)focus isDescendantOf:view];
}

/// Lays `content` out to fill `view`.
void fill(NSView* view, NSView* content) {
  content.translatesAutoresizingMaskIntoConstraints = NO;
  [view addSubview:content];
  [NSLayoutConstraint activateConstraints:@[
    [content.topAnchor constraintEqualToAnchor:view.topAnchor],
    [content.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
    [content.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
    [content.bottomAnchor constraintEqualToAnchor:view.bottomAnchor],
  ]];
}

}  // namespace

// ── DDColorField ──

namespace {

/// A swatch (or the field's well) in `hex`, with a thin edge so white shows on white; no colour
/// draws the diagonal line that means "default". `ring` marks the current colour. Drawn when shown,
/// so the edge and ring follow light and dark mode.
NSImage* swatchImage(NSString* hex, NSSize size, BOOL ring) {
  NSColor* color = colorOf(str(hex));
  NSImage* image = [NSImage imageWithSize:size
                                  flipped:NO
                           drawingHandler:^BOOL(NSRect r) {
                             CGFloat inset = ring ? 3 : 1;
                             NSRect box = NSInsetRect(r, inset, inset);
                             if (ring) {
                               NSBezierPath* around = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 1, 1)
                                                                                      xRadius:4
                                                                                      yRadius:4];
                               around.lineWidth = 2;
                               [NSColor.labelColor setStroke];
                               [around stroke];
                             }
                             NSBezierPath* fill = [NSBezierPath bezierPathWithRoundedRect:box xRadius:3 yRadius:3];
                             if (color) {
                               [color setFill];
                               [fill fill];
                             } else {
                               NSBezierPath* line = [NSBezierPath bezierPath];
                               [line moveToPoint:NSMakePoint(NSMinX(box) + 1, NSMinY(box) + 1)];
                               [line lineToPoint:NSMakePoint(NSMaxX(box) - 1, NSMaxY(box) - 1)];
                               line.lineWidth = 1.5;
                               [[NSColor colorWithSRGBRed:0.882 green:0.114 blue:0.282 alpha:1] setStroke];
                               [line stroke];
                             }
                             fill.lineWidth = 1;
                             [[NSColor.labelColor colorWithAlphaComponent:0.25] setStroke];
                             [fill stroke];
                             return YES;
                           }];
  return image;
}

/// The colour panel sends its colour to one field at a time; it does not retain it, so a field
/// lets go of it when it ends editing or goes away.
void* gPanelOwner = nullptr;

void releasePanel(void* owner) {
  if (gPanelOwner != owner || !NSColorPanel.sharedColorPanelExists) return;
  [NSColorPanel.sharedColorPanel setTarget:nil];
  [NSColorPanel.sharedColorPanel setAction:NULL];
  gPanelOwner = nullptr;
}

}  // namespace

@interface DDColorField () <NSPopoverDelegate>
@end

@implementation DDColorField {
  NSButton* _well;
  NSButton* _clear;
  NSTextField* _hex;
  NSString* _value;
  NSPopover* _popover;
  /// The event that last closed the swatches: when that was a click on the colour button (they
  /// close on any click outside), the click must not open them again.
  NSTimeInterval _closedBy;
  BOOL _compact;
}

- (instancetype)initCompact:(BOOL)compact {
  if (!(self = [super initWithFrame:NSZeroRect])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  _compact = compact;
  _value = @"";
  _well = [NSButton buttonWithImage:swatchImage(@"", NSMakeSize(compact ? 30 : 40, 20), NO)
                             target:self
                             action:@selector(openSwatches:)];
  _well.bordered = NO;
  _well.imagePosition = NSImageOnly;
  [_well.widthAnchor constraintEqualToConstant:compact ? 30 : 40].active = YES;
  [_well.heightAnchor constraintEqualToConstant:20].active = YES;
  _clear = iconButton(@"xmark.circle.fill", @"Use the default colour", self, @selector(cleared:));
  NSMutableArray<NSView*>* views = [NSMutableArray arrayWithObjects:_well, _clear, nil];
  if (!compact) {
    _hex = smallLabel(@"");
    _hex.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    [views addObject:_hex];
  }
  NSStackView* r = row(views);
  r.spacing = 2;
  fill(self, r);
  [self show];
  return self;
}

- (void)dealloc {
  _popover.delegate = nil;
  [_popover close];
  releasePanel((__bridge void*)self);
}

- (NSString*)value {
  return _value;
}

- (void)setValue:(NSString*)value {
  _value = [value copy] ?: @"";
  [self show];
}

- (void)show {
  _well.image = swatchImage(_value, NSMakeSize(_compact ? 30 : 40, 20), NO);
  _clear.hidden = _value.length == 0;
  _hex.stringValue = _value.length ? _value : @"default";
  _well.toolTip = _value.length ? _value : @"default";
  [_well setAccessibilityLabel:[NSString stringWithFormat:@"Colour: %@", _well.toolTip]];
}

/// Sets the value as the person picked it and reports the change.
- (void)pick:(NSString*)next {
  if ([next isEqualToString:_value]) return;
  _value = [next copy];
  [self show];
  if (self.onChange) self.onChange();
}

/// The preset swatches (dd::host::color_swatches()), row by row, then Custom… and Default.
- (void)openSwatches:(id)sender {
  if (_popover.shown) {
    [_popover close];
    return;
  }
  if (NSApp.currentEvent && NSApp.currentEvent.timestamp == _closedBy) return;
  auto current = dd::host::normalized_color(str(_value));
  NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:0 rows:0];
  grid.rowSpacing = 2;
  grid.columnSpacing = 2;
  const auto& rows = dd::host::color_swatches();
  for (std::size_t r = 0; r < rows.size(); ++r) {
    NSMutableArray<NSView*>* cells = [NSMutableArray array];
    for (const auto& swatch : rows[r]) {
      NSString* color = ns(swatch.color);
      NSButton* b = [NSButton buttonWithImage:swatchImage(color, NSMakeSize(20, 20), current == swatch.color)
                                       target:self
                                       action:@selector(swatchPicked:)];
      b.bordered = NO;
      b.imagePosition = NSImageOnly;
      b.identifier = color;
      b.toolTip = [NSString stringWithFormat:@"%@  %@", ns(swatch.name), color];
      [b setAccessibilityLabel:ns(swatch.name)];
      [b.widthAnchor constraintEqualToConstant:20].active = YES;
      [b.heightAnchor constraintEqualToConstant:20].active = YES;
      [cells addObject:b];
    }
    NSGridRow* gridRow = [grid addRowWithViews:cells];
    // A gap between the first row (the greys) and the colours.
    if (r == 1) gridRow.topPadding = 4;
  }
  NSButton* custom = [NSButton buttonWithTitle:@"Custom…" target:self action:@selector(custom:)];
  NSButton* reset = [NSButton buttonWithTitle:@"Default" target:self action:@selector(cleared:)];
  reset.toolTip = @"Use the default colour";
  reset.enabled = _value.length > 0;
  custom.controlSize = reset.controlSize = NSControlSizeSmall;
  custom.font = reset.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
  [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow - 1
                     forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* actions = row(@[ custom, spacer, reset ]);
  NSStackView* content = column();
  content.spacing = 8;
  content.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  [content addArrangedSubview:grid];
  [content addArrangedSubview:actions];
  [actions.widthAnchor constraintEqualToAnchor:grid.widthAnchor].active = YES;
  NSViewController* controller = [[NSViewController alloc] init];
  controller.view = content;
  _popover = [[NSPopover alloc] init];
  _popover.behavior = NSPopoverBehaviorTransient;
  _popover.contentViewController = controller;
  _popover.delegate = self;
  [_popover showRelativeToRect:_well.bounds
                        ofView:_well
                 preferredEdge:_well.isFlipped ? NSRectEdgeMaxY : NSRectEdgeMinY];
}

- (void)popoverDidClose:(NSNotification*)notification {
  _closedBy = NSApp.currentEvent ? NSApp.currentEvent.timestamp : -1;
}

- (void)swatchPicked:(NSButton*)sender {
  [_popover close];
  [self pick:sender.identifier];
}

/// Opens the colour panel with the current colour; it reports one colour per pick, not per drag.
- (void)custom:(id)sender {
  [_popover close];
  NSColorPanel* panel = NSColorPanel.sharedColorPanel;
  releasePanel(gPanelOwner);
  panel.showsAlpha = NO;
  panel.continuous = NO;
  panel.color = colorOf(str(_value)) ?: NSColor.whiteColor;
  [panel setTarget:self];
  [panel setAction:@selector(panelPicked:)];
  gPanelOwner = (__bridge void*)self;
  [panel orderFront:nil];
}

- (void)panelPicked:(NSColorPanel*)panel {
  std::string hex = hexOf(panel.color);
  if (!hex.empty()) [self pick:ns(hex)];
}

- (void)cleared:(id)sender {
  [_popover close];
  releasePanel((__bridge void*)self);
  [self pick:@""];
}

- (void)endColorEditing {
  [_popover close];
  releasePanel((__bridge void*)self);
}

@end

// ── DDStyleBlocks ──

@interface DDStyleBlocks () <NSComboBoxDelegate, NSTextFieldDelegate>
@end

@implementation DDStyleBlocks {
  std::vector<dd::host::BlockRow> _rows;
  std::string _shown;
  NSArray<NSString*>* _selectors;
  NSMutableIndexSet* _open;
  NSStackView* _list;
  /// Per row: its selector picker and, when open, its style editor (else NSNull).
  NSMutableArray<NSComboBox*>* _pickers;
  NSMutableArray* _editors;
  NSMutableArray<NSTextField*>* _summaries;
  BOOL _filling;
}

@synthesize onChange = _onChange;

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  _selectors = @[];
  _open = [NSMutableIndexSet indexSet];
  _pickers = [NSMutableArray array];
  _editors = [NSMutableArray array];
  _summaries = [NSMutableArray array];
  NSStackView* stack = column();
  _list = column();
  _list.spacing = 6;
  NSButton* add = [NSButton buttonWithTitle:@"Add style block" target:self action:@selector(addBlock:)];
  add.controlSize = NSControlSizeSmall;
  add.font = [NSFont systemFontOfSize:11];
  [stack addArrangedSubview:_list];
  [stack addArrangedSubview:add];
  [_list.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
  fill(self, stack);
  return self;
}

- (NSString*)text {
  return ns(_shown);
}

- (BOOL)editing {
  return typingIn(self);
}

- (void)setSelectors:(NSArray<NSString*>*)selectors {
  if ([selectors isEqualToArray:_selectors]) return;
  _selectors = [selectors copy];
  for (NSComboBox* picker in _pickers) {
    if (typingIn(picker)) continue;
    NSString* value = picker.stringValue;
    [picker removeAllItems];
    [picker addItemsWithObjectValues:_selectors];
    picker.stringValue = value;
  }
}

- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping {
  if (keepTyping && self.editing) return;
  _shown = str(text);
  auto rows = dd::host::block_rows(_shown);
  bool sameShape = rows.size() == _rows.size();
  for (std::size_t i = 0; sameShape && i < rows.size(); ++i)
    sameShape = rows[i].selector == _rows[i].selector;
  _rows = std::move(rows);
  if (sameShape && _pickers.count == _rows.size())
    [self refresh];
  else
    [self rebuild];
}

/// Shows the rows in the controls there are.
- (void)refresh {
  _filling = YES;
  for (std::size_t i = 0; i < _rows.size(); ++i) {
    if (!typingIn(_pickers[i])) _pickers[i].stringValue = ns(_rows[i].selector);
    _summaries[i].stringValue = _rows[i].style.empty() ? @"no keys yet" : ns(_rows[i].style);
    id editor = _editors[i];
    if ([editor isKindOfClass:DDStyleEditor.class]) [(DDStyleEditor*)editor showStyle:ns(_rows[i].style) keepTyping:YES];
  }
  _filling = NO;
}

/// Makes a block's controls per row: a disclosure, the selector, a remove button, a summary of
/// its keys and, when open, the style editor.
- (void)rebuild {
  for (id editor in _editors)
    if ([editor isKindOfClass:DDStyleEditor.class]) [(DDStyleEditor*)editor endColorEditing];
  for (NSView* v in [_list.arrangedSubviews copy]) [v removeFromSuperview];
  [_pickers removeAllObjects];
  [_editors removeAllObjects];
  [_summaries removeAllObjects];
  [_open removeIndexesInRange:NSMakeRange(_rows.size(), NSNotFound - _rows.size())];
  for (std::size_t i = 0; i < _rows.size(); ++i) {
    NSInteger tag = (NSInteger)i;
    BOOL open = [_open containsIndex:i];
    NSButton* disclose = [[NSButton alloc] initWithFrame:NSZeroRect];
    disclose.bezelStyle = NSBezelStyleDisclosure;
    disclose.buttonType = NSButtonTypePushOnPushOff;
    disclose.title = @"";
    disclose.state = open ? NSControlStateValueOn : NSControlStateValueOff;
    disclose.tag = tag;
    disclose.target = self;
    disclose.action = @selector(toggled:);
    disclose.toolTip = @"Edit this block's keys";

    NSComboBox* picker = [[NSComboBox alloc] initWithFrame:NSZeroRect];
    picker.completes = YES;
    picker.numberOfVisibleItems = 16;
    picker.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    picker.placeholderString = @"container, tag \"external\", relationship";
    [picker addItemsWithObjectValues:_selectors];
    picker.stringValue = ns(_rows[i].selector);
    picker.tag = tag;
    picker.delegate = self;
    picker.target = self;
    picker.action = @selector(selectorTyped:);
    [picker setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSButton* remove = iconButton(@"minus.circle", @"Remove this style block", self, @selector(removeBlock:));
    remove.tag = tag;
    NSStackView* header = row(@[ disclose, picker, remove ]);
    [_list addArrangedSubview:header];
    [header.widthAnchor constraintEqualToAnchor:_list.widthAnchor].active = YES;

    NSTextField* summary = smallLabel(_rows[i].style.empty() ? @"no keys yet" : ns(_rows[i].style));
    summary.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    summary.lineBreakMode = NSLineBreakByTruncatingTail;
    summary.hidden = open;
    [_list addArrangedSubview:summary];
    [summary.widthAnchor constraintLessThanOrEqualToAnchor:_list.widthAnchor].active = YES;

    id editorSlot = NSNull.null;
    if (open) {
      DDStyleEditor* editor = [[DDStyleEditor alloc] initWithTarget:dd::host::selector_target(_rows[i].selector)];
      [editor showStyle:ns(_rows[i].style) keepTyping:NO];
      __weak DDStyleBlocks* weakSelf = self;
      __weak DDStyleEditor* weakEditor = editor;
      editor.onChange = ^{
        [weakSelf block:tag styled:weakEditor.style];
      };
      [_list addArrangedSubview:editor];
      [editor.widthAnchor constraintEqualToAnchor:_list.widthAnchor constant:-8].active = YES;
      [_list setCustomSpacing:12 afterView:editor];
      editorSlot = editor;
    }
    [_pickers addObject:picker];
    [_editors addObject:editorSlot];
    [_summaries addObject:summary];
  }
}

/// Writes the rows back as text and reports a change.
- (void)commitRows {
  std::string text = dd::host::block_text(_rows);
  if (text == _shown) return;
  _shown = text;
  if (self.onChange) self.onChange();
}

- (void)block:(NSInteger)index styled:(NSString*)style {
  if (_filling || index >= (NSInteger)_rows.size()) return;
  _rows[(std::size_t)index].style = str(style);
  _summaries[(NSUInteger)index].stringValue = style.length ? style : @"no keys yet";
  [self commitRows];
}

- (void)toggled:(NSButton*)sender {
  if (sender.state == NSControlStateValueOn)
    [_open addIndex:(NSUInteger)sender.tag];
  else
    [_open removeIndex:(NSUInteger)sender.tag];
  [self rebuild];
}

- (void)selectorTyped:(NSComboBox*)sender {
  if (_filling || sender.tag >= (NSInteger)_rows.size()) return;
  auto& r = _rows[(std::size_t)sender.tag];
  std::string typed = str([sender.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]);
  if (typed == r.selector) return;
  bool retarget = dd::host::selector_target(typed) != dd::host::selector_target(r.selector);
  r.selector = typed;
  // An editor for elements does not fit a relationship block, and the other way round.
  if (retarget && [_open containsIndex:(NSUInteger)sender.tag])
    dispatch_async(dispatch_get_main_queue(), ^{
      [self rebuild];
    });
  [self commitRows];
}

- (void)comboBoxSelectionDidChange:(NSNotification*)note {
  NSComboBox* picker = note.object;
  id picked = picker.objectValueOfSelectedItem;
  if (![picked isKindOfClass:NSString.class]) return;
  picker.stringValue = picked;
  [self selectorTyped:picker];
}

- (void)controlTextDidEndEditing:(NSNotification*)note {
  if ([note.object isKindOfClass:NSComboBox.class]) [self selectorTyped:note.object];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:) || control.tag >= (NSInteger)_rows.size()) return NO;
  control.stringValue = ns(_rows[(std::size_t)control.tag].selector);
  [self.window makeFirstResponder:nil];
  return YES;
}

- (void)addBlock:(id)sender {
  [self.window makeFirstResponder:nil];
  _rows.push_back({"", ""});
  [_open addIndex:_rows.size() - 1];
  [self rebuild];
  [self.window makeFirstResponder:_pickers.lastObject];
}

- (void)removeBlock:(NSButton*)sender {
  if (sender.tag >= (NSInteger)_rows.size()) return;
  [self.window makeFirstResponder:nil];
  _rows.erase(_rows.begin() + sender.tag);
  [_open shiftIndexesStartingAtIndex:(NSUInteger)sender.tag + 1 by:-1];
  dispatch_async(dispatch_get_main_queue(), ^{
    [self rebuild];
  });
  [self commitRows];
}

@end

// ── DDThemeKeys ──

@interface DDThemeKeys () <NSTextFieldDelegate>
@end

@implementation DDThemeKeys {
  std::vector<std::pair<std::string, std::string>> _rows;
  std::string _shown;
  /// One control per key of dd::theme_keys(): a DDColorField or an NSTextField.
  NSMutableArray<NSView*>* _controls;
  BOOL _filling;
}

@synthesize onChange = _onChange;

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  _controls = [NSMutableArray array];
  NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
  grid.rowSpacing = 4;
  grid.columnSpacing = 8;
  [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  const auto& keys = dd::theme_keys();
  for (std::size_t i = 0; i < keys.size(); ++i) {
    NSInteger tag = (NSInteger)i;
    NSView* control;
    if (dd::host::is_color_key(keys[i])) {
      DDColorField* field = [[DDColorField alloc] initCompact:NO];
      __weak DDThemeKeys* weakSelf = self;
      __weak DDColorField* weakField = field;
      field.onChange = ^{
        [weakSelf key:tag set:weakField.value];
      };
      control = field;
    } else {
      NSTextField* field = [NSTextField textFieldWithString:@""];
      field.font = [NSFont systemFontOfSize:12];
      field.placeholderString = @"the base theme's";
      field.tag = tag;
      field.delegate = self;
      field.target = self;
      field.action = @selector(typed:);
      [field.widthAnchor constraintGreaterThanOrEqualToConstant:110].active = YES;
      control = field;
    }
    control.toolTip = ns(keys[i]);
    [grid addRowWithViews:@[ smallLabel(ns(keys[i])), control ]];
    [_controls addObject:control];
  }
  fill(self, grid);
  return self;
}

- (NSString*)text {
  return ns(_shown);
}

- (BOOL)editing {
  return typingIn(self);
}

- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping {
  _shown = str(text);
  _rows = dd::host::property_rows(_shown);
  _filling = YES;
  const auto& keys = dd::theme_keys();
  for (std::size_t i = 0; i < keys.size(); ++i) {
    std::string value;
    for (const auto& [k, v] : _rows)
      if (k == keys[i]) value = v;
    NSView* control = _controls[i];
    if ([control isKindOfClass:DDColorField.class]) {
      ((DDColorField*)control).value = ns(value);
    } else if (!(keepTyping && typingIn(control))) {
      ((NSTextField*)control).stringValue = ns(value);
    }
  }
  _filling = NO;
}

- (void)key:(NSInteger)index set:(NSString*)value {
  if (_filling) return;
  const std::string& key = dd::theme_keys()[(std::size_t)index];
  std::string v = str([value stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]);
  auto at = std::find_if(_rows.begin(), _rows.end(), [&](const auto& r) { return r.first == key; });
  if (v.empty()) {
    if (at == _rows.end()) return;
    _rows.erase(at);
  } else if (at != _rows.end()) {
    at->second = v;
  } else {
    _rows.emplace_back(key, v);
  }
  std::string text = dd::host::property_text(_rows);
  if (text == _shown) return;
  _shown = text;
  if (self.onChange) self.onChange();
}

- (void)typed:(NSTextField*)sender {
  [self key:sender.tag set:sender.stringValue];
}

- (void)controlTextDidEndEditing:(NSNotification*)note {
  [self typed:note.object];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:)) return NO;
  [self.window makeFirstResponder:nil];
  [self showText:ns(_shown) keepTyping:NO];
  return YES;
}

- (void)endColorEditing {
  for (NSView* c in _controls)
    if ([c isKindOfClass:DDColorField.class]) [(DDColorField*)c endColorEditing];
}

@end

// ── DDThemeNodes ──

@interface DDThemeNodes () <NSComboBoxDelegate>
@end

@implementation DDThemeNodes {
  std::vector<dd::host::BlockRow> _rows;
  std::string _shown;
  NSStackView* _list;
  NSComboBox* _newKind;
  /// Per row, its fill, line and text fields.
  NSMutableArray<NSArray<DDColorField*>*>* _fields;
  BOOL _filling;
}

@synthesize onChange = _onChange;

static const char* const kNodeKeys[] = {"fill", "stroke", "text"};

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  _fields = [NSMutableArray array];
  NSStackView* stack = column();
  _list = column();
  _list.spacing = 6;
  _newKind = [[NSComboBox alloc] initWithFrame:NSZeroRect];
  _newKind.placeholderString = @"kind";
  _newKind.font = [NSFont systemFontOfSize:12];
  _newKind.completes = YES;
  _newKind.target = self;
  _newKind.action = @selector(addKind:);
  NSButton* add = [NSButton buttonWithTitle:@"Add" target:self action:@selector(addKind:)];
  add.controlSize = NSControlSizeSmall;
  add.font = [NSFont systemFontOfSize:11];
  [stack addArrangedSubview:_list];
  [stack addArrangedSubview:row(@[ _newKind, add ])];
  [_list.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
  fill(self, stack);
  return self;
}

- (NSString*)text {
  return ns(_shown);
}

- (BOOL)editing {
  return typingIn(self);
}

- (void)setKinds:(NSArray<NSString*>*)kinds {
  if (typingIn(_newKind)) return;
  [_newKind removeAllItems];
  [_newKind addItemsWithObjectValues:kinds];
}

- (void)showText:(NSString*)text keepTyping:(BOOL)keepTyping {
  if (keepTyping && self.editing) return;
  _shown = str(text);
  auto rows = dd::host::block_rows(_shown);
  bool sameShape = rows.size() == _rows.size() && _fields.count == rows.size();
  for (std::size_t i = 0; sameShape && i < rows.size(); ++i) sameShape = rows[i].selector == _rows[i].selector;
  _rows = std::move(rows);
  if (!sameShape) [self rebuild];
  _filling = YES;
  for (std::size_t i = 0; i < _rows.size(); ++i)
    for (std::size_t k = 0; k < 3; ++k)
      _fields[i][k].value = ns(dd::host::style_value(_rows[i].style, kNodeKeys[k]).value_or(""));
  _filling = NO;
}

- (void)rebuild {
  for (NSArray<DDColorField*>* fields in _fields)
    for (DDColorField* f in fields) [f endColorEditing];
  for (NSView* v in [_list.arrangedSubviews copy]) [v removeFromSuperview];
  [_fields removeAllObjects];
  for (std::size_t i = 0; i < _rows.size(); ++i) {
    NSInteger tag = (NSInteger)i;
    NSTextField* kind = [NSTextField labelWithString:ns(_rows[i].selector)];
    kind.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
    NSButton* remove = iconButton(@"minus.circle", @"Remove this kind's colours", self, @selector(removeKind:));
    remove.tag = tag;
    NSStackView* header = row(@[ kind, remove ]);
    NSMutableArray<NSView*>* wells = [NSMutableArray array];
    NSMutableArray<DDColorField*>* fields = [NSMutableArray array];
    for (std::size_t k = 0; k < 3; ++k) {
      DDColorField* field = [[DDColorField alloc] initCompact:YES];
      __weak DDThemeNodes* weakSelf = self;
      __weak DDColorField* weakField = field;
      std::size_t key = k;
      field.onChange = ^{
        [weakSelf node:tag key:key set:weakField.value];
      };
      [wells addObject:smallLabel(k == 0 ? @"Fill" : k == 1 ? @"Line" : @"Text")];
      [wells addObject:field];
      [fields addObject:field];
    }
    [_list addArrangedSubview:header];
    [_list addArrangedSubview:row(wells)];
    [_fields addObject:fields];
  }
}

- (void)commitRows {
  std::string text = dd::host::block_text(_rows);
  if (text == _shown) return;
  _shown = text;
  if (self.onChange) self.onChange();
}

- (void)node:(NSInteger)index key:(std::size_t)key set:(NSString*)value {
  if (_filling || index >= (NSInteger)_rows.size()) return;
  auto& r = _rows[(std::size_t)index];
  std::optional<std::string> v;
  if (value.length) v = str(value);
  r.style = dd::host::with_style_value(r.style, kNodeKeys[key], v);
  [self commitRows];
}

- (void)addKind:(id)sender {
  std::string kind = str([_newKind.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]);
  if (kind.empty()) return;
  _newKind.stringValue = @"";
  if (std::any_of(_rows.begin(), _rows.end(), [&](const auto& r) { return r.selector == kind; })) return;
  _rows.push_back({kind, ""});
  [self rebuild];
  // A kind without colours writes nothing yet: it is kept as a row until a colour is picked.
}

- (void)removeKind:(NSButton*)sender {
  if (sender.tag >= (NSInteger)_rows.size()) return;
  _rows.erase(_rows.begin() + sender.tag);
  dispatch_async(dispatch_get_main_queue(), ^{
    [self rebuild];
    [self showText:ns(self->_shown) keepTyping:NO];
  });
  [self commitRows];
}

@end
