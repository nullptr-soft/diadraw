#import "DDStyleEditor.h"

#import "DDDrawing.h"
#import "DDThemeEditors.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "host/field_editors.hpp"

using dd::host::StyleControl;
using dd::host::StyleKey;

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string str(NSString* s) { return s.UTF8String ?: ""; }

NSColor* colorOf(const std::string& hex) {
  auto c = dd::host::normalized_color(hex);
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

/// A number as a style value: no trailing zeros (`2`, `1.5`).
std::string numberText(double v) {
  char out[32];
  std::snprintf(out, sizeof out, "%g", v);
  return out;
}

/// A preview of a line for a pop-up entry, drawn by the core's renderer in the menu's colours
/// (resolved when it is drawn, so it follows light and dark mode). It is rendered again only when
/// those colours change, not each time the menu draws it.
NSImage* sampleImage(const dd::Style& sample) {
  const CGFloat width = 48, height = 14;
  // The block's captured copies are const, so the preview is reached through a pointer. AppKit
  // may draw an image on any thread, so the preview is locked while it is used.
  struct Shared {
    explicit Shared(const dd::Style& style) : preview(style) {}
    std::mutex lock;
    dd::host::LinePreview preview;
  };
  auto shared = std::make_shared<Shared>(sample);
  return [NSImage imageWithSize:NSMakeSize(width, height)
                        flipped:YES
                 drawingHandler:^BOOL(NSRect) {
                   std::string ink = hexOf(NSColor.labelColor);
                   std::string surface = hexOf(NSColor.controlBackgroundColor);
                   std::scoped_lock hold(shared->lock);
                   const auto& list = shared->preview.list(ink, surface, width, height);
                   if (!list) return NO;
                   dd::mac::paint(NSGraphicsContext.currentContext.CGContext, *list);
                   return YES;
                 }];
}

NSTextField* smallLabel(NSString* text) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:11];
  label.textColor = NSColor.secondaryLabelColor;
  return label;
}

}  // namespace

@interface DDStyleEditor () <NSTextFieldDelegate>
@end

@implementation DDStyleEditor {
  dd::StyleTarget _target;
  std::string _style;
  /// One control per key of style_keys(_target), in that order: a DDColorField, an NSTextField
  /// (numbers; its stepper is in _steppers), an NSButton check box, an NSPopUpButton or an
  /// NSSegmentedControl.
  NSMutableArray<NSView*>* _controls;
  NSMutableDictionary<NSNumber*, NSStepper*>* _steppers;
  NSTextField* _other;
  NSButton* _reset;
  BOOL _filling;
}

- (const std::vector<StyleKey>&)keys {
  return dd::host::style_keys(_target);
}

