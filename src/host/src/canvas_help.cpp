#include "host/canvas_help.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

#include "ascii.hpp"
#include "canvas_help_data.hpp"

namespace dd::host {

using detail::take;
using detail::trim_ascii;

std::vector<HelpSection> parse_canvas_help(std::string_view text, HelpKeys keys) {
  const auto column = static_cast<std::size_t>(keys) + 1;  // after the action
  std::vector<HelpSection> sections;
  while (!text.empty()) {
    std::string_view line = trim_ascii(take(text, '\n'));
    if (line.empty() || line.starts_with("//")) continue;
    if (line.starts_with('#')) {
      std::string_view title = trim_ascii(line.substr(1));
      if (!title.empty()) sections.push_back({std::string(title), {}});
      continue;
    }
    if (sections.empty() || std::count(line.begin(), line.end(), '\t') != 3) continue;  // action, mac, pc, windows
    std::array<std::string_view, 4> fields;
    for (auto& field : fields) field = trim_ascii(take(line, '\t'));
    if (fields[0].empty()) continue;
    // `=` repeats the column before; the mac column has none before it.
    std::size_t from = column;
    while (from > 1 && fields[from] == "=") --from;
    std::string_view key = fields[from];
    if (key.empty() || key == "-" || key == "=") continue;
    sections.back().entries.push_back({std::string(key), std::string(fields[0])});
  }
  std::erase_if(sections, [](const HelpSection& s) { return s.entries.empty(); });
  return sections;
}

const std::vector<HelpSection>& canvas_help(HelpKeys keys) {
  static const std::array<std::vector<HelpSection>, 3> lists = {
      parse_canvas_help(detail::canvas_help_text, HelpKeys::mac),
      parse_canvas_help(detail::canvas_help_text, HelpKeys::pc),
      parse_canvas_help(detail::canvas_help_text, HelpKeys::windows),
  };
  return lists[static_cast<std::size_t>(keys)];
}

}  // namespace dd::host
