// The About dialogs' credits (host/credits.hpp).

#include "host/credits.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

#include "host/about.hpp"
#include "host/files.hpp"

using namespace dd::host;

namespace {

bool has(const std::vector<Credit>& credits, std::string_view name) {
  return std::any_of(credits.begin(), credits.end(), [&](const Credit& c) { return c.name == name; });
}

/// A file of the repository, or "" (which fails the checks below) when it cannot be read.
std::string read_source(const char* path) {
  return read_file(std::string(DIADRAW_SOURCE_DIR) + "/" + path).value_or("");
}

}  // namespace

TEST(Credits, ReadsTabSeparatedLines) {
  auto credits = parse_credits(
      "// a comment\n"
      "\n"
      "miniz\t3.0.2\tMIT\thttps://github.com/richgel999/miniz\tgtk macos  web\r\n"
      "GTK\t4\tLGPL-2.1-or-later\thttps://www.gtk.org\tgtk");
  ASSERT_EQ(credits.size(), 2u);
  EXPECT_EQ(credits[0],
            (Credit{"miniz", "3.0.2", "MIT", "https://github.com/richgel999/miniz", {"gtk", "macos", "web"}}));
  EXPECT_EQ(credits[1], (Credit{"GTK", "4", "LGPL-2.1-or-later", "https://www.gtk.org", {"gtk"}}));
}

TEST(Credits, LeavesOutLinesThatAreNotCredits) {
  EXPECT_TRUE(parse_credits("").empty());
  EXPECT_TRUE(parse_credits("\n// only comments\n   \n").empty());
  EXPECT_TRUE(parse_credits("name only\nname\t1\tMIT\thttps://x\n\t1\tMIT\thttps://x\tgtk\n").empty());
}

TEST(Credits, ALineEndingInATabHasNoApps) {
  EXPECT_EQ(parse_credits("x\t1\tMIT\thttps://x\t"), (std::vector<Credit>{{"x", "1", "MIT", "https://x", {}}}));
}

TEST(Credits, EveryLineOfTheFileIsACredit) {
  std::string text = read_source("src/host/data/credits.tsv");
  ASSERT_FALSE(text.empty());
  std::size_t lines = 0;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);)
    if (!line.empty() && !line.starts_with("//")) ++lines;
  EXPECT_EQ(parse_credits(text).size(), lines);
}

TEST(Credits, CopyrightAndLicenceMatchTheLicenseFile) {
  std::string license = read_source("LICENSE");
  EXPECT_TRUE(license.starts_with(kLicense));
  std::string copyright = kCopyright;  // "Copyright © …"; LICENSE spells it "(c)"
  ASSERT_TRUE(copyright.starts_with("Copyright ©"));
  EXPECT_NE(license.find("Copyright (c)" + copyright.substr(std::string("Copyright ©").size())), std::string::npos);
  EXPECT_NE(read_source("src/apps/macos/CMakeLists.txt").find("set(DIADRAW_COPYRIGHT \"" + copyright + "\")"),
            std::string::npos);
}

TEST(Credits, LabelsNameVersionAndLicence) {
  EXPECT_EQ(credit_label({"miniz", "3.0.2", "MIT", "https://x", {"gtk"}}), "miniz 3.0.2 (MIT)");
  EXPECT_EQ(credit_label({"Thing", "", "", "https://x", {"gtk"}}), "Thing");
}

TEST(Credits, EachNativeAppCreditsWhatItShips) {
  for (std::string_view app : {"gtk", "macos", "winui"}) {
    auto credits = credits_for(app);
    SCOPED_TRACE(app);
    EXPECT_TRUE(has(credits, "miniz"));
    EXPECT_TRUE(has(credits, "lunasvg"));
    EXPECT_TRUE(has(credits, "draw.io stencil sets"));
    EXPECT_FALSE(has(credits, "codemirror"));  // the web app's
    for (const auto& c : credits) {
      EXPECT_FALSE(c.version.empty()) << c.name;
      EXPECT_FALSE(c.license.empty()) << c.name;
      EXPECT_TRUE(c.url.starts_with("https://")) << c.name;
    }
  }
  EXPECT_TRUE(has(credits_for("gtk"), "gtkmm"));
  EXPECT_FALSE(has(credits_for("macos"), "gtkmm"));
  EXPECT_TRUE(has(credits_for("winui"), "Win2D"));
  EXPECT_TRUE(credits_for("nowhere").empty());
}
