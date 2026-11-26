// Paints a core display list with Win2D (Direct2D underneath), text with DirectWrite.

#pragma once

#include "winrt.hpp"

#include "diadraw/render/display_list.hpp"

namespace dd::winui {

/// Draws `list` into `ds`, whose user space is y-down with the list's page at the origin.
void paint(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& ds, const dd::draw::DisplayList& list);

}  // namespace dd::winui