- (instancetype)initWithTarget:(dd::StyleTarget)target {
  if (!(self = [super initWithFrame:NSZeroRect])) return nil;
  _target = target;
  _controls = [NSMutableArray array];
  _steppers = [NSMutableDictionary dictionary];
  self.translatesAutoresizingMaskIntoConstraints = NO;

  NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
  grid.translatesAutoresizingMaskIntoConstraints = NO;
  grid.rowSpacing = 5;
  grid.columnSpacing = 8;
  [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;

  std::string section;
  const auto& keys = [self keys];
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const StyleKey& k = keys[i];
    NSInteger tag = (NSInteger)i;
    if (k.section != section) {
      section = k.section;
      NSTextField* header = [NSTextField labelWithString:ns(section)];
      header.font = [NSFont boldSystemFontOfSize:11];
      NSGridRow* row = [grid addRowWithViews:@[ header, [NSGridCell emptyContentView] ]];
      [row mergeCellsInRange:NSMakeRange(0, 2)];
      [row cellAtIndex:0].xPlacement = NSGridCellPlacementLeading;
      if (grid.numberOfRows > 1) row.topPadding = 6;
    }
    NSView* label = smallLabel(ns(k.label));
    NSView* cell;
    NSView* control;
    switch (k.control) {
      case StyleControl::Color: {
        DDColorField* field = [[DDColorField alloc] initCompact:NO];
        __weak DDStyleEditor* weakSelf = self;
        __weak DDColorField* weakField = field;
        field.onChange = ^{
          [weakSelf set:tag to:str(weakField.value)];
        };
        cell = control = field;
        break;
      }
      case StyleControl::Number: {
        NSTextField* field = [NSTextField textFieldWithString:@""];
        field.placeholderString = ns(k.fallback);
        field.font = [NSFont systemFontOfSize:12];
        field.alignment = NSTextAlignmentRight;
        field.tag = tag;
        field.delegate = self;
        field.target = self;
        field.action = @selector(numberTyped:);
        [field.widthAnchor constraintEqualToConstant:52].active = YES;
        NSStepper* stepper = [[NSStepper alloc] initWithFrame:NSZeroRect];
        stepper.minValue = k.min;
        stepper.maxValue = k.max;
        stepper.increment = k.step;
        stepper.valueWraps = NO;
        stepper.tag = tag;
        stepper.target = self;
        stepper.action = @selector(stepped:);
        _steppers[@(tag)] = stepper;
        NSMutableArray<NSView*>* views = [NSMutableArray arrayWithObjects:field, stepper, nil];
        if (k.key == "opacity") [views addObject:smallLabel(@"%")];
        cell = [NSStackView stackViewWithViews:views];
        ((NSStackView*)cell).spacing = 2;
        control = field;
        break;
      }
      case StyleControl::Flag: {
        NSButton* box = [NSButton checkboxWithTitle:ns(k.label) target:self action:@selector(flagged:)];
        box.font = [NSFont systemFontOfSize:12];
        box.tag = tag;
        label = [NSGridCell emptyContentView];
        cell = control = box;
        break;
      }
      case StyleControl::Choice: {
        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        popup.controlSize = NSControlSizeSmall;
        popup.font = [NSFont systemFontOfSize:11];
        // Each entry's tag is its index in k.choices; section headers and separators have none.
        std::string group;
        for (std::size_t c = 0; c < k.choices.size(); ++c) {
          const auto& choice = k.choices[c];
          if (choice.group != group) {
            group = choice.group;
            if (!group.empty()) {
              NSMenuItem* separator = NSMenuItem.separatorItem;
              separator.tag = -1;
              [popup.menu addItem:separator];
              NSMenuItem* header = [[NSMenuItem alloc] initWithTitle:ns(group) action:nil keyEquivalent:@""];
              header.enabled = NO;
              header.tag = -1;
              [popup.menu addItem:header];
            }
          }
          NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:ns(choice.label) action:nil keyEquivalent:@""];
          item.tag = (NSInteger)c;
          if (auto sample = dd::host::style_sample(k, choice.value)) item.image = sampleImage(*sample);
          [popup.menu addItem:item];
        }
        popup.autoenablesItems = NO;
        popup.tag = tag;
        popup.target = self;
        popup.action = @selector(chosen:);
        cell = control = popup;
        break;
      }
      case StyleControl::FontStyle: {
        NSArray<NSImage*>* images = @[
          [NSImage imageWithSystemSymbolName:@"bold" accessibilityDescription:@"Bold"],
          [NSImage imageWithSystemSymbolName:@"italic" accessibilityDescription:@"Italic"],
          [NSImage imageWithSystemSymbolName:@"underline" accessibilityDescription:@"Underline"],
        ];
        NSSegmentedControl* segments = [NSSegmentedControl segmentedControlWithImages:images
                                                                         trackingMode:NSSegmentSwitchTrackingSelectAny
                                                                               target:self
                                                                               action:@selector(fontStyled:)];
        segments.controlSize = NSControlSizeSmall;
        segments.tag = tag;
        cell = control = segments;
        break;
      }
    }
    control.toolTip = ns(k.key);
    [grid addRowWithViews:@[ label, cell ]];
    [_controls addObject:control];
  }

  NSTextField* otherLabel = smallLabel(@"Other");
  _other = [NSTextField textFieldWithString:@""];
  _other.placeholderString = @"more keys, e.g. shadow=1";
  _other.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
  _other.toolTip = @"Style keys without a control above, as key=value;…";
  _other.delegate = self;
  _other.target = self;
  _other.action = @selector(otherTyped:);
  NSGridRow* otherRow = [grid addRowWithViews:@[ otherLabel, _other ]];
  otherRow.topPadding = 6;

  _reset = [NSButton buttonWithTitle:@"Clear style" target:self action:@selector(clearStyle:)];
  _reset.controlSize = NSControlSizeSmall;
  _reset.font = [NSFont systemFontOfSize:11];
  [grid addRowWithViews:@[ [NSGridCell emptyContentView], _reset ]];

  [self addSubview:grid];
  [NSLayoutConstraint activateConstraints:@[
    [grid.topAnchor constraintEqualToAnchor:self.topAnchor],
    [grid.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [grid.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor],
    [grid.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
    [_other.widthAnchor constraintGreaterThanOrEqualToConstant:120],
    [_other.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
  ]];
  return self;
}

- (NSString*)style {
  return ns(_style);
}

- (BOOL)editing {
  NSResponder* focus = self.window.firstResponder;
  if (![focus isKindOfClass:NSTextView.class]) return NO;
  NSView* editor = (NSView*)focus;
  return [editor isDescendantOf:self];
}

// ── Showing ──

- (void)showStyle:(NSString*)style keepTyping:(BOOL)keepTyping {
  _style = str(style);
  _filling = YES;
  NSResponder* focus = self.window.firstResponder;
  auto typing = [&](NSControl* c) { return keepTyping && c.currentEditor && focus == c.currentEditor; };
  const auto& keys = [self keys];
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const StyleKey& k = keys[i];
    auto value = dd::host::style_control_value(_style, k);
    NSView* control = _controls[i];
    switch (k.control) {
      case StyleControl::Color: {
        // A value the field cannot show as a colour (`none`, a name) is still shown as text.
        ((DDColorField*)control).value = value ? ns(*value) : @"";
        break;
      }
      case StyleControl::Number: {
        NSTextField* field = (NSTextField*)control;
        NSStepper* stepper = _steppers[@(i)];
        stepper.doubleValue = std::atof((value ? *value : k.fallback).c_str());
        if (!typing(field)) field.stringValue = value ? ns(*value) : @"";
        break;
      }
      case StyleControl::Flag:
        ((NSButton*)control).state =
            (value ? *value : k.fallback) == "1" ? NSControlStateValueOn : NSControlStateValueOff;
        break;
      case StyleControl::Choice: {
        NSPopUpButton* popup = (NSPopUpButton*)control;
        // A value without an entry (`endArrow=sysMLs`) gets one of its own after the others,
        // tagged past the end of k.choices.
        NSInteger extra = (NSInteger)k.choices.size();
        if (NSMenuItem* old = [popup.menu itemWithTag:extra]) [popup.menu removeItem:old];
        NSInteger at = 0;
        if (value) {
          at = -1;
          for (std::size_t c = 0; c < k.choices.size(); ++c)
            if (k.choices[c].value == *value) at = (NSInteger)c;
          if (at < 0) {
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:ns(*value) action:nil keyEquivalent:@""];
            item.tag = extra;
            if (auto sample = dd::host::style_sample(k, *value)) item.image = sampleImage(*sample);
            [popup.menu addItem:item];
            at = extra;
          }
        }
        [popup selectItemWithTag:at];
        break;
      }
      case StyleControl::FontStyle: {
        NSSegmentedControl* segments = (NSSegmentedControl*)control;
        int bits = dd::host::font_style_bits(value ? *value : k.fallback);
        for (NSInteger s = 0; s < 3; ++s) [segments setSelected:(bits & (1 << s)) != 0 forSegment:s];
        break;
      }
    }
  }
  if (!typing(_other)) _other.stringValue = ns(dd::host::other_style(_style, _target));
  _reset.enabled = !_style.empty();
  _filling = NO;
}

