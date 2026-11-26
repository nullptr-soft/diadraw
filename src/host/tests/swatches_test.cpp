// The colour pickers' preset swatches (host/swatches.hpp).

#include "host/swatches.hpp"

#include <gtest/gtest.h>

#include <set>

#include "host/field_editors.hpp"

using namespace dd::host;

TEST(Swatches, ReadsRowsOfColoursAndNames) {
  auto rows = parse_swatches(
      "// a comment\n"
      "\n"
      "#FFFFFF White\n"
      "  #abc   Light  blue  \r\n"
      "\n"
      "\n"
      "#000000\tBlack\n");
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0], (std::vector<Swatch>{{"#ffffff", "White"}, {"#aabbcc", "Light  blue"}}));
  EXPECT_EQ(rows[1], (std::vector<Swatch>{{"#000000", "Black"}}));
}

TEST(Swatches, LeavesOutLinesThatAreNotSwatches) {
  auto rows = parse_swatches("red Red\n#12345 Short\n#123456\nplain words\n#654321 Kept");
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], (std::vector<Swatch>{{"#654321", "Kept"}}));
  EXPECT_TRUE(parse_swatches("").empty());
  EXPECT_TRUE(parse_swatches("\n// only comments\n\n").empty());
}

TEST(Swatches, BuiltInPaletteIsAGridOfDistinctNamedColours) {
  const auto& rows = color_swatches();
  ASSERT_FALSE(rows.empty());
  std::set<std::string> colors, names;
  std::size_t count = 0;
  for (const auto& row : rows) {
    EXPECT_EQ(row.size(), rows.front().size()) << "every row is as long as the first";
    for (const auto& s : row) {
      EXPECT_EQ(normalized_color(s.color), s.color);
      EXPECT_FALSE(s.name.empty());
      colors.insert(s.color);
      names.insert(s.name);
      ++count;
    }
  }
  EXPECT_EQ(colors.size(), count) << "no colour is listed twice";
  EXPECT_EQ(names.size(), count) << "no name is listed twice";
  EXPECT_TRUE(colors.contains("#ffffff"));
  EXPECT_TRUE(colors.contains("#000000"));
  EXPECT_EQ(&color_swatches(), &rows) << "read once";
}
