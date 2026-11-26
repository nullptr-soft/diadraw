// UTF-8 repair (base/text.hpp): valid text is kept byte for byte, and every byte outside a
// well-formed sequence becomes U+FFFD. Line breaks of every kind become `\n`.

#include <gtest/gtest.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "diadraw/base/text.hpp"

using dd::normalize_line_breaks;
using dd::to_valid_utf8;

namespace {

const std::string kReplacement = "\xEF\xBF\xBD";

TEST(ToValidUtf8, KeepsValidText) {
  for (const std::string& s : {std::string(), std::string("plain ascii"), std::string("Zo\xC3\xAB"),
                              std::string("\xE2\x82\xAC 5"), std::string("\xF0\x9F\x98\x80"),
                              std::string("\xF4\x8F\xBF\xBF"), std::string("a\0b", 3)})
    EXPECT_EQ(to_valid_utf8(s), s);
}

TEST(ToValidUtf8, ReplacesEachBadByte) {
  EXPECT_EQ(to_valid_utf8("Zo\xEB"), "Zo" + kReplacement);                     // Latin-1
  EXPECT_EQ(to_valid_utf8("\xFF\xFE"), kReplacement + kReplacement);           // never valid
  EXPECT_EQ(to_valid_utf8("\x80x"), kReplacement + "x");                       // stray continuation
  EXPECT_EQ(to_valid_utf8("\xE2\x82"), kReplacement + kReplacement);           // truncated
  EXPECT_EQ(to_valid_utf8("\xC0\xAF"), kReplacement + kReplacement);           // overlong
  EXPECT_EQ(to_valid_utf8("\xE0\x80\xAF"), kReplacement + kReplacement + kReplacement);
  EXPECT_EQ(to_valid_utf8("\xED\xA0\x80"), kReplacement + kReplacement + kReplacement);  // surrogate
  EXPECT_EQ(to_valid_utf8("\xF4\x90\x80\x80"), kReplacement + kReplacement + kReplacement + kReplacement);
  EXPECT_EQ(to_valid_utf8("\xC3\xAB\xEB\xC3\xAB"), "\xC3\xAB" + kReplacement + "\xC3\xAB");
}

TEST(NormalizeLineBreaks, TurnsEveryLineBreakIntoLf) {
  EXPECT_EQ(normalize_line_breaks("a\r\nb\rc\nd"), "a\nb\nc\nd");
  EXPECT_EQ(normalize_line_breaks("\r"), "\n");
  EXPECT_EQ(normalize_line_breaks("\r\r\n"), "\n\n");
  EXPECT_EQ(normalize_line_breaks(""), "");
}

}  // namespace

// from_chars_double, and the strtod_l path builds for older macOS versions use, read numbers the
// same way as std::from_chars.
TEST(Text, FromCharsDouble) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
  struct Case {
    std::string text;
    std::errc ec;
    std::size_t read;
    double value;
  };
  const Case cases[] = {
      {"1.5", std::errc(), 3, 1.5},
      {"-0.25e2", std::errc(), 7, -25},
      {"42abc", std::errc(), 2, 42},
      {".5", std::errc(), 2, 0.5},
      {"1e", std::errc(), 1, 1},
      {"1e+", std::errc(), 1, 1},
      {"0x10", std::errc(), 1, 0},
      {"inf", std::errc(), 3, kInf},
      {"-Infinity", std::errc(), 9, -kInf},
      {"infx", std::errc(), 3, kInf},
      {"nan", std::errc(), 3, kNaN},
      {"nan(1)x", std::errc(), 6, kNaN},
      {"1e-310", std::errc(), 6, 1e-310},      // subnormal: strtod says ERANGE, from_chars reads it
      {"4.9e-324", std::errc(), 8, 4.9e-324},
      {"1e999", std::errc::result_out_of_range, 5, 7},
      {"1e-400", std::errc::result_out_of_range, 6, 7},
      {"+1", std::errc::invalid_argument, 0, 7},
      {" 1", std::errc::invalid_argument, 0, 7},
      {"in", std::errc::invalid_argument, 0, 7},
      {"", std::errc::invalid_argument, 0, 7},
      {"-", std::errc::invalid_argument, 0, 7},
      {"--1", std::errc::invalid_argument, 0, 7},
  };
  using Parse = std::from_chars_result (*)(const char*, const char*, double&);
  std::vector<std::pair<const char*, Parse>> parsers{{"from_chars_double", &dd::from_chars_double}};
#if defined(DD_HAVE_STRTOD_L)
  parsers.emplace_back("from_chars_double_strtod", &dd::from_chars_double_strtod);
#endif
  for (const auto& c : cases) {
    const char* first = c.text.data();
    const char* last = first + c.text.size();
    for (const auto& [name, parse] : parsers) {
      SCOPED_TRACE(c.text + " (" + name + ")");
      double v = 7;
      errno = 12345;
      const auto r = parse(first, last, v);
      EXPECT_EQ(errno, 12345);  // like std::from_chars, errno is left alone
      EXPECT_EQ(r.ec, c.ec);
      if (c.ec != std::errc::invalid_argument) {
        EXPECT_EQ(static_cast<std::size_t>(r.ptr - first), c.read);
      }
      if (std::isnan(c.value)) {
        EXPECT_TRUE(std::isnan(v));
      } else {
        EXPECT_EQ(v, c.value);
      }
    }
  }
}
