#pragma once

// UTF-8 text helpers. The core works in UTF-8 byte offsets; JavaScript hosts and editors count
// UTF-16 code units, so the bindings convert at the edge with these.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>  // defines __GLIBC__ where it applies
#include <string>
#include <string_view>

namespace dd {

/// A character of ECMAScript `\s` in the C locale: space, \t, \n, \v, \f or \r.
[[nodiscard]] constexpr bool is_ascii_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

/// A character that `\w` matches, which `\b` treats as part of a word.
[[nodiscard]] constexpr bool is_ascii_word(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/// `c` in lowercase when it is an ASCII capital, else `c`.
[[nodiscard]] constexpr char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

/// Whether `text` starts with `prefix`, ignoring ASCII case.
[[nodiscard]] constexpr bool starts_with_ascii_icase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i)
    if (ascii_lower(text[i]) != ascii_lower(prefix[i])) return false;
  return true;
}

/// Whether `a` and `b` are equal, ignoring ASCII case.
[[nodiscard]] constexpr bool ascii_iequals(std::string_view a, std::string_view b) {
  return a.size() == b.size() && starts_with_ascii_icase(a, b);
}

/// ASCII whitespace plus the no-break space and BOM, as the DSL treats them.
[[nodiscard]] bool is_js_space(std::string_view s, std::size_t i, std::size_t* length);

/// `s` without leading and trailing whitespace (JavaScript `trim`, for the characters the DSL meets).
[[nodiscard]] std::string_view trim(std::string_view s);
[[nodiscard]] std::string_view trim_end(std::string_view s);

/// `std::from_chars` for a double. Apple's libc++ has the floating-point overload only from a
/// recent macOS, so a build for an older one (the Python wheels, macOS 13.3) uses
/// `from_chars_double_strtod` instead; CMake decides (DD_HAVE_FROM_CHARS_DOUBLE).
[[nodiscard]] std::from_chars_result from_chars_double(const char* first, const char* last, double& value);
#if defined(__APPLE__) || defined(__GLIBC__)
#define DD_HAVE_STRTOD_L 1
/// The same through `strtod_l` in the C locale, reading exactly what `std::from_chars` reads.
/// Exposed so the tests can compare the two where both exist.
[[nodiscard]] std::from_chars_result from_chars_double_strtod(const char* first, const char* last, double& value);
#endif

/// Number of UTF-16 code units the UTF-8 text encodes.
[[nodiscard]] std::size_t utf16_length(std::string_view utf8);

/// The UTF-16 offset of a byte offset in `utf8`.
[[nodiscard]] inline std::size_t utf16_offset(std::string_view utf8, std::size_t byte_offset) {
  return utf16_length(utf8.substr(0, byte_offset));
}

/// The byte offset of a UTF-16 offset in `utf8` (clamped to the end).
[[nodiscard]] std::size_t byte_offset(std::string_view utf8, std::size_t utf16_offset);

/// Decodes the UTF-8 code point at `i` and advances `i` past it (lenient: never fails).
std::uint32_t next_code_point(std::string_view s, std::size_t& i);
/// Appends a code point as UTF-8.
void append_utf8(std::string& out, std::uint32_t cp);
/// `s` with every byte that does not belong to a well-formed UTF-8 sequence (stray continuation
/// bytes, truncated or overlong sequences, surrogates, code points past U+10FFFF) replaced by
/// U+FFFD, one replacement per bad byte. Valid text comes back unchanged.
[[nodiscard]] std::string to_valid_utf8(std::string_view s);
/// JavaScript's `\s` (and what `trim` removes).
[[nodiscard]] bool is_js_whitespace(std::uint32_t cp);
/// JavaScript's `trim`, over its full whitespace set.
[[nodiscard]] std::string_view js_trim(std::string_view s);
/// `s` with its `\r\n` and lone `\r` line breaks (as Windows text and native multi-line fields give
/// them) turned into `\n`.
[[nodiscard]] std::string normalize_line_breaks(std::string_view s);
/// Collapses runs of JavaScript whitespace into one space: `s.replace(/\s+/g, ' ')`.
[[nodiscard]] std::string collapse_whitespace(std::string_view s);

/// JavaScript's `Number(string)`: trims, `""` is 0, hex/octal/binary prefixes, else NaN.
[[nodiscard]] double js_to_number(std::string_view s);
/// ECMAScript ToInt32, for `x & 1`: NaN and infinities are 0, the rest wraps modulo 2^32.
[[nodiscard]] std::int32_t js_to_int32(double d);

/// `text` escaped for XML text or a double-quoted attribute value: `&`, `<`, `>` and `"`.
[[nodiscard]] std::string escape_xml(std::string_view text);

}  // namespace dd
