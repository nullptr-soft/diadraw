#import "DDInspector.h"

#import "DDPropertiesTable.h"
#import "DDStyleEditor.h"

#include <algorithm>
#include <string>
#include <vector>

#include "host/field_editors.hpp"

using dd::host::ElementFields;
using dd::host::RelationshipFields;

@interface DDInspector () <NSTextFieldDelegate, NSComboBoxDelegate, NSTokenFieldDelegate>
- (void)commit:(id)sender;
@end

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string str(NSString* s) { return s.UTF8String ?: ""; }

/// How a field is edited.
enum class Control {
  Text,
  Multiline,
  Arrow,  ///< a pop-up of the three arrows
  Kind,   ///< a combo box of the kinds
  Shape,  ///< a combo box of the shapes
  Tech,   ///< a combo box of the technologies the document uses
  Tags,   ///< a token field
  Style,  ///< a DDStyleEditor
  Props,  ///< a DDPropertiesTable
  Area,   ///< a DDTextArea: Return starts a new line
};

template <class Fields>
struct Field {
  NSString* key;
  NSString* label;
  NSString* placeholder;
  std::string Fields::*member;
  Control control = Control::Text;
};

const std::vector<Field<ElementFields>>& element_fields() {
  static const std::vector<Field<ElementFields>> all{
      {@"id", @"Id", @"", &ElementFields::local_id},
      {@"kind", @"Kind", @"container", &ElementFields::kind, Control::Kind},
      {@"name", @"Name", @"", &ElementFields::name},
      {@"technology", @"Tech", @"", &ElementFields::technology, Control::Tech},
      {@"shape", @"Shape", @"from the kind", &ElementFields::shape, Control::Shape},
      {@"tags", @"Tags", @"external, legacy", &ElementFields::tags, Control::Tags},
      {@"description", @"Description", @"", &ElementFields::description, Control::Area},
      {@"style", @"Style", @"", &ElementFields::style, Control::Style},
      {@"props", @"Properties", @"", &ElementFields::props, Control::Props},
      {@"shared-styles", @"Shared styles", @"container: fill=#438dd5 (one block per line)",
       &ElementFields::shared_styles, Control::Multiline},
  };
  return all;
}

const std::vector<Field<RelationshipFields>>& relationship_fields() {
  static const std::vector<Field<RelationshipFields>> all{
      {@"label", @"Label", @"", &RelationshipFields::label},
      {@"technology", @"Tech", @"", &RelationshipFields::technology, Control::Tech},
      {@"arrow", @"Arrow", @"", &RelationshipFields::arrow, Control::Arrow},
      {@"name", @"Name (optional id)", @"derived from the ends", &RelationshipFields::name},
      {@"tags", @"Tags", @"async, legacy", &RelationshipFields::tags, Control::Tags},
      {@"style", @"Style", @"", &RelationshipFields::style, Control::Style},
      {@"props", @"Properties", @"", &RelationshipFields::props, Control::Props},
  };
  return all;
}

NSArray<NSString*>* arrows() { return @[ @"->", @"-->", @"<->" ]; }
/// What the arrow pop-up shows, in the order of arrows(): the arrow and what it draws.
NSArray<NSString*>* arrow_titles() { return @[ @"->   solid", @"-->  dashed", @"<->  both ends" ]; }

NSStackView* make_stack() {
  NSStackView* stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 4;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  return stack;
}

NSString* valueOf(NSView* view) {
  if ([view isKindOfClass:DDStyleEditor.class]) return ((DDStyleEditor*)view).style;
  if ([view isKindOfClass:DDPropertiesTable.class]) return ((DDPropertiesTable*)view).text;
  if ([view isKindOfClass:DDTextArea.class]) return ((DDTextArea*)view).text;
  if ([view isKindOfClass:NSPopUpButton.class]) {
    NSInteger at = ((NSPopUpButton*)view).indexOfSelectedItem;
    return at >= 0 && at < (NSInteger)arrows().count ? arrows()[at] : @"";
  }
  if ([view isKindOfClass:NSTokenField.class]) {
    std::vector<std::string> tags;
    id value = ((NSTokenField*)view).objectValue;
    if ([value isKindOfClass:NSArray.class]) {
      for (id token in (NSArray*)value)
        if ([token isKindOfClass:NSString.class])
          for (auto& tag : dd::host::split_tags(str(token))) tags.push_back(tag);  // `a, b` typed as one
    } else if ([value isKindOfClass:NSString.class]) {
      tags = dd::host::split_tags(str(value));
    }
    return ns(dd::host::join_tags(tags));
  }
  return ((NSControl*)view).stringValue;
}

