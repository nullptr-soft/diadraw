// Syntax colouring and inline diagnostics for the text pane. Tokens come from the core
// (dd::dsl::highlight), diagnostics from the parse; both are painted as layout-manager temporary
// attributes, so they never enter the text, the undo history or the saved file.

#pragma once

#import <AppKit/AppKit.h>

#include <string>
#include <vector>

#include "diadraw/dsl/parse.hpp"

/// A diagnostic placed in the text view's (UTF-16) coordinates.
struct DDMark {
  NSRange range;
  bool error = true;
  NSString* message;
};

/// The UTF-16 offset (what the text view counts) of every UTF-8 byte offset of `text`, and of its end.
std::vector<NSUInteger> DDUtf16Map(const std::string& text);

/// Recolours all of `view`'s text from its current contents.
void DDColourSyntax(NSTextView* view);

/// Underlines the diagnostics of `text` (the UTF-8 the view shows) and returns them placed.
std::vector<DDMark> DDMarkDiagnostics(NSTextView* view, const std::string& text,
                                      const std::vector<dd::dsl::Diagnostic>& diagnostics);
