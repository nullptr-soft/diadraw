// PNG and PDF export through the configured backends.

#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>
#include <string>

#include "diadraw/export/export.hpp"
#include "miniz.h"

using namespace dd::exporting;

namespace {
const char* kSvg =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="60" viewBox="-10 -10 120 60">)svg"
    R"svg(<defs><marker id="m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">)svg"
    R"svg(<path d="M0,0 L10,5 L0,10 z" fill="#475569"/></marker></defs>)svg"
    R"svg(<rect x="0" y="0" width="40" height="30" rx="6" fill="#1e3a8a" stroke="#000" fill-opacity="0.5"/>)svg"
    R"svg(<path d="M40,15 C60,15 60,15 80,15 A5 5 0 0 1 90 20" fill="none" stroke="#475569" stroke-dasharray="4 2" marker-end="url(#m)"/>)svg"
    R"svg(<svg x="80" y="0" width="20" height="20" viewBox="0 0 48 48"><circle cx="24" cy="24" r="20" fill="red"/></svg>)svg"
    R"svg(<text x="20" y="15" text-anchor="middle" dominant-baseline="central" font-size="11" font-weight="600" fill="#fff">Café €</text></svg>)svg";
}

TEST(Export, SvgSize) {
  auto size = svg_size(kSvg);
  ASSERT_TRUE(size);
  EXPECT_EQ(size->width, 120);
  EXPECT_FALSE(svg_size("<svg/>"));
}

TEST(Export, PngPixelsRefuseBadScalesAndHugeImages) {
  auto pixels = png_pixels({120, 60}, 2);
  ASSERT_TRUE(pixels.ok());
  EXPECT_EQ(pixels->first, 240);
  EXPECT_EQ(pixels->second, 120);
  EXPECT_FALSE(png_pixels({120, 60}, 0).ok());
  EXPECT_FALSE(png_pixels({120, 60}, -1).ok());
  EXPECT_FALSE(png_pixels({120, 60}, std::numeric_limits<double>::quiet_NaN()).ok());
  EXPECT_FALSE(png_pixels({120, 60}, std::numeric_limits<double>::infinity()).ok());
  EXPECT_FALSE(png_pixels({120, 60}, 1e12).ok());
  EXPECT_TRUE(png_pixels({kMaxPngSide, 1}, 1).ok());
  EXPECT_FALSE(png_pixels({kMaxPngSide + 1, 1}, 1).ok());
  EXPECT_TRUE(png_pixels({kMaxPngSide, kMaxPngSide}, 1).ok());  // 1 GB of pixels, the most there may be
  EXPECT_FALSE(png_pixels({kMaxPngSide, kMaxPngSide + 1.0}, 1).ok());
  EXPECT_FALSE(png_pixels({120, 60}, 0.001).ok());  // smaller than a pixel
  EXPECT_FALSE(png_pixels({std::numeric_limits<double>::quiet_NaN(), 10}, 1).ok());
  EXPECT_FALSE(png_pixels({-1e20, 10}, 1).ok());
}

TEST(Export, PngBackgroundIsHex) {
  EXPECT_EQ(parse_png_background("#ffffff").value(), 0xFFFFFFFFu);
  EXPECT_EQ(parse_png_background("#1e3A8a").value(), 0x1E3A8AFFu);
  EXPECT_EQ(parse_png_background("#abc").value(), 0xAABBCCFFu);
  EXPECT_FALSE(parse_png_background("#zzzzzz").ok());
  EXPECT_FALSE(parse_png_background("white").ok());
  EXPECT_FALSE(parse_png_background("#abcd").ok());
  EXPECT_FALSE(parse_png_background("").ok());
}

TEST(Export, PdfIsAOnePageVectorDocument) {
  auto writer = make_pdf_writer();
  if (!writer) GTEST_SKIP() << "no PDF backend in this build";
  auto pdf = writer->pdf(kSvg);
  ASSERT_TRUE(pdf) << pdf.error().message;
  EXPECT_EQ(pdf->substr(0, 8), "%PDF-1.4");
  EXPECT_NE(pdf->find("/MediaBox [0 0 120 60]"), std::string::npos);
  EXPECT_NE(pdf->find("/BaseFont /Helvetica-Bold"), std::string::npos);
  EXPECT_NE(pdf->find("/BaseFont /Helvetica-Oblique"), std::string::npos);
  EXPECT_NE(pdf->find("/BaseFont /Helvetica-BoldOblique"), std::string::npos);
  EXPECT_NE(pdf->find("/ExtGState"), std::string::npos);
  EXPECT_EQ(pdf->substr(pdf->size() - 6), "%%EOF\n");
}

TEST(Export, PngAtScale) {
  auto rasterizer = make_rasterizer();
  if (!rasterizer) GTEST_SKIP() << "no PNG backend in this build";
  auto png = rasterizer->png(kSvg, {2, "#ffffff"});
  ASSERT_TRUE(png) << png.error().message;
  EXPECT_EQ(png->substr(1, 3), "PNG");
  // IHDR: width and height, big-endian, at bytes 16..23.
  auto be32 = [&](std::size_t at) {
    return (static_cast<unsigned char>((*png)[at]) << 24) | (static_cast<unsigned char>((*png)[at + 1]) << 16) |
           (static_cast<unsigned char>((*png)[at + 2]) << 8) | static_cast<unsigned char>((*png)[at + 3]);
  };
  EXPECT_EQ(be32(16), 240u);
  EXPECT_EQ(be32(20), 120u);
}

TEST(Export, PngRefusesScalesItCannotDraw) {
  auto rasterizer = make_rasterizer();
  if (!rasterizer) GTEST_SKIP() << "no PNG backend in this build";
  for (double scale : {0.0, -1.0, 1e6, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    EXPECT_FALSE(rasterizer->png(kSvg, {scale, std::nullopt})) << scale;
}

TEST(Export, PdfDrawsEachRunOfALabelInItsFace) {
  auto writer = make_pdf_writer();
  if (!writer) GTEST_SKIP() << "no PDF backend in this build";
  auto pdf = writer->pdf(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="40"><text x="10" y="20" font-size="10" fill="#000">)"
      R"(<tspan>a </tspan><tspan font-weight="600" font-style="italic">b</tspan><tspan font-style="italic"> c</tspan>)"
      R"(<tspan text-decoration="underline line-through">d</tspan></text></svg>)");
  ASSERT_TRUE(pdf) << pdf.error().message;
  // The page's content stream, inflated.
  const auto start = pdf->find("stream\n") + 7;
  const auto end = pdf->find("\nendstream", start);
  ASSERT_LT(start, end);
  std::size_t size = 0;
  void* raw = tinfl_decompress_mem_to_heap(pdf->data() + start, end - start, &size, TINFL_FLAG_PARSE_ZLIB_HEADER);
  ASSERT_NE(raw, nullptr);
  const std::string content(static_cast<const char*>(raw), size);
  std::free(raw);
  EXPECT_NE(content.find("/F1 10 Tf"), std::string::npos) << content;
  EXPECT_NE(content.find("/F4 10 Tf"), std::string::npos) << content;
  EXPECT_NE(content.find("/F3 10 Tf"), std::string::npos) << content;
  // An underline and a strike-through under and through "d".
  std::size_t bars = 0;
  for (auto at = content.find(" re f"); at != std::string::npos; at = content.find(" re f", at + 1)) ++bars;
  EXPECT_EQ(bars, 2U) << content;
}
