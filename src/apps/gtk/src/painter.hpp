// Paints a core display list with Cairo, text with Pango (the system's fonts).

#pragma once

#include <cairo.h>

#include "diadraw/render/display_list.hpp"

namespace dd::gtk {

/// Draws `list` into `cr`, whose user space is y-down with the list's page at the origin.
void paint(cairo_t* cr, const dd::draw::DisplayList& list);

}  // namespace dd::gtk
