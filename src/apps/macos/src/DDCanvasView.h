// The diagram as an editor: an AppKit host for dd::canvas::Canvas (select, Shift or Command adds,
// drag over empty canvas to select what the band encloses, ⌘A selects all, drag one of several
// selected nodes to move them together, drag to place, Option for an exact position, drag the
// handle to connect, drag an end of a selected line to reconnect it, double-click to rename,
// Delete to remove, drag a corner to resize (double-click it to fit the node to its content) and
// the round handle off the top-right corner to rotate; in fixed views, drag a bend point to
// reroute). The view holds no model logic: it paints what the core rendered and reports gestures
// to its delegate, which turns them into core calls.

#pragma once

#import <AppKit/AppKit.h>

#include <optional>
#include <string>
#include <vector>

#include "diadraw/canvas/canvas.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/render/display_list.hpp"

@class DDCanvasView;

@protocol DDCanvasDelegate <NSObject>
- (void)canvas:(DDCanvasView*)canvas didSelect:(const dd::editor::Selection&)selection;
- (void)canvas:(DDCanvasView*)canvas didDrop:(const std::string&)element box:(const dd::Box&)box freeform:(BOOL)freeform;
/// Several selected nodes dragged together, each by `offset` (diagram units).
- (void)canvas:(DDCanvasView*)canvas didMove:(const std::vector<std::string>&)elements by:(const dd::Point&)offset;
/// A node resized, or a relationship's bend points dragged (fixed views only).
- (void)canvas:(DDCanvasView*)canvas didResize:(const std::string&)element box:(const dd::Box&)box;
/// A resize corner double-clicked: size the node to fit its content.
- (void)canvas:(DDCanvasView*)canvas didFit:(const std::string&)element;
/// A node turned with its rotate handle: degrees clockwise.
- (void)canvas:(DDCanvasView*)canvas didRotate:(const std::string&)element degrees:(double)degrees;
- (void)canvas:(DDCanvasView*)canvas
    didReroute:(const std::string&)relationship
        points:(const std::vector<dd::Point>&)points;
- (void)canvas:(DDCanvasView*)canvas didConnect:(const std::string&)source to:(const std::string&)target;
- (void)canvas:(DDCanvasView*)canvas
    didReconnect:(const std::string&)relationship
             end:(dd::editor::End)end
              to:(const std::string&)element;
- (void)canvas:(DDCanvasView*)canvas didRename:(const std::string&)element to:(const std::string&)name;
- (void)canvas:(DDCanvasView*)canvas didRelabel:(const std::string&)relationship to:(const std::optional<std::string>&)label;
- (void)canvas:(DDCanvasView*)canvas didDelete:(const dd::editor::Selection&)selection;
/// The hint a drop would write, for the preview while dragging.
- (std::optional<dd::LayoutHint>)canvas:(DDCanvasView*)canvas previewDrop:(const std::string&)element
                                     box:(const dd::Box&)box
                                freeform:(BOOL)freeform;
- (std::string)canvas:(DDCanvasView*)canvas nameOf:(const std::string&)element;
- (std::string)canvas:(DDCanvasView*)canvas labelOf:(const std::string&)relationship;
@end

@interface DDCanvasView : NSView
@property(nonatomic, weak) id<DDCanvasDelegate> delegate;
@property(nonatomic) CGFloat zoom;
/// The grid's step in diagram units; 0 hides it. While it shows, drags, resizes and bend points
/// land on it.
@property(nonatomic) CGFloat grid;
/// Shows a newly rendered view; the selection keeps what still exists.
- (void)showRender:(const dd::editor::ViewRender&)render;
- (void)clear;
@property(nonatomic) dd::editor::Selection selection;

// For scripted tests: where things are, in view coordinates, and finishing an inline edit.
- (std::optional<NSPoint>)centerOf:(const std::string&)element;
- (std::optional<NSPoint>)handlePoint;
- (std::optional<NSPoint>)endPoint:(dd::editor::End)end;
/// A resize corner of the one selected node: 0 top-left, then clockwise.
- (std::optional<NSPoint>)resizePoint:(NSUInteger)corner;
- (std::optional<NSPoint>)labelPointOf:(const std::string&)relationship;
- (NSPoint)viewPointFor:(double)x y:(double)y;
- (CGFloat)unitsPerPoint;
- (BOOL)commitEditing:(NSString*)text;
/// While a test script runs: focus changes do not end inline editing.
@property(nonatomic) BOOL scripted;
- (void)delete:(id)sender;
- (void)selectAll:(id)sender;
@end