void setValue(NSView* view, NSString* value, bool keepTyping) {
  if ([view isKindOfClass:DDStyleEditor.class]) {
    [(DDStyleEditor*)view showStyle:value keepTyping:keepTyping];
  } else if ([view isKindOfClass:DDPropertiesTable.class]) {
    [(DDPropertiesTable*)view showText:value keepTyping:keepTyping];
  } else if ([view isKindOfClass:DDTextArea.class]) {
    ((DDTextArea*)view).text = value;
  } else if ([view isKindOfClass:NSPopUpButton.class]) {
    NSUInteger at = [arrows() indexOfObject:value];
    [(NSPopUpButton*)view selectItemAtIndex:at == NSNotFound ? -1 : (NSInteger)at];
  } else if ([view isKindOfClass:NSTokenField.class]) {
    NSMutableArray<NSString*>* tokens = [NSMutableArray array];
    for (const auto& tag : dd::host::split_tags(str(value))) [tokens addObject:ns(tag)];
    ((NSTokenField*)view).objectValue = tokens;
  } else {
    ((NSControl*)view).stringValue = value;
  }
}

/// True while `view` (or, for the style editor, one of its fields) is being typed in.
bool typing(NSView* view) {
  if ([view isKindOfClass:DDStyleEditor.class]) return ((DDStyleEditor*)view).editing;
  if ([view isKindOfClass:DDPropertiesTable.class]) return ((DDPropertiesTable*)view).editing;
  if ([view isKindOfClass:DDTextArea.class]) return ((DDTextArea*)view).editing;
  NSControl* control = (NSControl*)view;
  // A field editor is first responder while a field is being typed in.
  return control.currentEditor && view.window.firstResponder == control.currentEditor;
}

NSTextField* small_label(NSString* text) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:11];
  label.textColor = NSColor.secondaryLabelColor;
  return label;
}

