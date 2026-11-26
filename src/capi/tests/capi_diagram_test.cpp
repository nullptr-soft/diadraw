// ABI 2 of the C ABI: the DSL, layout, rendering and import (diadraw_capi).

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "diadraw/diadraw.h"
#include "helpers.hpp"

namespace {

std::string take(char* s) {
  std::string out = s ? s : "";
  dd_string_free(s);
  return out;
}

constexpr const char* kSource = R"(model {
  a = process "Alpha"
  b = process "Beta"
  a -> b "calls"
}

view main {
  include *
}
)";

}  // namespace

TEST(CABI2, ReportsTheAbiVersion) { EXPECT_EQ(dd_abi_version(), 2); }

TEST(CABI2, ParsesChecksAndFormats) {
  auto parsed = take(dd_parse_json(kSource));
  EXPECT_TRUE(dd::test::contains(parsed, R"("model":)"));
  EXPECT_TRUE(dd::test::contains(parsed, R"("diagnostics":[])"));

  auto checked = take(dd_check_json(kSource, nullptr));
  EXPECT_TRUE(dd::test::contains(checked, R"("errors":0)"));

  auto bad = take(dd_check_json("model { a = }", nullptr));
  EXPECT_FALSE(dd::test::contains(bad, R"("errors":0)"));
  EXPECT_TRUE(dd::test::contains(bad, R"("severity":"error")"));

  char* error = nullptr;
  auto formatted = take(dd_format_source("model {\n a =   process \"A\"\n}\n", &error));
  EXPECT_EQ(error, nullptr);
  EXPECT_TRUE(dd::test::contains(formatted, "a = process \"A\""));
  EXPECT_EQ(dd_format_source("model { a = }", &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "error"));

  EXPECT_EQ(dd_parse_json(nullptr), nullptr);
  EXPECT_EQ(dd_check_json(nullptr, nullptr), nullptr);
}

TEST(CABI2, RoundTripsDocumentsThroughSource) {
  char* error = nullptr;
  dd_document* doc = dd_document_from_source(kSource, &error);
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(dd_document_apply(doc, R"({"type":"addElement","element":{"id":"c","kind":"process","name":"Gamma"}})",
                              &error),
            DD_OK);
  auto source = take(dd_document_source(doc));
  EXPECT_TRUE(dd::test::contains(source, "c = process \"Gamma\""));
  dd_document_free(doc);

  auto printed = take(dd_print_model(R"({"elements":{"x":{"id":"x","kind":"process","name":"X"}},"relationships":{},
                                        "groups":{},"views":{}})",
                                     &error));
  EXPECT_TRUE(dd::test::contains(printed, "x = process \"X\""));
  EXPECT_EQ(dd_print_model("{nope", &error), nullptr);
  take(error);

  EXPECT_EQ(dd_document_from_source("model { a = }", &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "error"));
  EXPECT_EQ(dd_document_source(nullptr), nullptr);
}

TEST(CABI2, LaysOutViews) {
  char* error = nullptr;
  auto geometry = take(dd_layout_json(kSource, nullptr, "main", &error));
  EXPECT_EQ(error, nullptr);
  EXPECT_TRUE(dd::test::contains(geometry, R"("nodes":{"a":{"x":)"));
  EXPECT_TRUE(dd::test::contains(geometry, R"("edges":{)"));
  EXPECT_TRUE(dd::test::contains(take(dd_layout_json(kSource, nullptr, nullptr, &error)), R"("width":)"));
  EXPECT_EQ(dd_layout_json(kSource, nullptr, "nope", &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "nope"));
}

TEST(CABI2, RendersEveryFormat) {
  char* out = nullptr;
  size_t len = 0;
  char* error = nullptr;

  ASSERT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_SVG, 0, &out, &len, &error), DD_OK);
  std::string svg(out, len);
  dd_string_free(out);
  EXPECT_TRUE(dd::test::contains(svg, "<svg"));
  EXPECT_TRUE(dd::test::contains(svg, "Alpha"));

  ASSERT_EQ(dd_render(kSource, nullptr, "main", DD_FORMAT_PNG, 1, &out, &len, &error), DD_OK);
  EXPECT_GT(len, 8u);
  EXPECT_EQ(std::string(out, 4), "\x89PNG");
  dd_string_free(out);

  ASSERT_EQ(dd_render(kSource, nullptr, "main", DD_FORMAT_PDF, 0, &out, &len, &error), DD_OK);
  EXPECT_EQ(std::string(out, 5), "%PDF-");
  dd_string_free(out);

  ASSERT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_DRAWIO, 0, &out, &len, &error), DD_OK);
  EXPECT_TRUE(dd::test::contains(std::string(out, len), "<mxfile"));
  dd_string_free(out);

  ASSERT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_BPMN, 0, &out, &len, &error), DD_OK);
  EXPECT_TRUE(dd::test::contains(std::string(out, len), "definitions"));
  dd_string_free(out);

  EXPECT_EQ(dd_render(kSource, nullptr, "nope", DD_FORMAT_SVG, 0, &out, &len, &error), DD_NOT_FOUND);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(len, 0u);
  take(error);
  EXPECT_EQ(dd_render(kSource, nullptr, "nope", DD_FORMAT_DRAWIO, 0, &out, &len, &error), DD_NOT_FOUND);
  take(error);
  EXPECT_EQ(dd_render("model { a = }", nullptr, nullptr, DD_FORMAT_SVG, 0, &out, &len, &error), DD_REJECTED);
  take(error);
  EXPECT_EQ(dd_render(kSource, nullptr, nullptr, static_cast<dd_format>(DD_FORMAT_BPMN + 1), 0, &out, &len, &error),
            DD_INVALID_ARGUMENT);
  take(error);
  EXPECT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_SVG, 0, nullptr, &len, nullptr), DD_INVALID_ARGUMENT);
}

