#import "DDCanvasView.h"

#include <algorithm>
#include <cmath>

#import "DDDrawing.h"

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

}  // namespace

@interface DDCanvasView () <NSTextFieldDelegate>
@end

// The interaction itself is dd::canvas::Canvas (shared with the other shells); this view forwards
// events to it, paints what it holds and hosts the inline text field.
@implementation DDCanvasView {
  dd::canvas::Canvas _canvas;
  NSTextField* _editor;
  std::optional<dd::canvas::Edit> _edit;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) [self connectCallbacks];
  return self;
}

- (void)connectCallbacks {
  __weak DDCanvasView* weak = self;
  auto& c = _canvas.callbacks;
  c.selected = [weak](const dd::editor::Selection& s) { [weak.delegate canvas:weak didSelect:s]; };
  c.dropped = [weak](const std::string& id, const dd::Box& box, bool freeform) {
    [weak.delegate canvas:weak didDrop:id box:box freeform:freeform];
  };
  c.moved = [weak](const std::vector<std::string>& ids, const dd::Point& offset) {
    [weak.delegate canvas:weak didMove:ids by:offset];
  };
  c.resized = [weak](const std::string& id, const dd::Box& box) { [weak.delegate canvas:weak didResize:id box:box]; };
  c.fitted = [weak](const std::string& id) { [weak.delegate canvas:weak didFit:id]; };
  c.rotated = [weak](const std::string& id, double degrees) { [weak.delegate canvas:weak didRotate:id degrees:degrees]; };
  c.rerouted = [weak](const std::string& id, const std::vector<dd::Point>& points) {
    [weak.delegate canvas:weak didReroute:id points:points];
  };
  c.connected = [weak](const std::string& a, const std::string& b) { [weak.delegate canvas:weak didConnect:a to:b]; };
  c.reconnected = [weak](const std::string& id, dd::editor::End end, const std::string& element) {
    [weak.delegate canvas:weak didReconnect:id end:end to:element];
  };
  c.renamed = [weak](const std::string& id, const std::string& name) { [weak.delegate canvas:weak didRename:id to:name]; };
  c.relabelled = [weak](const std::string& id, const std::optional<std::string>& label) {
    [weak.delegate canvas:weak didRelabel:id to:label];
  };
  c.deleted = [weak](const dd::editor::Selection& s) { [weak.delegate canvas:weak didDelete:s]; };
  c.preview_drop = [weak](const std::string& id, const dd::Box& box, bool freeform) -> std::optional<dd::LayoutHint> {
    DDCanvasView* view = weak;
    if (!view) return std::nullopt;
    return [view.delegate canvas:view previewDrop:id box:box freeform:freeform];
  };
  c.name_of = [weak](const std::string& id) -> std::string {
    DDCanvasView* view = weak;
    return view ? [view.delegate canvas:view nameOf:id] : id;
  };
  c.label_of = [weak](const std::string& id) -> std::string {
    DDCanvasView* view = weak;
    return view ? [view.delegate canvas:view labelOf:id] : "";
  };
}