/// Adds a label and a control per field to `stack`.
template <class Fields>
void build(DDInspector* owner, NSStackView* stack, const std::vector<Field<Fields>>& fields, dd::StyleTarget target,
           NSMutableArray<NSView*>* inputs) {
  for (const auto& f : fields) {
    NSView* control;
    switch (f.control) {
      case Control::Arrow: {
        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        [popup addItemsWithTitles:arrow_titles()];
        popup.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        popup.target = owner;
        popup.action = @selector(commit:);  // picking an arrow applies it at once
        control = popup;
        break;
      }
      case Control::Kind:
      case Control::Shape:
      case Control::Tech: {
        // A list to pick from that still takes a new value typed in.
        NSComboBox* combo = [[NSComboBox alloc] initWithFrame:NSZeroRect];
        combo.completes = YES;
        combo.numberOfVisibleItems = 16;
        combo.placeholderString = f.placeholder;
        combo.font = [NSFont systemFontOfSize:12];
        combo.delegate = owner;
        combo.target = owner;
        combo.action = @selector(commit:);
        control = combo;
        break;
      }
      case Control::Tags: {
        NSTokenField* tokens = [[NSTokenField alloc] initWithFrame:NSZeroRect];
        tokens.tokenizingCharacterSet = [NSCharacterSet characterSetWithCharactersInString:@","];
        tokens.placeholderString = f.placeholder;
        tokens.font = [NSFont systemFontOfSize:12];
        tokens.delegate = owner;
        tokens.target = owner;
        tokens.action = @selector(commit:);
        control = tokens;
        break;
      }
      case Control::Style: {
        DDStyleEditor* editor = [[DDStyleEditor alloc] initWithTarget:target];
        __weak DDInspector* weakOwner = owner;
        __weak DDStyleEditor* weakEditor = editor;
        editor.onChange = ^{
          [weakOwner commit:weakEditor];
        };
        control = editor;
        break;
      }
      case Control::Props: {
        DDPropertiesTable* table = [[DDPropertiesTable alloc] initWithFrame:NSZeroRect];
        __weak DDInspector* weakOwner = owner;
        __weak DDPropertiesTable* weakTable = table;
        table.onChange = ^{
          [weakOwner commit:weakTable];
        };
        control = table;
        break;
      }
      case Control::Area: {
        DDTextArea* area = [[DDTextArea alloc] initWithFrame:NSZeroRect];
        __weak DDInspector* weakOwner = owner;
        __weak DDTextArea* weakArea = area;
        area.onCommit = ^{
          [weakOwner commit:weakArea];
        };
        control = area;
        break;
      }
      case Control::Text:
      case Control::Multiline: {
        NSTextField* input = [NSTextField textFieldWithString:@""];
        input.placeholderString = f.placeholder;
        input.font = [NSFont systemFontOfSize:12];
        input.delegate = owner;
        input.target = owner;
        input.action = @selector(commit:);
        if (f.control == Control::Multiline) {
          input.usesSingleLineMode = NO;
          input.cell.wraps = YES;
          input.cell.scrollable = NO;
          [input.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
        }
        control = input;
        break;
      }
    }
    control.identifier = f.key;
    NSTextField* label = small_label(f.label);
    if (f.control == Control::Style) {
      label.font = [NSFont boldSystemFontOfSize:11];
      label.textColor = NSColor.labelColor;
      [stack setCustomSpacing:12 afterView:stack.arrangedSubviews.lastObject];
    }
    [stack addArrangedSubview:label];
    [stack addArrangedSubview:control];
    [stack setCustomSpacing:f.control == Control::Style ? 12 : 8 afterView:control];
    [control.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    [inputs addObject:control];
  }
}

/// Fills a panel's controls; a field being typed in keeps its text when the item is the same.
template <class Fields>
void fill(const std::vector<Field<Fields>>& fields, NSArray<NSView*>* inputs, const Fields& shown, bool same) {
  for (std::size_t i = 0; i < fields.size(); ++i) {
    NSView* input = inputs[i];
    if ([input isKindOfClass:DDStyleEditor.class]) {
      if (!same) [(DDStyleEditor*)input endColorEditing];
      setValue(input, ns(shown.*(fields[i].member)), same);
      continue;
    }
    if (same && typing(input)) continue;
    setValue(input, ns(shown.*(fields[i].member)), same);
  }
}

/// The panel's fields as typed, and whether any differs from what is shown.
template <class Fields>
bool typed(const std::vector<Field<Fields>>& fields, NSArray<NSView*>* inputs, const Fields& shown, Fields& out) {
  out = shown;
  bool changed = false;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    out.*(fields[i].member) = str(valueOf(inputs[i]));
    changed = changed || out.*(fields[i].member) != shown.*(fields[i].member);
  }
  return changed;
}

/// Outlines a field red with why as its tooltip, or takes that off (`problem` null).
void mark(NSView* view, const std::string* problem) {
  if (!problem) {
    if (!view.layer || view.layer.borderWidth == 0) return;
    view.layer.borderWidth = 0;
    view.toolTip = nil;
    return;
  }
  view.wantsLayer = YES;
  view.layer.borderColor = NSColor.systemRedColor.CGColor;
  view.layer.borderWidth = 2;
  view.layer.cornerRadius = 4;
  view.toolTip = ns(*problem);
}

/// Outlines the fields the edit as typed would refuse; returns them.
template <class Fields, class Check>
std::vector<dd::editor::FieldProblem> check(const std::vector<Field<Fields>>& fields, NSArray<NSView*>* inputs,
                                            const std::optional<Fields>& shown, Check with) {
  std::vector<dd::editor::FieldProblem> problems, out;
  if (shown && with) {
    Fields edited;
    typed(fields, inputs, *shown, edited);
    problems = with(edited);
  }
  for (std::size_t i = 0; i < fields.size(); ++i) {
    std::string key = str(fields[i].key);
    auto it = std::ranges::find(problems, key, &dd::editor::FieldProblem::field);
    mark(inputs[i], it == problems.end() ? nullptr : &it->message);
    if (it != problems.end()) out.push_back(*it);
  }
  return out;
}

void set_items(NSComboBox* combo, const std::vector<dd::host::Choice>& choices) {
  [combo removeAllItems];
  NSMutableArray<NSString*>* values = [NSMutableArray array];
  for (const auto& c : choices) [values addObject:ns(c.value)];
  [combo addItemsWithObjectValues:values];
}

}  // namespace