TEST(CABI2, RefusesPngScalesItCannotDraw) {
  char* out = nullptr;
  size_t len = 0;
  char* error = nullptr;
  for (double scale : {9.0, 1e6, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_PNG, scale, &out, &len, &error), DD_INVALID_ARGUMENT)
        << scale;
    EXPECT_EQ(out, nullptr);
    EXPECT_TRUE(dd::test::contains(take(error), "scale"));
  }
  // 0 and below mean the default, 2.
  ASSERT_EQ(dd_render(kSource, nullptr, nullptr, DD_FORMAT_PNG, -1, &out, &len, &error), DD_OK);
  dd_string_free(out);
  // A diagram too large to rasterise at the largest scale fails cleanly.
  std::string wide = "model {\n";
  for (int i = 0; i < 200; ++i) {
    wide += "  e" + std::to_string(i) + " = process \"Element " + std::to_string(i) + "\"\n";
    if (i > 0) wide += "  e" + std::to_string(i - 1) + " -> e" + std::to_string(i) + "\n";
  }
  wide += "}\nview v {\n  include *\n  layout layered direction right\n}\n";
  ASSERT_EQ(dd_render(wide.c_str(), nullptr, nullptr, DD_FORMAT_PNG, 8, &out, &len, &error), DD_FAILED);
  EXPECT_EQ(out, nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "too large"));
}

TEST(CABI2, ResolvesLibrariesForAFileThatNeedNotExist) {
  auto file = (std::filesystem::temp_directory_path() / "diadraw-capi-missing" / "doc.draw").string();
  auto checked = take(dd_check_json(kSource, file.c_str()));
  EXPECT_TRUE(dd::test::contains(checked, R"("errors":0)"));
  char* out = nullptr;
  size_t len = 0;
  char* error = nullptr;
  ASSERT_EQ(dd_render(kSource, file.c_str(), "main", DD_FORMAT_SVG, 0, &out, &len, &error), DD_OK);
  dd_string_free(out);
  auto geometry = take(dd_layout_json(kSource, file.c_str(), nullptr, &error));
  EXPECT_TRUE(dd::test::contains(geometry, R"("nodes":)"));

  // Unknown library kinds are warnings with or without a file.
  const char* unknown = "model {\n  a = nosuchkind \"A\"\n}\n";
  EXPECT_EQ(take(dd_check_json(unknown, nullptr)), take(dd_check_json(unknown, file.c_str())));
}

TEST(CABI2, SurvivesOddInput) {
  char* out = nullptr;
  size_t len = 0;
  char* error = nullptr;
  for (const char* source : {"", "\xff\xfe\x80 model {", "model {\n  a = process \"\xc3\"\n}\n"}) {
    take(dd_parse_json(source));
    take(dd_check_json(source, nullptr));
    take(dd_format_source(source, &error));
    take(error);
    error = nullptr;
    take(dd_layout_json(source, nullptr, "\xff", &error));
    take(error);
    error = nullptr;
    dd_render(source, nullptr, nullptr, DD_FORMAT_SVG, 0, &out, &len, &error);
    dd_string_free(out);
    take(error);
    error = nullptr;
  }
  EXPECT_EQ(dd_layout_json("model { a = }", nullptr, nullptr, &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "error"));
  for (const auto& [data, format] : std::vector<std::pair<const char*, const char*>>{
           {"<mxfile><diagram name=\"x\">", "drawio"},
           {"<?xml version=\"1.0\"?><definitions><process", "bpmn"},
           {"\xff\xfe garbage", "plantuml"},
           {"workspace {", "structurizr"},
           {"flowchart LR\n  A -->", "mermaid"}}) {
    error = nullptr;
    take(dd_import_json(data, nullptr, format, 1, &error));
    take(error);
  }
}

TEST(CABI2, ImportsOtherFormats) {
  char* error = nullptr;
  auto imported = take(dd_import_json("flowchart LR\n  A[Start] --> B[End]\n", "flow.mmd", nullptr, 1, &error));
  EXPECT_EQ(error, nullptr);
  EXPECT_TRUE(dd::test::contains(imported, R"("source":")"));
  EXPECT_TRUE(dd::test::contains(imported, R"("report":)"));
  EXPECT_TRUE(dd::test::contains(imported, "Start"));

  auto named = take(dd_import_json("flowchart LR\n  A --> B\n", nullptr, "mermaid", 0, &error));
  EXPECT_TRUE(dd::test::contains(named, R"("model":)"));

  EXPECT_EQ(dd_import_json("hello", nullptr, "nope", 0, &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "unknown format"));
  EXPECT_EQ(dd_import_json("hello", "notes.txt", nullptr, 0, &error), nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "cannot tell"));
}

TEST(CABI2, ReadsIncludesOnlyForAFile) {
  auto dir = std::filesystem::temp_directory_path() / "diadraw-capi-include";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "secret.dsl") << "secretPerson = person \"Secret Person\"\n";
  const std::string data = "workspace {\n  model {\n    !include " + (dir / "secret.dsl").generic_string() +
                           "\n    !include secret.dsl\n  }\n}\n";
  char* error = nullptr;
  auto without = take(dd_import_json(data.c_str(), nullptr, "structurizr", 1, &error));
  take(error);
  EXPECT_FALSE(dd::test::contains(without, "Secret Person"));
  error = nullptr;
  auto with = take(dd_import_json(data.c_str(), (dir / "workspace.dsl").string().c_str(), "structurizr", 1, &error));
  take(error);
  EXPECT_TRUE(dd::test::contains(with, "Secret Person"));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