- (void)endColorEditing {
  for (NSView* control in _controls)
    if ([control isKindOfClass:DDColorField.class]) [(DDColorField*)control endColorEditing];
}

// ── Editing ──

/// Sets key `index` as its control does and reports the change.
- (void)set:(NSInteger)index to:(const std::string&)value {
  if (_filling) return;
  const StyleKey& k = [self keys][(std::size_t)index];
  std::string next = dd::host::with_style_setting(_style, k, value);
  if (next == _style) return;
  [self showStyle:ns(next) keepTyping:NO];
  if (self.onChange) self.onChange();
}

- (void)numberTyped:(NSTextField*)sender {
  NSString* typed = [sender.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  if (typed.length == 0) {
    [self set:sender.tag to:""];
    return;
  }
  const StyleKey& k = [self keys][(std::size_t)sender.tag];
  double v = std::clamp(typed.doubleValue, k.min, k.max);
  [self set:sender.tag to:numberText(v)];
}

- (void)stepped:(NSStepper*)sender {
  [self set:sender.tag to:numberText(sender.doubleValue)];
}

- (void)flagged:(NSButton*)sender {
  [self set:sender.tag to:sender.state == NSControlStateValueOn ? "1" : "0"];
}

- (void)chosen:(NSPopUpButton*)sender {
  const StyleKey& k = [self keys][(std::size_t)sender.tag];
  NSInteger at = sender.selectedItem ? sender.selectedItem.tag : -1;
  if (at < 0 || at >= (NSInteger)k.choices.size()) return;  // the value it already had
  [self set:sender.tag to:k.choices[(std::size_t)at].value];
}

- (void)fontStyled:(NSSegmentedControl*)sender {
  int bits = 0;
  for (NSInteger s = 0; s < 3; ++s)
    if ([sender isSelectedForSegment:s]) bits |= 1 << s;
  [self set:sender.tag to:std::to_string(bits)];
}

- (void)otherTyped:(NSTextField*)sender {
  if (_filling) return;
  std::string next = dd::host::with_other_style(_style, _target, str(sender.stringValue));
  if (next == _style) return;
  [self showStyle:ns(next) keepTyping:NO];
  if (self.onChange) self.onChange();
}

- (void)clearStyle:(id)sender {
  if (_style.empty()) return;
  [self endColorEditing];
  [self showStyle:@"" keepTyping:NO];
  if (self.onChange) self.onChange();
}

/// Leaving a text field after typing applies it, as Return does; Escape puts it back.
- (void)controlTextDidEndEditing:(NSNotification*)note {
  NSTextField* field = note.object;
  if (field == _other)
    [self otherTyped:field];
  else
    [self numberTyped:field];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:)) return NO;
  [self showStyle:ns(_style) keepTyping:NO];
  [self.window makeFirstResponder:nil];
  return YES;
}

- (BOOL)setKey:(NSString*)key to:(NSString*)value {
  if ([key isEqualToString:@"other"]) {
    _other.stringValue = value;
    [self otherTyped:_other];
    return YES;
  }
  const auto& keys = [self keys];
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i].key != str(key)) continue;
    std::string v = str(value);
    if (keys[i].control == StyleControl::Color && !v.empty()) {
      // As the colour field would write it.
      NSColor* color = colorOf(v);
      if (!color) return NO;
      v = hexOf(color);
    }
    [self set:(NSInteger)i to:v.empty() ? (keys[i].control == StyleControl::Flag ? keys[i].fallback : "") : v];
    return YES;
  }
  return NO;
}

@end
