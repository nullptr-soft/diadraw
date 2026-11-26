// File › Examples: every app lists corpus/*.draw, and every example opens cleanly: no parse
// errors or warnings, and every view lays out and renders with every hint honoured.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "host/document.hpp"
#include "host/examples.hpp"

using namespace dd;
using namespace dd::host;

TEST(Examples, AreTheCorpusFilesSortedByName) {
  std::vector<std::string> files;
  for (const auto& entry : std::filesystem::directory_iterator(DIADRAW_CORPUS_DIR))
    if (entry.path().extension() == ".draw") files.push_back(entry.path().stem().string());
  std::sort(files.begin(), files.end());
  std::vector<std::string> names;
  for (const auto& e : examples()) names.emplace_back(e.name);
  EXPECT_EQ(names, files);
}

TEST(Examples, CarryEachFileVerbatim) {
  for (const auto& e : examples()) {
    auto text = read_file((std::filesystem::path(DIADRAW_CORPUS_DIR) / (std::string(e.name) + ".draw")).string());
    ASSERT_TRUE(text) << e.name;
    std::string expected = *text;
    std::erase(expected, '\r');  // as the generator does
    EXPECT_EQ(e.source, expected) << e.name;
  }
}

TEST(Examples, AreFoundByName) {
  ASSERT_FALSE(examples().empty());
  const Example& first = examples().front();
  const Example* found = find_example(first.name);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->source, first.source);
  EXPECT_EQ(find_example("no-such-example"), nullptr);
}

TEST(Examples, ParseAndRenderEveryView) {
  for (const auto& e : examples()) {
    SCOPED_TRACE(std::string(e.name));
    Document doc{std::string(e.source), ""};
    for (const auto& d : doc.session().parsed().diagnostics)
      ADD_FAILURE() << "line " << d.line << ": " << d.message;
    doc.refresh();
    ASSERT_FALSE(doc.views().empty());
    const std::vector<std::string> views = doc.views();  // refresh() replaces the list
    for (const auto& view : views) {
      SCOPED_TRACE(view);
      doc.choose_view(view);
      auto refreshed = doc.refresh();
      EXPECT_TRUE(refreshed.render.has_value());
      EXPECT_FALSE(refreshed.error) << refreshed.status;
      EXPECT_EQ(refreshed.status, "");
      auto svg = doc.svg();
      ASSERT_TRUE(svg.ok()) << svg.error().message;
      EXPECT_EQ(svg->rfind("<svg", 0), 0u);
    }
  }
}
