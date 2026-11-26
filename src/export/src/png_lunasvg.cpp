// The PNG backend on lunasvg (MIT): renders the SVG at `scale` with the system's fonts.

#include <cstdint>

#include "diadraw/export/export.hpp"
#include "lunasvg.h"

namespace dd::exporting {

namespace {

class LunaRasterizer final : public Rasterizer {
 public:
  [[nodiscard]] std::string name() const override { return "lunasvg"; }

  [[nodiscard]] Result<std::string> png(std::string_view svg, const PngOptions& options) const override {
    auto size = svg_size(svg);
    if (!size) return size.error();
    auto document = lunasvg::Document::loadFromData(svg.data(), svg.size());
    if (!document) return fail("The SVG could not be read");
    auto pixels = png_pixels(*size, options.scale);
    if (!pixels) return pixels.error();
    const auto [width, height] = *pixels;
    std::uint32_t background = 0x00000000;
    if (options.background) {
      auto colour = parse_png_background(*options.background);
      if (!colour) return colour.error();
      background = *colour;
    }
    lunasvg::Bitmap bitmap = document->renderToBitmap(width, height, background);
    if (bitmap.isNull()) return fail("The SVG could not be rendered");
    std::string out;
    bitmap.writeToPng(
        [](void* closure, void* data, int length) {
          static_cast<std::string*>(closure)->append(static_cast<const char*>(data), static_cast<std::size_t>(length));
        },
        &out);
    if (out.empty()) return fail("PNG encoding failed");
    return out;
  }
};

}  // namespace

std::unique_ptr<Rasterizer> make_rasterizer() { return std::make_unique<LunaRasterizer>(); }

}  // namespace dd::exporting
