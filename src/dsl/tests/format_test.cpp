// Formatting source text (format_source): indentation, spacing and blank lines change, the tokens
// and the lines they are on do not.

#include "diadraw/dsl/format.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/dsl/syntax.hpp"

namespace {

using dd::dsl::format_source;
using dd::dsl::FormatOptions;

std::string fmt(std::string_view text, int indent = 2) { return format_source(text, FormatOptions{indent}); }

/// The text of every token and comment, in order: what formatting must not change.
std::vector<std::string> tokens(const std::string& text) {
  std::vector<std::string> out;
  // The lexer's tokens: parse errors can span several tokens and the whitespace between them.
  auto tree = dd::dsl::lex_source(text);
  for (const auto& t : tree.tokens) out.push_back(text.substr(t.from, t.to - t.from));
  // Formatting drops the spaces at the end of a line, line comments' too.
  for (const auto& c : tree.comments) {
    std::string comment = text.substr(c.from, c.to - c.from);
    if (!c.block) comment.erase(comment.find_last_not_of(" \t\r") + 1);
    out.push_back(std::move(comment));
  }
  return out;
}

TEST(Format, IndentsByBraceDepth) {
  EXPECT_EQ(fmt("model {\nshop = system \"Shop\" {\n      web = container \"Web\"\n}\n}\n"),
            "model {\n  shop = system \"Shop\" {\n    web = container \"Web\"\n  }\n}\n");
}

TEST(Format, IndentSizeIsTheOption) {
  const std::string text = "model {\n a = process\n b = system {\n c = container\n }\n}\n";
  EXPECT_EQ(fmt(text, 4), "model {\n    a = process\n    b = system {\n        c = container\n    }\n}\n");
  EXPECT_EQ(fmt(text, 1), "model {\n a = process\n b = system {\n  c = container\n }\n}\n");
  // Out of range sizes are clamped.
  EXPECT_EQ(fmt("model {\na = process\n}\n", 0), "model {\n a = process\n}\n");
  EXPECT_EQ(fmt("model {\na = process\n}\n", 40), "model {\n        a = process\n}\n");
}

TEST(Format, SpacesTokens) {
  EXPECT_EQ(fmt("model{\n  a=process   \"A\"tech\"T\"\n  a->b\"calls\"\n  x = a-->b\n}\n"),
            "model {\n  a = process \"A\" tech \"T\"\n  a -> b \"calls\"\n  x = a --> b\n}\n");
  EXPECT_EQ(fmt("view v {\n  pin a at 10 ,20 size 30,40\n  route \"a->b\" via 1,2 3 ,4\n}\n"),
            "view v {\n  pin a at 10, 20 size 30, 40\n  route \"a->b\" via 1, 2 3, 4\n}\n");
  // `.` and `*` keep their spacing.
  EXPECT_EQ(fmt("model {\n  shop.web -> shop.db\n}\nview v of shop {\n  include  *\n}\n"),
            "model {\n  shop.web -> shop.db\n}\nview v of shop {\n  include *\n}\n");
}

TEST(Format, OneLineBlocks) {
  EXPECT_EQ(fmt("style container {fill \"#fff\"}\nmodel {}\nmodel { }\n"),
            "style container { fill \"#fff\" }\nmodel {}\nmodel { }\n");
}

TEST(Format, BlankLinesAndTrailingSpace) {
  EXPECT_EQ(fmt("\n\nmodel {   \n\n  a = process  \n\n\n\n  b = process\n\n}\n\n\n\nview v {\n}\n\n"),
            "model {\n  a = process\n\n  b = process\n}\n\nview v {\n}\n");
}

TEST(Format, Comments) {
  EXPECT_EQ(fmt("model {\n// first\n  a = process     // trailing   \n      /* block */ b = process\n}\n"),
            "model {\n  // first\n  a = process // trailing\n  /* block */ b = process\n}\n");
  // A block comment's later lines move with its first.
  EXPECT_EQ(fmt("model {\n      /* one\n         two */\n}\n"), "model {\n  /* one\n     two */\n}\n");
}

TEST(Format, ContinuedStatementsIndentOneMore) {
  EXPECT_EQ(fmt("model {\n  a = container \"A\"\n  tech \"T\"\n      tag \"x\"\n  b = process\n}\n"),
            "model {\n  a = container \"A\"\n    tech \"T\"\n    tag \"x\"\n  b = process\n}\n");
}

TEST(Format, BlockStringsMoveWithTheirLine) {
  const std::string text =
      "model {\n"
      "        db = container \"DB\" description \"\"\"\n"
      "          Orders and their lines.\n"
      "\n"
      "            Archived after two years.\n"
      "        \"\"\"\n"
      "}\n";
  const std::string formatted = fmt(text);
  EXPECT_EQ(formatted,
            "model {\n"
            "  db = container \"DB\" description \"\"\"\n"
            "    Orders and their lines.\n"
            "\n"
            "      Archived after two years.\n"
            "  \"\"\"\n"
            "}\n");
  auto before = dd::dsl::parse(text), after = dd::dsl::parse(formatted);
  ASSERT_NE(before.model.elements.find("db"), nullptr);
  ASSERT_NE(after.model.elements.find("db"), nullptr);
  EXPECT_EQ(after.model.elements.find("db")->description, before.model.elements.find("db")->description);
  EXPECT_EQ(before.model.elements.find("db")->description, "Orders and their lines.\n\n  Archived after two years.");
}

TEST(Format, BlockStringsThatCannotMoveStay) {
  // Text on the opening line counts towards the shared indentation, so the lines stay.
  const std::string text = "model {\n    a = process description \"\"\"first\n      second\"\"\"\n}\n";
  EXPECT_EQ(fmt(text), "model {\n  a = process description \"\"\"first\n      second\"\"\"\n}\n");
  // A line with too little indentation to move left: nothing moves.
  const std::string shallow = "model {\n        a = process description \"\"\"\n  x\n        \"\"\"\n}\n";
  EXPECT_EQ(fmt(shallow), "model {\n  a = process description \"\"\"\n  x\n        \"\"\"\n}\n");
}

TEST(Format, UnclosedTextIsKept) {
  EXPECT_EQ(fmt("model {\n      a = process description \"\"\"\n   never closed  \n"),
            "model {\n  a = process description \"\"\"\n   never closed  \n");
  EXPECT_EQ(fmt("model {\n   /* open\n"), "model {\n  /* open\n");
  // An unclosed string runs to the end of its line, spaces included.
  EXPECT_EQ(fmt("model {\n      a = process \"unclosed   \n}\n"), "model {\n  a = process \"unclosed   \n}\n");
}

TEST(Format, TextThatDoesNotParse) {
  EXPECT_EQ(fmt("model {\n      a = = \n   }}}\nview v {\n include *\n}\n"),
            "model {\n  a = =\n}}}\nview v {\n  include *\n}\n");
}

/// Formats `text` and checks what must hold for any input: the same model and diagnostics, the
/// same tokens, and nothing more to change the second time.
void expect_safe(const std::string& text) {
  const std::string formatted = fmt(text);
  auto before = dd::dsl::parse(text), after = dd::dsl::parse(formatted);
  EXPECT_EQ(dd::dsl::print(after.model), dd::dsl::print(before.model)) << text;
  EXPECT_EQ(after.diagnostics.size(), before.diagnostics.size()) << text;
  EXPECT_EQ(tokens(formatted), tokens(text)) << text;
  EXPECT_EQ(fmt(formatted), formatted) << text;
}

TEST(Format, BrokenTextKeepsItsMeaning) {
  // A `\` ending an unclosed string escapes the line break: nothing may move it next to one.
  expect_safe("model {\n  a = process \"abc\\ \n  b = process \"B\"\n}\n");
  expect_safe("model {\n  a = process \"abc\\\t\n  b = process \"B\"\n}\n");
  expect_safe("model {\n  a = process \"x\\");
  EXPECT_EQ(fmt("model {\n  a = process \"x\\"), "model {\n  a = process \"x\\");
  // The same with `\r\n` line endings, and mixed ones.
  expect_safe("model {\r\n  a = process \"unclosed  \r\n  b = process\r\n}\r\n");
  expect_safe("model {\r\n  a = process \"abc\\\r\n  b = process \"B\"\r\n}\r\n");
  expect_safe("model {\n  a = process \"unclosed  \r\n  b = process\r\n}\r\n");
  expect_safe("model {\n  a = process \"abc\\\r\n  b = process \"B\"\r\n}\r\n");
  EXPECT_EQ(fmt("model {\r\n  a = process \"unclosed  \r\n  b = process\r\n}\r\n"),
            "model {\r\n  a = process \"unclosed  \r\n  b = process\r\n}\r\n");
  // Line endings come from the first line the output keeps, not from a dropped blank line.
  expect_safe("\n/* a\r\n b */\r\nmodel {\r\n}\r\n");
  expect_safe("\nmodel { a = process \"u\\\r\n  b = process \"B\"\r\n}\r\n");
  expect_safe("\n  a = process \"u\r\n  b = process\r\n");
  // Cut or stray UTF-8.
  expect_safe("model {\n  a = process\n}\n\xF0");
  expect_safe("model {\n  a = process \xF0\n  b = process\n}\n");
  expect_safe("model {\n  a = process \x80 \"B\"\n}\n");
  // A lone `\r` in a block string's line is no blank line to lowering.
  expect_safe("model {\n        a = process description \"\"\"\n          one\n\r   \n        \"\"\"\n}\n");
  // An unclosed block comment or block string runs to the end.
  expect_safe("model {\n      /* open\n  a = process\n");
}

TEST(Format, NoBlankLineAfterABraceAndAComment) {
  EXPECT_EQ(fmt("model { // the model\n\n  a = process\n}\n"), "model { // the model\n  a = process\n}\n");
  EXPECT_EQ(fmt("model { /* the model */\n\n  a = process\n}\n"), "model { /* the model */\n  a = process\n}\n");
}

TEST(Format, LineEndingsAndByteOrderMark) {
  EXPECT_EQ(fmt("model {\r\n a = process\r\n}\r\n"), "model {\r\n  a = process\r\n}\r\n");
  EXPECT_EQ(fmt("\xEF\xBB\xBFmodel {\n a = process\n}"), "\xEF\xBB\xBFmodel {\n  a = process\n}\n");
  EXPECT_EQ(fmt(""), "");
  EXPECT_EQ(fmt("  \n\n "), "");
}

TEST(Format, SequenceFrames) {
  EXPECT_EQ(fmt("view s {\nsequence {\nalt \"ok\" {\na -> b \"x\"\n} else \"no\" {\nb -> a \"y\"\n}\n}\n}\n"),
            "view s {\n  sequence {\n    alt \"ok\" {\n      a -> b \"x\"\n    } else \"no\" {\n      b -> a \"y\"\n    }\n  }\n}\n");
}

TEST(Format, TheGuidesExample) {
  // docs/guide/formatting.md shows this before and after.
  EXPECT_EQ(fmt("model {\ncustomer=person \"Customer\"\n   shop = system \"Shop\" {\n  web = container \"Web app\"   "
                "tech \"React\"\n      }\ncustomer->shop.web \"places order\"   // the main flow\n}\n"),
            "model {\n  customer = person \"Customer\"\n  shop = system \"Shop\" {\n    web = container \"Web app\" "
            "tech \"React\"\n  }\n  customer -> shop.web \"places order\" // the main flow\n}\n");
}

// ── The sample documents ─────────────────────────────────────────────────────────────────────

std::vector<std::string> corpus() {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(DIADRAW_CORPUS_DIR))
    if (entry.path().extension() == ".draw") files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> out;
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    out.push_back(text.str());
  }
  return out;
}

TEST(Format, SampleDocumentsKeepTheirMeaning) {
  const auto files = corpus();
  ASSERT_FALSE(files.empty());
  for (const auto& text : files) {
    auto before = dd::dsl::parse(text);
    const std::string canonical = dd::dsl::print(before.model);
    for (int indent : {1, 2, 4, 8}) {
      const std::string formatted = fmt(text, indent);
      auto after = dd::dsl::parse(formatted);
      EXPECT_EQ(dd::dsl::print(after.model), canonical) << text;
      EXPECT_EQ(after.diagnostics.size(), before.diagnostics.size()) << text;
      EXPECT_EQ(tokens(formatted), tokens(text)) << text;
      EXPECT_EQ(fmt(formatted, indent), formatted) << "not idempotent at indent " << indent;
      EXPECT_LE(std::count(formatted.begin(), formatted.end(), '\n'), std::count(text.begin(), text.end(), '\n') + 1);
    }
    // The canonical printer's text is already formatted at the default indentation.
    EXPECT_EQ(fmt(canonical), canonical);
  }
}

}  // namespace
