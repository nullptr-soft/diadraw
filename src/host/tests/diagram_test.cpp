// Exporting a document (host/diagram.hpp) as `diadraw render`, the C ABI and the Python package do.

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/model/views.hpp"
#include "host/diagram.hpp"

using namespace dd;

namespace {

const char* kSource = R"(model {
  a = system "A"
  b = system "B"
  a -> b "uses"
}

view first {
  include *
}

view second {
  include a
}
)";

std::size_t count(std::string_view text, std::string_view what) {
  std::size_t n = 0;
  for (auto at = text.find(what); at != std::string_view::npos; at = text.find(what, at + 1)) ++n;
  return n;
}

Model model() { return with_default_view(dsl::parse(kSource).model); }

std::string bytes(host::ExportFormat format, const host::ExportOptions& options = {}) {
  auto result = host::export_diagram(model(), format, options, {});
  if (const auto* failure = std::get_if<host::ExportError>(&result)) {
    ADD_FAILURE() << failure->message;
    return {};
  }
  return std::get<host::Exported>(std::move(result)).bytes;
}

}  // namespace

TEST(ExportDiagram, EveryViewIsADrawioPage) {
  EXPECT_EQ(count(bytes(host::ExportFormat::Drawio), "<diagram "), 2u);
}

TEST(ExportDiagram, OneNamedView) {
  host::ExportOptions options;
  options.view = "second";
  auto xml = bytes(host::ExportFormat::Drawio, options);
  EXPECT_EQ(count(xml, "<diagram "), 1u);
  EXPECT_NE(xml.find("second"), std::string::npos);
}

TEST(ExportDiagram, AnUnknownViewListsTheViews) {
  host::ExportOptions options;
  options.view = "nope";
  auto result = host::export_diagram(model(), host::ExportFormat::Svg, options, {});
  const auto* failure = std::get_if<host::ExportError>(&result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->kind, host::ExportError::Kind::NotFound);
  EXPECT_EQ(failure->message, "no view \"nope\" (views: first, second)");
}

TEST(ExportDiagram, Bpmn) {
  auto xml = bytes(host::ExportFormat::Bpmn);
  EXPECT_NE(xml.find("definitions"), std::string::npos);
  EXPECT_EQ(count(xml, "<bpmndi:BPMNDiagram"), 2u);
}

TEST(ExportDiagram, PngRefusesABadBackgroundBeforeDrawing) {
  host::ExportOptions options;
  options.background = "white";
  auto result = host::export_diagram(model(), host::ExportFormat::Png, options, {});
  const auto* failure = std::get_if<host::ExportError>(&result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->kind, host::ExportError::Kind::InvalidArgument);
  EXPECT_NE(failure->message.find("#rgb"), std::string::npos);
}

TEST(ExportDiagram, PngTakesABackground) {
  host::ExportOptions options;
  options.scale = 1;
  options.background = "#ffffff";
  auto result = host::export_diagram(model(), host::ExportFormat::Png, options, {});
  if (const auto* failure = std::get_if<host::ExportError>(&result);
      failure && failure->kind == host::ExportError::Kind::Unsupported)
    GTEST_SKIP() << "no PNG backend in this build";
  ASSERT_TRUE(std::holds_alternative<host::Exported>(result));
  EXPECT_EQ(std::get<host::Exported>(result).bytes.substr(1, 3), "PNG");
}

TEST(ExportFormatNamed, KnowsTheFiveFormatsByTheirLowerCaseNames) {
  EXPECT_EQ(host::export_format_named("svg"), host::ExportFormat::Svg);
  EXPECT_EQ(host::export_format_named("png"), host::ExportFormat::Png);
  EXPECT_EQ(host::export_format_named("pdf"), host::ExportFormat::Pdf);
  EXPECT_EQ(host::export_format_named("drawio"), host::ExportFormat::Drawio);
  EXPECT_EQ(host::export_format_named("bpmn"), host::ExportFormat::Bpmn);
  EXPECT_FALSE(host::export_format_named("SVG"));
  EXPECT_FALSE(host::export_format_named("jpeg"));
  EXPECT_FALSE(host::export_format_named(""));
}

TEST(CheckExportOptions, OnlyPngHasOptionsToRefuse) {
  host::ExportOptions bad;
  bad.scale = 0;
  bad.background = "white";
  EXPECT_FALSE(host::check_export_options(host::ExportFormat::Svg, bad));
  EXPECT_FALSE(host::check_export_options(host::ExportFormat::Pdf, bad));
  EXPECT_FALSE(host::check_export_options(host::ExportFormat::Drawio, bad));
  EXPECT_FALSE(host::check_export_options(host::ExportFormat::Bpmn, bad));
  for (double scale : {0.0, -1.0, 8.5, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
    host::ExportOptions options;
    options.scale = scale;
    auto refused = host::check_export_options(host::ExportFormat::Png, options);
    ASSERT_TRUE(refused) << scale;
    EXPECT_EQ(refused->kind, host::ExportError::Kind::InvalidArgument);
  }
  host::ExportOptions good;
  good.scale = 8;
  good.background = "#fff";
  EXPECT_FALSE(host::check_export_options(host::ExportFormat::Png, good));
}

TEST(LibraryWarnings, AModelGetsTheWarningsItsSourceWould) {
  // c4 is built in, so it resolves offline; the warning is about the shape it does not have.
  const char* source = "use \"c4@^1\" as c4\nmodel {\n  a = system shape c4.nosuch\n}\n";
  host::Analysis analysis = host::analyze(source, "x.draw");
  ASSERT_EQ(analysis.library_warnings.size(), 1u);
  EXPECT_NE(analysis.library_warnings.front().find("no shape \"c4.nosuch\""), std::string::npos);
  const Model& parsed = analysis.parse.model;
  EXPECT_EQ(host::library_warnings(parsed, host::resolve_for_model(parsed, "x.draw")), analysis.library_warnings);
}

TEST(HintWarning, IsWordedOnce) {
  EXPECT_EQ(host::hint_warning("{\"type\":\"pin\"}"), "layout could not honour hint {\"type\":\"pin\"}");
}
