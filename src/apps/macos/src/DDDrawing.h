// Paints a core display list with Core Graphics, text with Core Text (the system's fonts).

#pragma once

#import <CoreGraphics/CoreGraphics.h>

#include "diadraw/render/display_list.hpp"

namespace dd::mac {

/// Draws `list` into `context`, whose user space is y-down with the list's page at the origin.
void paint(CGContextRef context, const dd::draw::DisplayList& list);

}  // namespace dd::mac
