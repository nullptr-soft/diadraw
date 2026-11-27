#pragma once

// Text helpers shared by the host's sources (not part of its interface).

#include <string_view>

namespace dd::host::detail {

/// `s` without ASCII blanks (space, tab, CR, LF) at either end. Unlike dd::trim it leaves other
/// white space, so the rules match the web app's (field-editors.ts, swatches.ts).
[[nodiscard]] inline std::string_view trim_ascii(std::string_view s) {
  auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && blank(s.front())) s.remove_prefix(1);
  while (!s.empty() && blank(s.back())) s.remove_suffix(1);
  return s;
}

/// The next field of `s` up to `sep` (the rest when there is none), taken off `s`.
[[nodiscard]] inline std::string_view take(std::string_view& s, char sep) {
  auto end = s.find(sep);
  std::string_view field = s.substr(0, end);
  s = end == std::string_view::npos ? std::string_view() : s.substr(end + 1);
  return field;
}

}  // namespace dd::host::detail
