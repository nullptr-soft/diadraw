// The JavaScript regex engine, against results V8 gives for the same patterns.

#include <gtest/gtest.h>

#include "diadraw/base/js_regex.hpp"

using dd::js::Regex;

TEST(JsRegex, GroupsAndUndefined) {
  Regex re("^(a)?(b+)(c)?$");
  auto m = re.exec("bb");
  ASSERT_TRUE(m);
  EXPECT_FALSE(m->has(1));
  EXPECT_EQ(m->str(2), "bb");
  EXPECT_FALSE(m->has(3));
}

TEST(JsRegex, LazyGreedyAndBacktracking) {
  EXPECT_EQ(Regex("<(.+?)>").exec("<a><b>")->str(1), "a");
  EXPECT_EQ(Regex("<(.+)>").exec("<a><b>")->str(1), "a><b");
  EXPECT_EQ(Regex("^(\\w+)\\s*:\\s*(.*?)\\s*$").exec("key :  value  ")->str(2), "value");
  EXPECT_EQ(Regex("(x+x+)+y").exec("xxxxy")->str(0), "xxxxy");
}

TEST(JsRegex, RepeatedGroupCapturesReset) {
  // /(?:(a)|b)+/.exec("ab") → [ 'ab', undefined ]
  auto m = Regex("(?:(a)|b)+").exec("ab");
  ASSERT_TRUE(m);
  EXPECT_FALSE(m->has(1));
}

TEST(JsRegex, UnicodeClassesAndWhitespace) {
  Regex name("^[\\p{L}\\p{N}_.]+$", "u");
  EXPECT_TRUE(name.test("Пример_1.x"));
  EXPECT_TRUE(name.test("名前"));
  EXPECT_FALSE(name.test("a→b"));
  EXPECT_TRUE(Regex("^\\s+$").test("\xC2\xA0\t\xE3\x80\x80"));  // NBSP, tab, ideographic space
  EXPECT_FALSE(Regex("^.$").test("\n"));
  EXPECT_TRUE(Regex("^.$", "s").test("\n"));
}

TEST(JsRegex, CaseInsensitiveAndBoundaries) {
  EXPECT_TRUE(Regex("^skinparam\\b", "i").test("SkinParam x"));
  EXPECT_FALSE(Regex("^skinparam\\b", "i").test("skinparams"));
  EXPECT_TRUE(Regex("[a-z]+", "i").test("ABC"));
}

TEST(JsRegex, Lookaround) {
  Regex re("(?<![\\w$])FOO(?![\\w])");
  EXPECT_EQ(re.replace("FOO $FOO aFOO FOO_ (FOO)", "X", true), "X $FOO aFOO FOO_ (X)");
  EXPECT_EQ(Regex("a(?=b)").exec("acab")->index(), 2u);
}

TEST(JsRegex, ReplaceAndSplit) {
  EXPECT_EQ(Regex("(\\w+)@(\\w+)").replace("me@host", "$2 at $1 ($&) $$", false), "host at me (me@host) $");
  EXPECT_EQ(Regex("a*").replace("baaac", "-", true), "-b--c-");
  auto parts = Regex("\\r?\\n").split("a\r\nb\nc");
  EXPECT_EQ(parts, (std::vector<std::string>{"a", "b", "c"}));
  auto with = Regex("(-)|,").split_with_groups("a-b,c");
  ASSERT_EQ(with.size(), 5u);
  EXPECT_EQ(with[1], std::optional<std::string>("-"));
  EXPECT_FALSE(with[3].has_value());
  EXPECT_EQ(Regex("").split("abc"), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(JsRegex, MatchAllAdvancesPastEmptyMatches) {
  auto all = Regex("\\d*").match_all("1a22");
  ASSERT_EQ(all.size(), 4u);  // "1", "", "22", ""
  EXPECT_EQ(all[2].str(), "22");
}
