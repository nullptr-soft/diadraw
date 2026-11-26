#include "diadraw/base/text.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <limits>

#if defined(__APPLE__)
#include <xlocale.h>
#elif defined(DD_HAVE_STRTOD_L)
#include <locale.h>
#endif

namespace dd {

#if defined(DD_HAVE_STRTOD_L)
namespace {

bool is_number_char(char c) {
  return (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
}
bool is_word_char(char c) {  // `inf`, `infinity`, `nan`, `nan(n-char-sequence)`
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '(' ||
         c == ')';
}

/// Restores `errno` on every return: std::from_chars never touches it.
struct KeepErrno {
  int saved = errno;
  ~KeepErrno() { errno = saved; }
};

}  // namespace

std::from_chars_result from_chars_double_strtod(const char* first, const char* last, double& value) {
  KeepErrno keep;  // first, so nothing below (newlocale, an allocation) leaks an errno either
  // What from_chars reads: an optional '-', then digits, '.' and an exponent, or a word. Cutting
  // the text there keeps strtod from reading what from_chars would not (whitespace, '+', hex).
  if (first == last || *first == '+') return {first, std::errc::invalid_argument};
  const char* body = first + (*first == '-' ? 1 : 0);
  const bool word = body != last && (*body == 'i' || *body == 'I' || *body == 'n' || *body == 'N');
  const char* stop = body;
  while (stop != last && (word ? is_word_char(*stop) : is_number_char(*stop))) ++stop;

  // strtod needs a terminated string; numbers fit on the stack.
  char small[64];
  std::string large;
  const auto length = static_cast<std::size_t>(stop - first);
  const char* text = small;
  if (length < sizeof small) {
    std::copy(first, stop, small);
    small[length] = '\0';
  } else {
    large.assign(first, stop);
    text = large.c_str();
  }

  static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
  errno = 0;
  char* end = nullptr;
  const double parsed = strtod_l(text, &end, c_locale);
  const auto read = static_cast<std::size_t>(end - text);
  if (read == 0) return {first, std::errc::invalid_argument};
  // strtod reports ERANGE for subnormal results too, which from_chars reads; only overflow and
  // underflow to zero are out of range.
  if (errno == ERANGE && !(parsed != 0 && std::isfinite(parsed)))
    return {first + read, std::errc::result_out_of_range};  // value untouched
  value = parsed;
  return {first + read, std::errc()};
}
#endif

std::from_chars_result from_chars_double(const char* first, const char* last, double& value) {
#if defined(DD_HAVE_FROM_CHARS_DOUBLE)
  return std::from_chars(first, last, value);
#elif defined(DD_HAVE_STRTOD_L)
  return from_chars_double_strtod(first, last, value);
#else
#error "Neither std::from_chars for double nor strtod_l is available"
#endif
}

bool is_js_space(std::string_view s, std::size_t i, std::size_t* length) {
  char c = s[i];
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
    *length = 1;
    return true;
  }
  if (s.compare(i, 2, "\xC2\xA0") == 0) {
    *length = 2;
    return true;
  }
  if (s.compare(i, 3, "\xEF\xBB\xBF") == 0) {
    *length = 3;
    return true;
  }
  return false;
}

std::string_view trim_end(std::string_view s) {
  while (!s.empty()) {
    std::size_t len = 0;
    char c = s.back();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
      s.remove_suffix(1);
    } else if (s.size() >= 2 && is_js_space(s, s.size() - 2, &len) && len == 2) {
      s.remove_suffix(2);
    } else if (s.size() >= 3 && is_js_space(s, s.size() - 3, &len) && len == 3) {
      s.remove_suffix(3);
    } else {
      break;
    }
  }
  return s;
}

std::string_view trim(std::string_view s) {
  std::size_t len = 0;
  while (!s.empty() && is_js_space(s, 0, &len)) s.remove_prefix(len);
  return trim_end(s);
}

std::size_t utf16_length(std::string_view utf8) {
  std::size_t units = 0;
  for (std::size_t i = 0; i < utf8.size();) {
    auto c = static_cast<unsigned char>(utf8[i]);
    std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    units += len == 4 ? 2 : 1;
    i += len;
  }
  return units;
}

std::size_t byte_offset(std::string_view utf8, std::size_t utf16_offset) {
  std::size_t units = 0;
  std::size_t i = 0;
  while (i < utf8.size() && units < utf16_offset) {
    auto c = static_cast<unsigned char>(utf8[i]);
    std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    units += len == 4 ? 2 : 1;
    i += len;
  }
  return i < utf8.size() ? i : utf8.size();
}

