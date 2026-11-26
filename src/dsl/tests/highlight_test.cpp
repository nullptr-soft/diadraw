#include "diadraw/dsl/highlight.hpp"
#include "diadraw/dsl/json.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using dd::dsl::highlight;

/// "text:tag" for each token, for readable expectations.
std::vector<std::string> show(std::string_view text) {
  std::vector<std::string> out;
  for (const auto& t : highlight(text))
    out.push_back(std::string(text.substr(t.from, t.to - t.from)) + ":" +
                  std::string(dd::dsl::token_kind_name(t.kind)));
  return out;
}

TEST(Highlight, DeclarationsAndRelationships) {
  EXPECT_EQ(show("model {\n  shop = system \"Shop\"\n  user -> shop\n}"),
            (std::vector<std::string>{"model:keyword", "{:bracket", "shop:definition", "=:operator", "system:typeName",
                                      "\"Shop\":string", "user:variableName", "->:operator", "shop:variableName",
                                      "}:bracket"}));
}

TEST(Highlight, NamedRelationshipEndpointsAreNotKinds) {
  EXPECT_EQ(show("pay = shop.web --> db"),
            (std::vector<std::string>{"pay:definition", "=:operator", "shop:variableName", "web:variableName",
                                      "-->:operator", "db:variableName"}));
}

TEST(Highlight, KindMayFollowOnALaterLine) {
  EXPECT_EQ(show("a =\n\n  process"), (std::vector<std::string>{"a:definition", "=:operator", "process:typeName"}));
}

TEST(Highlight, CommentsSpanLinesAndMerge) {
  EXPECT_EQ(show("x /* a\nb */ y // z"),
            (std::vector<std::string>{"x:variableName", "/* a:comment", "b */:comment", "y:variableName",
                                      "// z:comment"}));
}

TEST(Highlight, NumbersStringsAndArrows) {
  EXPECT_EQ(show("-1.5 2. --> ->> <-> \"a\\\"b\" \"open"),
            (std::vector<std::string>{"-1.5:number", "2:number", "-->:operator", "->>:operator", "<->:operator",
                                      "\"a\\\"b\":string", "\"open:string"}));
}

TEST(Highlight, BlockStringsSpanLines) {
  EXPECT_EQ(show("a = x description \"\"\"\n  one \\\"\"\"\n\n  two\n  \"\"\" tech \"t\""),
            (std::vector<std::string>{"a:definition", "=:operator", "x:typeName", "description:keyword",
                                      "\"\"\":string", "  one \\\"\"\":string", "  two:string",
                                      "  \"\"\":string", "tech:keyword", "\"t\":string"}));
  EXPECT_EQ(show("\"\"\"a\"\"\" b"), (std::vector<std::string>{"\"\"\"a\"\"\":string", "b:variableName"}));
}

TEST(Highlight, AnEscapedStrayByteInABlockStringDoesNotSwallowItsEnd) {
  EXPECT_EQ(show("\"\"\"x\\\xE2\"\"\" b"),
            (std::vector<std::string>{"\"\"\"x\\\xE2\"\"\":string", "b:variableName"}));
}

TEST(Highlight, OffsetsAreBytes) {
  auto tokens = highlight("é = x");
  ASSERT_EQ(tokens.size(), 2u);  // "é" is not a word character: only "=" and "x"
  EXPECT_EQ(tokens[0].from, 3u);
  EXPECT_EQ(tokens[1].from, 5u);
  EXPECT_EQ(tokens[1].kind, dd::dsl::TokenKind::Kind);
}

TEST(Highlight, JsonOffsetsCountUtf16Units) {
  // "😀" is four UTF-8 bytes and two UTF-16 units; "é" two bytes and one unit.
  dd::Json tokens = dd::dsl::highlight_json("model {\n  😀é = process\n}");
  ASSERT_TRUE(tokens.is_array());
  const auto& list = tokens.as_array();
  ASSERT_GE(list.size(), 3u);
  EXPECT_EQ(list[0].dump(), R"([0,5,"keyword"])");
  // `process` starts after "model {\n  " (10 units), "😀é" (3) and " = " (3).
  bool found = false;
  for (const auto& t : list)
    if (t.as_array()[2].as_string() == "typeName") {
      EXPECT_EQ(t.as_array()[0].as_number(), 16);
      EXPECT_EQ(t.as_array()[1].as_number(), 23);
      found = true;
    }
  EXPECT_TRUE(found);
  EXPECT_EQ(dd::dsl::highlight_json("").dump(), "[]");
}

}  // namespace
