// Code folding for the text pane: a ruler left of the text with a marker on every block that
// spans lines (`model`, `view`, elements with children, groups, …, from dd::dsl::fold_ranges).
// Clicking a marker hides the lines between the block's braces (null glyphs and zero-advance line
// breaks, so the text itself never changes), or shows them again. A folded block stays folded
// while the text around it changes, and unfolds when its braces go or the caret moves into it.

#pragma once

#import <AppKit/AppKit.h>

#include <vector>

/// A line with a fold marker (0-based) and whether its block is folded, for the gesture tests.
struct DDFoldMarker {
  NSInteger line = 0;
  bool folded = false;
};

@interface DDTextFolds : NSRulerView <NSLayoutManagerDelegate>

/// Becomes the vertical ruler of `view`'s scroll view and the delegate of its layout manager.
- (instancetype)initWithTextView:(NSTextView*)view;

/// Recomputes the blocks after the text changed (call from textDidChange:).
- (void)textDidChange;
/// Unfolds the blocks holding the caret (call from textViewDidChangeSelection:).
- (void)selectionDidChange;
/// Folds or unfolds the block starting on `line` (0-based); NO when no block starts there.
- (BOOL)toggleLine:(NSInteger)line;
/// Not `markers`, which NSRulerView has for its NSRulerMarkers.
- (std::vector<DDFoldMarker>)foldMarkers;

@end