std::uint32_t next_code_point(std::string_view s, std::size_t& i) {
  auto c = static_cast<unsigned char>(s[i]);
  std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  std::uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
  for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
  i += len;
  return cp;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string to_valid_utf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    // The well-formed sequences of Unicode's table 3-7: the lead byte fixes the length and the
    // range of the second byte; the other continuation bytes are 80..BF.
    std::size_t len = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2;
    } else if (c == 0xE0) {
      len = 3;
      lo = 0xA0;
    } else if (c == 0xED) {
      len = 3;
      hi = 0x9F;
    } else if (c >= 0xE1 && c <= 0xEF) {
      len = 3;
    } else if (c == 0xF0) {
      len = 4;
      lo = 0x90;
    } else if (c == 0xF4) {
      len = 4;
      hi = 0x8F;
    } else if (c >= 0xF1 && c <= 0xF3) {
      len = 4;
    }
    bool ok = len > 0 && i + len <= s.size();
    for (std::size_t k = 1; ok && k < len; ++k) {
      auto b = static_cast<unsigned char>(s[i + k]);
      ok = k == 1 ? (b >= lo && b <= hi) : (b >= 0x80 && b <= 0xBF);
    }
    if (ok) {
      out.append(s.substr(i, len));
      i += len;
    } else {
      out += "\xEF\xBF\xBD";
      ++i;
    }
  }
  return out;
}

bool is_js_whitespace(std::uint32_t cp) {
  return (cp >= 0x09 && cp <= 0x0D) || cp == 0x20 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
         cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF;
}

std::string_view js_trim(std::string_view s) {
  std::size_t start = 0;
  while (start < s.size()) {
    std::size_t i = start;
    if (!is_js_whitespace(next_code_point(s, i))) break;
    start = i;
  }
  std::size_t end = s.size();
  while (end > start) {
    std::size_t back = end - 1;
    while (back > start && (static_cast<unsigned char>(s[back]) & 0xC0) == 0x80) --back;
    std::size_t i = back;
    if (!is_js_whitespace(next_code_point(s, i))) break;
    end = back;
  }
  return s.substr(start, end - start);
}

std::string normalize_line_breaks(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\r') {
      out.push_back(s[i]);
    } else {
      out.push_back('\n');
      if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
    }
  }
  return out;
}

std::string collapse_whitespace(std::string_view s) {
  std::string out;
  bool in_run = false;
  for (std::size_t i = 0; i < s.size();) {
    std::size_t start = i;
    if (is_js_whitespace(next_code_point(s, i))) {
      if (!in_run) out.push_back(' ');
      in_run = true;
    } else {
      out.append(s, start, i - start);
      in_run = false;
    }
  }
  return out;
}

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

double js_to_number(std::string_view raw) {
  std::string_view s = js_trim(raw);
  if (s.empty()) return 0;
  if (s == "Infinity" || s == "+Infinity") return kInf;
  if (s == "-Infinity") return -kInf;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'o' || s[1] == 'O' || s[1] == 'b' || s[1] == 'B')) {
    int base = (s[1] == 'x' || s[1] == 'X') ? 16 : (s[1] == 'o' || s[1] == 'O') ? 8 : 2;
    double v = 0;
    for (char c : s.substr(2)) {
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
      if (d >= base) return std::nan("");
      v = v * base + d;
    }
    return v;
  }
  std::string_view body = s;
  bool negative = false;
  if (body.front() == '+' || body.front() == '-') {
    negative = body.front() == '-';
    body.remove_prefix(1);
  }
  // StrUnsignedDecimalLiteral: digits [. digits] [e[+-]digits], with at least one digit.
  bool digit = false;
  for (char c : body) {
    if (c >= '0' && c <= '9') digit = true;
    else if (c != '.' && c != 'e' && c != 'E' && c != '+' && c != '-') return std::nan("");
  }
  if (!digit || body.front() == 'e' || body.front() == 'E') return std::nan("");
  double v = 0;
  auto [end, ec] = from_chars_double(body.data(), body.data() + body.size(), v);
  if (ec == std::errc::result_out_of_range) v = kInf;
  else if (ec != std::errc() || end != body.data() + body.size()) return std::nan("");
  return negative ? -v : v;
}

std::int32_t js_to_int32(double d) {
  if (!std::isfinite(d)) return 0;
  double m = std::fmod(std::trunc(d), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(m));
}

}  // namespace dd
