#import "DDStylePanel.h"

#import "DDThemeEditors.h"

#include <string>
#include <vector>

namespace {

using Fields = dd::host::DocumentFields;

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string str(NSString* s) { return s.UTF8String ?: ""; }

enum class Control {
  Theme,   ///< a combo box of the themes
  Blocks,  ///< DDStyleBlocks
  Keys,    ///< DDThemeKeys
  Nodes,   ///< DDThemeNodes
};

struct Field {
  NSString* key;
  NSString* label;
  NSString* placeholder;
  std::string Fields::*member;
  Control control;
  bool declared;  ///< in the declared theme's section
};

const std::vector<Field>& fields() {
  static const std::vector<Field> all{
      {@"theme", @"Theme", @"a built-in theme such as dark or ocean, or a new name", &Fields::theme, Control::Theme, false},
      {@"view-theme", @"This view's theme", @"the document's", &Fields::view_theme, Control::Theme, false},
      {@"styles", @"Style blocks", nil, &Fields::styles, Control::Blocks, false},
      {@"extends", @"Extends", @"default", &Fields::extends, Control::Theme, true},
      {@"theme-keys", @"Theme keys", nil, &Fields::theme_keys, Control::Keys, true},
      {@"theme-nodes", @"Node colours", nil, &Fields::theme_nodes, Control::Nodes, true},
  };
  return all;
}

BOOL typing(NSView* view) {
  NSResponder* focus = view.window.firstResponder;
  return [focus isKindOfClass:NSTextView.class] && [(NSView*)focus isDescendantOf:view];
}

}  // namespace

@interface DDStylePanel () <NSComboBoxDelegate, NSTextFieldDelegate>
@end

@implementation DDStylePanel {
  NSTextField* _themeTitle;
  /// One control per field, in the order of fields(): an NSComboBox or an editor of DDThemeEditors.h.
  NSMutableArray<NSView*>* _inputs;
  NSMutableArray<NSTextField*>* _labels;
  std::optional<Fields> _shown;
  BOOL _filling;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  _inputs = [NSMutableArray array];
  _labels = [NSMutableArray array];
  NSStackView* stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 4;
  stack.edgeInsets = NSEdgeInsetsMake(10, 10, 10, 10);
  stack.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField* title = [NSTextField labelWithString:@"Theme and styles"];
  title.font = [NSFont boldSystemFontOfSize:12];
  [stack addArrangedSubview:title];
  [stack setCustomSpacing:8 afterView:title];

  __weak DDStylePanel* weakSelf = self;
  for (const auto& f : fields()) {
    if (f.declared && !_themeTitle) {
      _themeTitle = [NSTextField labelWithString:@""];
      _themeTitle.font = [NSFont boldSystemFontOfSize:12];
      [stack addArrangedSubview:_themeTitle];
      [stack setCustomSpacing:8 afterView:_themeTitle];
    }
    NSTextField* label = [NSTextField labelWithString:f.label];
    label.font = [NSFont systemFontOfSize:11];
    label.textColor = NSColor.secondaryLabelColor;
    NSView* input;
    switch (f.control) {
      case Control::Theme: {
        NSComboBox* combo = [[NSComboBox alloc] initWithFrame:NSZeroRect];
        combo.placeholderString = f.placeholder;
        combo.font = [NSFont systemFontOfSize:12];
        combo.completes = YES;
        combo.numberOfVisibleItems = 16;
        combo.delegate = self;
        combo.target = self;
        combo.action = @selector(commit:);
        input = combo;
        break;
      }
      case Control::Blocks:
        input = [[DDStyleBlocks alloc] initWithFrame:NSZeroRect];
        break;
      case Control::Keys:
        input = [[DDThemeKeys alloc] initWithFrame:NSZeroRect];
        break;
      case Control::Nodes:
        input = [[DDThemeNodes alloc] initWithFrame:NSZeroRect];
        break;
    }
    if ([input conformsToProtocol:@protocol(DDTextEditor)])
      ((id<DDTextEditor>)input).onChange = ^{
        [weakSelf commit:nil];
      };
    input.identifier = f.key;
    [stack addArrangedSubview:label];
    [stack addArrangedSubview:input];
    [stack setCustomSpacing:10 afterView:input];
    [input.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-20].active = YES;
    [_labels addObject:label];
    [_inputs addObject:input];
  }

  [self addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.topAnchor constraintEqualToAnchor:self.topAnchor],
    [stack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
    [stack.bottomAnchor constraintLessThanOrEqualToAnchor:self.bottomAnchor],
  ]];
  self.hidden = YES;
  return self;
}

