#include "diadraw/export/export.hpp"

#include <cmath>
#include <string>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/base/text.hpp"

namespace dd::exporting {

Result<SvgSize> svg_size(std::string_view svg) {
  static const js::Regex kWidth(R"re(<svg[^>]*\swidth="([\d.]+)")re");
  static const js::Regex kHeight(R"re(<svg[^>]*\sheight="([\d.]+)")re");
  auto w = kWidth.exec(svg);
  auto h = kHeight.exec(svg);
  double width = w ? js_to_number(w->view(1)) : 0;
  double height = h ? js_to_number(h->view(1)) : 0;
  if (!(width > 0) || !(height > 0)) return fail("SVG has no width/height");
  return SvgSize{width, height};
}

Result<std::pair<int, int>> png_pixels(const SvgSize& size, double scale) {
  if (!std::isfinite(scale) || !(scale > 0)) return fail("The PNG scale must be a positive number");
  if (!std::isfinite(size.width) || !std::isfinite(size.height) || !(size.width > 0) || !(size.height > 0))
    return fail("The SVG has no size");
  // In double, before any conversion to int, so a huge scale cannot overflow.
  const double width = std::round(size.width * scale);
  const double height = std::round(size.height * scale);
  if (width < 1 || height < 1) return fail("The PNG would be smaller than a pixel; use a larger scale");
  if (!(width <= kMaxPngSide && height <= kMaxPngSide && width * height <= kMaxPngPixels))
    return fail("The PNG would be too large (", std::to_string(static_cast<long long>(width)), " x ",
                std::to_string(static_cast<long long>(height)), " pixels; at most ", std::to_string(kMaxPngSide),
                " on a side and ", std::to_string(static_cast<long long>(kMaxPngPixels)),
                " in all); use a smaller scale");
  return std::pair{static_cast<int>(width), static_cast<int>(height)};
}

Result<std::uint32_t> parse_png_background(std::string_view colour) {
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if ((colour.size() != 4 && colour.size() != 7) || colour[0] != '#')
    return fail("The PNG background must be #rgb or #rrggbb, not \"", colour, "\"");
  std::uint32_t rgb = 0;
  for (char c : colour.substr(1)) {
    const int d = digit(c);
    if (d < 0) return fail("The PNG background must be #rgb or #rrggbb, not \"", colour, "\"");
    const auto nibble = static_cast<std::uint32_t>(d);
    rgb = colour.size() == 4 ? (rgb << 8) | (nibble << 4) | nibble : (rgb << 4) | nibble;
  }
  return (rgb << 8) | 0xFFu;
}

}  // namespace dd::exporting