@implementation DDInspector {
  NSTextField* _title;
  NSStackView* _elementStack;
  NSStackView* _relationshipStack;
  /// One control per field, in the order of element_fields() and relationship_fields().
  NSMutableArray<NSView*>* _elementInputs;
  NSMutableArray<NSView*>* _relationshipInputs;
  std::optional<ElementFields> _element;
  std::optional<RelationshipFields> _relationship;
  dd::host::FieldChoices _choices;
  BOOL _filling;
  std::vector<dd::editor::FieldProblem> _invalid;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  _elementInputs = [NSMutableArray array];
  _relationshipInputs = [NSMutableArray array];
  NSStackView* stack = make_stack();
  stack.edgeInsets = NSEdgeInsetsMake(10, 10, 10, 10);

  _title = [NSTextField labelWithString:@""];
  _title.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
  _title.textColor = NSColor.secondaryLabelColor;
  _title.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [stack addArrangedSubview:_title];
  [stack setCustomSpacing:8 afterView:_title];

  // One stack of fields per kind of item; only the one for the selection is shown.
  _elementStack = make_stack();
  _relationshipStack = make_stack();
  for (NSStackView* panel in @[ _elementStack, _relationshipStack ]) {
    [stack addArrangedSubview:panel];
    [panel.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-20].active = YES;
    panel.hidden = YES;
  }
  build(self, _elementStack, element_fields(), dd::StyleTarget::Elements, _elementInputs);
  build(self, _relationshipStack, relationship_fields(), dd::StyleTarget::Relationships, _relationshipInputs);

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
  bool kinds = choices.kinds != _choices.kinds;
  bool shapes = choices.shapes != _choices.shapes;
  bool techs = choices.technologies != _choices.technologies;
  _choices = choices;
  for (NSArray<NSView*>* inputs in @[ _elementInputs, _relationshipInputs ]) {
    for (NSView* input in inputs) {
      if (![input isKindOfClass:NSComboBox.class] || typing(input)) continue;
      NSComboBox* combo = (NSComboBox*)input;
      if ([combo.identifier isEqualToString:@"kind"] && kinds) set_items(combo, _choices.kinds);
      if ([combo.identifier isEqualToString:@"shape"] && shapes) set_items(combo, _choices.shapes);
      if ([combo.identifier isEqualToString:@"technology"] && techs) {
        std::vector<dd::host::Choice> list;
        for (const auto& t : _choices.technologies) list.push_back({t, t, ""});
        set_items(combo, list);
      }
    }
  }
}

- (void)showFields:(const std::optional<ElementFields>&)shown {
  bool same = shown && _element && shown->id == _element->id;
  _element = shown;
  _relationship.reset();  // the panel shows one element, one relationship or nothing
  _elementStack.hidden = !shown;
  _relationshipStack.hidden = YES;
  self.hidden = !shown;
  if (!shown) {
    [self outlineRefusedFields];  // a hidden panel keeps no outlines
    return;
  }
  _filling = YES;
  _title.stringValue = ns(shown->id);
  fill(element_fields(), _elementInputs, *shown, same);
  _filling = NO;
  [self outlineRefusedFields];
}

- (void)showRelationshipFields:(const std::optional<RelationshipFields>&)shown {
  bool same = shown && _relationship && shown->id == _relationship->id;
  _relationship = shown;
  _element.reset();
  _relationshipStack.hidden = !shown;
  _elementStack.hidden = YES;
  self.hidden = !shown;
  if (!shown) {
    [self outlineRefusedFields];  // a hidden panel keeps no outlines
    return;
  }
  _filling = YES;
  std::string title = shown->source + " " + shown->arrow + " " + shown->target;
  if (!shown->name.empty()) title = shown->name + " = " + title;
  _title.stringValue = ns(title);
  fill(relationship_fields(), _relationshipInputs, *shown, same);
  _filling = NO;
  [self outlineRefusedFields];
}