- (void)setChoices:(const dd::host::FieldChoices&)choices {
  NSMutableArray<NSString*>* themes = [NSMutableArray array];
  for (const auto& c : choices.themes) [themes addObject:ns(c.value)];
  NSMutableArray<NSString*>* selectors = [NSMutableArray array];
  for (const auto& s : dd::host::selector_choices(choices)) [selectors addObject:ns(s)];
  NSMutableArray<NSString*>* kinds = [NSMutableArray array];
  for (const auto& c : choices.kinds) [kinds addObject:ns(c.value)];
  _filling = YES;
  for (NSView* input in _inputs) {
    if ([input isKindOfClass:NSComboBox.class]) {
      NSComboBox* combo = (NSComboBox*)input;
      if (typing(combo) || [combo.objectValues isEqualToArray:themes]) continue;
      NSString* value = combo.stringValue;
      [combo removeAllItems];
      [combo addItemsWithObjectValues:themes];
      combo.stringValue = value;
    } else if ([input isKindOfClass:DDStyleBlocks.class]) {
      [(DDStyleBlocks*)input setSelectors:selectors];
    } else if ([input isKindOfClass:DDThemeNodes.class]) {
      [(DDThemeNodes*)input setKinds:kinds];
    }
  }
  _filling = NO;
}

- (void)showFields:(const std::optional<Fields>&)shown {
  bool sameTheme = shown && _shown && shown->declared == _shown->declared;
  _shown = shown;
  self.hidden = !shown;
  if (!shown) {
    for (NSView* input in _inputs)
      if ([input respondsToSelector:@selector(endColorEditing)]) [(id)input endColorEditing];
    return;
  }
  bool declared = !shown->declared.empty();
  _themeTitle.stringValue = ns("Theme " + shown->declared);
  _themeTitle.hidden = !declared;
  _filling = YES;
  for (std::size_t i = 0; i < fields().size(); ++i) {
    NSView* input = _inputs[i];
    if (fields()[i].declared) {
      input.hidden = !declared;
      _labels[i].hidden = !declared;
    }
    // What is being typed stays, unless it belongs to another theme.
    bool keep = !fields()[i].declared || sameTheme;
    NSString* text = ns((*shown).*(fields()[i].member));
    if ([input conformsToProtocol:@protocol(DDTextEditor)]) {
      [(id<DDTextEditor>)input showText:text keepTyping:keep];
    } else if (!(keep && typing(input))) {
      ((NSComboBox*)input).stringValue = text;
    }
  }
  _filling = NO;
}

- (NSString*)valueOf:(NSView*)input {
  if ([input conformsToProtocol:@protocol(DDTextEditor)]) return ((id<DDTextEditor>)input).text;
  return ((NSComboBox*)input).stringValue;
}

- (void)commit:(id)sender {
  if (_filling || !_shown || !self.onCommit) return;
  Fields typed = *_shown;
  for (std::size_t i = 0; i < fields().size(); ++i) typed.*(fields()[i].member) = str([self valueOf:_inputs[i]]);
  if (!(typed == *_shown)) self.onCommit(typed);
}

/// Picking a theme from a combo box's list applies it at once.
- (void)comboBoxSelectionDidChange:(NSNotification*)note {
  if (_filling) return;
  NSComboBox* combo = note.object;
  id picked = combo.objectValueOfSelectedItem;
  if (![picked isKindOfClass:NSString.class]) return;
  combo.stringValue = picked;
  [self commit:combo];
}

/// Leaving a field after typing commits it, as Return does; Escape puts it back.
- (void)controlTextDidEndEditing:(NSNotification*)note {
  [self commit:note.object];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command == @selector(cancelOperation:) && _shown) {
    for (std::size_t i = 0; i < fields().size(); ++i)
      if (_inputs[i] == control) control.stringValue = ns((*_shown).*(fields()[i].member));
    [self.window makeFirstResponder:nil];
    return YES;
  }
  return NO;
}

- (BOOL)setField:(NSString*)key to:(NSString*)value {
  if (self.hidden || !_shown) return NO;
  for (NSView* input in _inputs) {
    if (![input.identifier isEqualToString:key]) continue;
    if (input.hidden) return NO;
    if ([input conformsToProtocol:@protocol(DDTextEditor)])
      [(id<DDTextEditor>)input showText:value keepTyping:NO];
    else
      ((NSComboBox*)input).stringValue = value;
    [self commit:input];
    return YES;
  }
  return NO;
}

@end