- (BOOL)isFlipped {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

// ── Content ──

- (void)showRender:(const dd::editor::ViewRender&)render {
  _canvas.show(render);
  [self updateSize];
  [self setNeedsDisplay:YES];
}

- (void)clear {
  _canvas.clear();
  [self setNeedsDisplay:YES];
}

- (CGFloat)zoom {
  return _canvas.zoom();
}

- (void)setZoom:(CGFloat)zoom {
  _canvas.set_zoom(zoom);
  [self updateSize];
  [self setNeedsDisplay:YES];
}

- (CGFloat)grid {
  return _canvas.grid();
}

- (void)setGrid:(CGFloat)grid {
  _canvas.set_grid(grid);
  [self setNeedsDisplay:YES];
}

- (void)updateSize {
  NSSize size = NSMakeSize(_canvas.width(), _canvas.height());
  NSSize visible = self.enclosingScrollView ? self.enclosingScrollView.contentSize : size;
  [self setFrameSize:NSMakeSize(std::max(size.width, visible.width), std::max(size.height, visible.height))];
}

- (dd::editor::Selection)selection {
  return _canvas.selection();
}

- (void)setSelection:(dd::editor::Selection)selection {
  _canvas.set_selection(selection);
  [self setNeedsDisplay:YES];
}

// ── Drawing ──

- (void)drawRect:(NSRect)dirty {
  [[NSColor colorWithSRGBRed:0.973 green:0.98 blue:0.988 alpha:1] setFill];
  NSRectFill(dirty);
  if (!_canvas.list()) return;
  CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
  CGContextSaveGState(ctx);
  CGContextScaleCTM(ctx, _canvas.zoom(), _canvas.zoom());
  // The grid covers the whole view, which can be larger than the drawing.
  dd::mac::paint(ctx, _canvas.backdrop(self.bounds.size.width, self.bounds.size.height));
  dd::mac::paint(ctx, *_canvas.list());
  dd::mac::paint(ctx, _canvas.overlay());
  CGContextRestoreGState(ctx);
}

// ── Mouse ──

- (NSPoint)at:(NSEvent*)event {
  return [self convertPoint:event.locationInWindow fromView:nil];
}

- (dd::canvas::Modifiers)modifiers:(NSEvent*)event {
  NSEventModifierFlags flags = event.modifierFlags;
  return {(flags & NSEventModifierFlagShift) != 0, (flags & NSEventModifierFlagOption) != 0,
          (flags & NSEventModifierFlagCommand) != 0};
}

- (void)mouseDown:(NSEvent*)event {
  if (_editor) return;
  [self.window makeFirstResponder:self];
  NSPoint p = [self at:event];
  std::optional<dd::canvas::Edit> edit;
  if (_canvas.press(p.x, p.y, (int)event.clickCount, [self modifiers:event], &edit)) [self setNeedsDisplay:YES];
  if (edit) [self startEditing:*edit];
}

- (void)mouseDragged:(NSEvent*)event {
  NSPoint p = [self at:event];
  if (_canvas.drag(p.x, p.y, [self modifiers:event])) {
    [self autoscroll:event];
    [self setNeedsDisplay:YES];
  }
}

- (void)mouseUp:(NSEvent*)event {
  NSPoint p = [self at:event];
  if (_canvas.release(p.x, p.y, [self modifiers:event])) [self setNeedsDisplay:YES];
}

- (void)magnifyWithEvent:(NSEvent*)event {
  self.zoom = _canvas.zoom() * (1 + event.magnification);
}

// ── Inline rename ──

- (void)startEditing:(const dd::canvas::Edit&)edit {
  NSRect at = NSMakeRect(edit.x, edit.y, edit.width, edit.height);
  NSTextField* field = [NSTextField textFieldWithString:ns(edit.value)];
  field.frame = NSMakeRect(at.origin.x, NSMidY(at) - 12, std::max<CGFloat>(120, at.size.width), 24);
  field.delegate = self;
  field.font = [NSFont systemFontOfSize:13];
  if (edit.multiline()) {
    // A name may span lines: the field wraps and grows downward with them.
    field.usesSingleLineMode = NO;
    field.cell.wraps = YES;
    field.cell.scrollable = NO;
    field.lineBreakMode = NSLineBreakByWordWrapping;
  }
  [self addSubview:field];
  _editor = field;
  _edit = edit;
  [self fitEditor];
  [self.window makeFirstResponder:field];
  [field selectText:nil];
}

/// Makes a multi-line field as tall as its text, keeping its top where it is.
- (void)fitEditor {
  NSTextField* field = _editor;
  if (!field || !_edit || !_edit->multiline()) return;
  NSRect frame = field.frame;
  CGFloat text_height;
  if (auto* editor = (NSTextView*)field.currentEditor) {
    // While editing, the text is the field editor's (the cell's value is only updated at the end);
    // its used rect counts the empty line after a trailing line break too.
    [editor.layoutManager ensureLayoutForTextContainer:editor.textContainer];
    text_height = NSHeight([editor.layoutManager usedRectForTextContainer:editor.textContainer]) +
                  2 * editor.textContainerInset.height;
  } else {
    text_height = [field.cell cellSizeForBounds:NSMakeRect(0, 0, frame.size.width, CGFLOAT_MAX)].height;
  }
  CGFloat height = std::max<CGFloat>(24, std::ceil(text_height) + 6);
  if (height == frame.size.height) return;
  // The view is flipped, so the origin is the top edge.
  frame.size.height = height;
  field.frame = frame;
}

- (void)finishEditing:(BOOL)save {
  NSTextField* field = _editor;
  if (!field) return;
  _editor = nil;
  std::string value = field.stringValue.UTF8String;
  [field removeFromSuperview];
  [self.window makeFirstResponder:self];
  auto edit = std::move(_edit);
  _edit.reset();
  if (save && edit) _canvas.commit_edit(*edit, value);
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command == @selector(insertNewline:) || command == @selector(insertLineBreak:) ||
      command == @selector(insertNewlineIgnoringFieldEditor:) || command == @selector(noop:)) {
    // ⌘Return (or Control-Return) commits; in a name, Return alone starts a new line.
    NSEvent* event = NSApp.currentEvent;
    bool is_return = event.type == NSEventTypeKeyDown && (event.keyCode == 36 || event.keyCode == 76);
    bool commit_modifier =
        (event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    if (command == @selector(noop:) && !(is_return && commit_modifier)) return NO;
    if (_edit && _edit->multiline() && !(is_return && commit_modifier)) {
      [textView insertNewlineIgnoringFieldEditor:self];  // posts controlTextDidChange:, which fits
      return YES;
    }
    [self finishEditing:YES];
    return YES;
  }
  if (command == @selector(cancelOperation:)) {
    [self finishEditing:NO];
    return YES;
  }
  return NO;
}

- (void)controlTextDidChange:(NSNotification*)note {
  [self fitEditor];
}

- (void)controlTextDidEndEditing:(NSNotification*)note {
  // Focus also goes when the app is deactivated; keep the field open until the person comes back.
  if (!NSApp.isActive || !self.window.isKeyWindow || _scripted) return;
  [self finishEditing:YES];
}

// ── Scripted tests ──

static NSPoint ns_point(dd::draw::Point p) { return NSMakePoint(p.x, p.y); }

- (NSPoint)viewPointFor:(double)x y:(double)y {
  return ns_point(_canvas.to_view(x, y));
}

- (CGFloat)unitsPerPoint {
  return _canvas.units_per_pixel();
}

- (std::optional<NSPoint>)centerOf:(const std::string&)element {
  auto p = _canvas.center_of(element);
  return p ? std::optional(ns_point(*p)) : std::nullopt;
}

- (std::optional<NSPoint>)handlePoint {
  auto p = _canvas.handle_point();
  return p ? std::optional(ns_point(*p)) : std::nullopt;
}

- (std::optional<NSPoint>)endPoint:(dd::editor::End)end {
  auto p = _canvas.end_point(end);
  return p ? std::optional(ns_point(*p)) : std::nullopt;
}

- (std::optional<NSPoint>)resizePoint:(NSUInteger)corner {
  auto points = _canvas.resize_points();
  return corner < points.size() ? std::optional(ns_point(points[corner])) : std::nullopt;
}

- (std::optional<NSPoint>)labelPointOf:(const std::string&)relationship {
  auto p = _canvas.label_point(relationship);
  return p ? std::optional(ns_point(*p)) : std::nullopt;
}

- (BOOL)commitEditing:(NSString*)text {
  if (!_editor) return NO;
  _editor.stringValue = text;
  [self finishEditing:YES];
  return YES;
}

// ── Keys ──

- (void)keyDown:(NSEvent*)event {
  NSString* chars = event.charactersIgnoringModifiers;
  unichar c = chars.length ? [chars characterAtIndex:0] : 0;
  if (c == NSDeleteCharacter || c == NSDeleteFunctionKey || c == NSBackspaceCharacter) {
    if (_canvas.delete_selection()) [self setNeedsDisplay:YES];
  } else if (c == 0x1B) {
    _canvas.escape();
    [self setNeedsDisplay:YES];
  } else {
    [super keyDown:event];
  }
}

- (void)delete:(id)sender {
  if (_canvas.delete_selection()) [self setNeedsDisplay:YES];
}

- (void)selectAll:(id)sender {
  if (_canvas.select_all()) [self setNeedsDisplay:YES];
}

@end
