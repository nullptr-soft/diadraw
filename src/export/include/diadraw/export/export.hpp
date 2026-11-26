#pragma once

// PNG and PDF export of rendered diagrams (the SVG `render_svg` writes), for native hosts.
//
// Both formats sit behind small interfaces, so a backend can be replaced without touching callers:
// a backend is one source file that implements `Rasterizer` or `PdfWriter` and its factory, chosen
// at build time with the CMake options DIADRAW_PNG_BACKEND and DIADRAW_PDF_BACKEND.
//
//   - PNG: `lunasvg` (MIT; SVG rasteriser on plutovg, system fonts), or `none`.
//   - PDF: `builtin`, a vector writer for the SVG DiaDraw renders (shapes, paths, markers, nested
//     drawings, text in the standard Helvetica faces, so nothing is embedded), or `none`.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "diadraw/base/result.hpp"

namespace dd::exporting {

/// The `width` and `height` attributes of a rendered DiaDraw SVG.
struct SvgSize {
  double width = 0;
  double height = 0;
};
Result<SvgSize> svg_size(std::string_view svg);

struct PngOptions {
  /// Pixel density; 2 gives a sharp image on high-DPI screens.
  double scale = 2;
  /// A colour behind the drawing, `#rgb` or `#rrggbb`; transparent when unset.
  std::optional<std::string> background;
};

/// The largest width or height, in pixels, a PNG may have.
inline constexpr int kMaxPngSide = 16384;
/// The most pixels a PNG may have (a 1 GB bitmap while it is drawn; plutovg sizes its buffer in
/// int arithmetic, which this keeps well inside). With today's side limit it is the side that binds;
/// this guards the total if `kMaxPngSide` grows.
inline constexpr double kMaxPngPixels = 1 << 28;

/// The PNG's pixel size for an SVG of `size` at `scale`; fails for a size or scale that is not a
/// positive finite number, an image smaller than a pixel, or one over `kMaxPngSide` on a side or
/// `kMaxPngPixels` in all.
Result<std::pair<int, int>> png_pixels(const SvgSize& size, double scale);

/// `#rgb` or `#rrggbb` as 0xRRGGBBAA, opaque; fails for anything else.
Result<std::uint32_t> parse_png_background(std::string_view colour);

class Rasterizer {
 public:
  virtual ~Rasterizer() = default;
  [[nodiscard]] virtual std::string name() const = 0;
  /// The PNG file bytes.
  [[nodiscard]] virtual Result<std::string> png(std::string_view svg, const PngOptions& options) const = 0;
};

class PdfWriter {
 public:
  virtual ~PdfWriter() = default;
  [[nodiscard]] virtual std::string name() const = 0;
  /// A one-page PDF the size of the diagram.
  [[nodiscard]] virtual Result<std::string> pdf(std::string_view svg) const = 0;
};

/// The PNG backend this build was configured with; nullptr when it has none.
std::unique_ptr<Rasterizer> make_rasterizer();
/// The PDF backend this build was configured with; nullptr when it has none.
std::unique_ptr<PdfWriter> make_pdf_writer();

}  // namespace dd::exporting