- (void)commit:(id)sender {
  if (_filling) return;
  if (_element && self.onCommit) {
    ElementFields out;
    if (typed(element_fields(), _elementInputs, *_element, out)) self.onCommit(out);
  } else if (_relationship && self.onCommitRelationship) {
    RelationshipFields out;
    if (typed(relationship_fields(), _relationshipInputs, *_relationship, out)) self.onCommitRelationship(out);
  }
  // A refused commit keeps the typed text: it stays outlined.
  [self outlineRefusedFields];
}

/// Outlines the fields of the shown panel the edit as typed would refuse.
- (void)outlineRefusedFields {
  _invalid = ::check(element_fields(), _elementInputs, _element, self.checkElement);
  auto relationship = ::check(relationship_fields(), _relationshipInputs, _relationship, self.checkRelationship);
  _invalid.insert(_invalid.end(), relationship.begin(), relationship.end());
}

- (std::vector<dd::editor::FieldProblem>)invalidFields {
  return _invalid;
}

/// Typing in a field outlines it while the edit would be refused.
- (void)controlTextDidChange:(NSNotification*)note {
  if (!_filling) [self outlineRefusedFields];
}

/// Leaving a field after typing commits it, as Return does; Escape puts it back.
- (void)controlTextDidEndEditing:(NSNotification*)note {
  [self commit:note.object];
}

/// Picking an entry of a combo box's list applies it at once.
- (void)comboBoxSelectionDidChange:(NSNotification*)note {
  if (_filling) return;
  NSComboBox* combo = note.object;
  id picked = combo.objectValueOfSelectedItem;
  if (![picked isKindOfClass:NSString.class]) return;
  // The field shows the entry only after this notification; set it so commit reads it.
  combo.stringValue = picked;
  [self commit:combo];
}

/// Tags already used in the document, for the token field to complete.
- (NSArray*)tokenField:(NSTokenField*)tokenField
    completionsForSubstring:(NSString*)substring
               indexOfToken:(NSInteger)tokenIndex
        indexOfSelectedItem:(NSInteger*)selectedIndex {
  NSMutableArray<NSString*>* out = [NSMutableArray array];
  for (const auto& tag : _choices.tags) {
    NSString* t = ns(tag);
    if (substring.length == 0 || [t rangeOfString:substring options:NSCaseInsensitiveSearch | NSAnchoredSearch].location == 0)
      [out addObject:t];
  }
  if (selectedIndex) *selectedIndex = out.count ? 0 : -1;
  return out;
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command != @selector(cancelOperation:)) return NO;
  if (_element) {
    for (std::size_t i = 0; i < element_fields().size(); ++i)
      if (_elementInputs[i] == control) setValue(control, ns((*_element).*(element_fields()[i].member)), false);
  } else if (_relationship) {
    for (std::size_t i = 0; i < relationship_fields().size(); ++i)
      if (_relationshipInputs[i] == control)
        setValue(control, ns((*_relationship).*(relationship_fields()[i].member)), false);
  } else {
    return NO;
  }
  [self.window makeFirstResponder:nil];
  return YES;
}

- (BOOL)enterField:(NSString*)key to:(NSString*)value {
  if (self.hidden) return NO;
  NSArray<NSView*>* inputs = _element ? _elementInputs : _relationship ? _relationshipInputs : nil;
  for (NSView* input in inputs) {
    if (![input.identifier isEqualToString:key]) continue;
    if (![input isKindOfClass:NSTextField.class] || [input isKindOfClass:NSTokenField.class]) return NO;
    setValue(input, value, false);
    [self outlineRefusedFields];  // as typing it does
    return YES;
  }
  return NO;
}

- (BOOL)setField:(NSString*)key to:(NSString*)value {
  if (self.hidden) return NO;
  NSArray<NSView*>* inputs = _element ? _elementInputs : _relationship ? _relationshipInputs : nil;
  NSString* styleKey = [key hasPrefix:@"style."] ? [key substringFromIndex:6] : nil;
  for (NSView* input in inputs) {
    if (styleKey) {
      if ([input isKindOfClass:DDStyleEditor.class]) return [(DDStyleEditor*)input setKey:styleKey to:value];
      continue;
    }
    if (![input.identifier isEqualToString:key]) continue;
    if ([input isKindOfClass:NSPopUpButton.class] && ![arrows() containsObject:value]) return NO;
    setValue(input, value, false);
    [self commit:input];
    return YES;
  }
  return NO;
}

@end
