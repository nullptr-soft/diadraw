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

using dd::dsl::fold_ranges;

/// "start-end" lines for each fold range, and the hidden text checked against the lines.
std::vector<std::string> folds(std::string_view text) {
  std::vector<std::string> out;
  for (const auto& f : fold_ranges(text)) {
    EXPECT_EQ(text[f.open], '{');
    EXPECT_EQ(text[f.close], '}');
    EXPECT_LT(f.from, f.to);
    out.push_back(std::to_string(f.start_line) + "-" + std::to_string(f.end_line));
  }
  return out;
}

TEST(Fold, ModelViewAndContainers) {
  std::string_view text =
      "model {\n"
      "  shop = system \"Shop\" {\n"
      "    web = container \"Web\"\n"
      "    db = container \"DB\" { tag x }\n"
      "  }\n"
      "}\n"
      "view v of shop {\n"
      "  include *\n"
      "}\n";
  EXPECT_EQ(folds(text), (std::vector<std::string>{"0-4", "1-3", "6-7"}));
  auto ranges = fold_ranges(text);
  // The hidden text runs from the end of the `{` line to the end of the line before the `}`.
  EXPECT_EQ(text.substr(ranges[1].from, ranges[1].to - ranges[1].from),
            "\n    web = container \"Web\"\n    db = container \"DB\" { tag x }");
  // As whole lines: the `{` line and the `}` line stay.
  EXPECT_EQ(text.substr(ranges[1].line, ranges[1].first - ranges[1].line), "  shop = system \"Shop\" {\n");
  EXPECT_EQ(text.substr(ranges[1].first, ranges[1].last - ranges[1].first),
            "    web = container \"Web\"\n    db = container \"DB\" { tag x }\n");
}

TEST(Fold, SkipsShortBlocksStringsAndComments) {
  EXPECT_EQ(folds("model {\n}\nview v {\n  include *\n}"), (std::vector<std::string>{"2-3"}));
  EXPECT_EQ(folds("model {\n  a = process \"{\"\n  // }\n  /* } */\n}"), (std::vector<std::string>{"0-3"}));
  EXPECT_EQ(folds("a = process \"\"\"\n{\n\n}\n\"\"\""), (std::vector<std::string>{}));
}

TEST(Fold, OneRangePerLineAndUnmatchedBraces) {
  // Two blocks open on one line: the outer one folds.
  EXPECT_EQ(folds("model { g = group {\n  a = process\n}\n}"), (std::vector<std::string>{"0-2"}));
  // `}}` is one token of two braces.
  EXPECT_EQ(folds("model {\n  s = system {\n    a = process\n  }}\n"), (std::vector<std::string>{"0-2", "1-2"}));
  // A stray `}` and an unclosed `{` are skipped.
  EXPECT_EQ(folds("}\nmodel {\n  a = process\n}\nview v {\n  include *\n"), (std::vector<std::string>{"1-2"}));
  EXPECT_EQ(folds(""), (std::vector<std::string>{}));
}

TEST(Fold, CrlfAndJson) {
  std::string_view text = "model {\r\n  a = process\r\n}\r\n";
  auto ranges = fold_ranges(text);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_EQ(text.substr(ranges[0].from, ranges[0].to - ranges[0].from), "\r\n  a = process");
  EXPECT_EQ(text.substr(ranges[0].first, ranges[0].last - ranges[0].first), "  a = process\r\n");
  // A lone \r ends a line too, as in `highlight`.
  EXPECT_EQ(folds("model {\r  a = process\r}"), (std::vector<std::string>{"0-1"}));
  // UTF-16 offsets: é is one unit, 😀 two.
  EXPECT_EQ(dd::dsl::fold_ranges_json("é😀 {\n  a = process\n}").dump(),
            R"([{"from":5,"to":19,"startLine":0,"endLine":1}])");
}

}  // namespace
