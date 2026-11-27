#include "host/credits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <utility>

#include "ascii.hpp"
#include "credits_data.hpp"

namespace dd::host {

using detail::take;
using detail::trim_ascii;

namespace {

constexpr std::size_t kFields = 5;  // name, version, licence, address, apps

}  // namespace

std::vector<Credit> parse_credits(std::string_view text) {
  std::vector<Credit> credits;
  while (!text.empty()) {
    std::string_view line = take(text, '\n');
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (trim_ascii(line).empty() || trim_ascii(line).starts_with("//")) continue;
    // Fields are what the tabs separate, so a line ending in a tab has an empty last field.
    std::array<std::string_view, kFields> fields;
    std::size_t count = 1 + static_cast<std::size_t>(std::count(line.begin(), line.end(), '\t'));
    if (count < kFields) continue;
    for (auto& field : fields) field = trim_ascii(take(line, '\t'));
    if (fields[0].empty()) continue;
    Credit credit{std::string(fields[0]), std::string(fields[1]), std::string(fields[2]), std::string(fields[3]), {}};
    for (std::string_view apps = fields[4]; !apps.empty();) {
      std::string_view app = trim_ascii(take(apps, ' '));
      if (!app.empty()) credit.apps.emplace_back(app);
    }
    credits.push_back(std::move(credit));
  }
  return credits;
}

std::vector<Credit> credits_for(std::string_view app) {
  static const std::vector<Credit> all = parse_credits(detail::credits_text);
  std::vector<Credit> out;
  std::ranges::copy_if(all, std::back_inserter(out),
                       [&](const Credit& c) { return std::ranges::find(c.apps, app) != c.apps.end(); });
  return out;
}

std::string credit_label(const Credit& credit) {
  std::string label = credit.name;
  if (!credit.version.empty()) label += " " + credit.version;
  if (!credit.license.empty()) label += " (" + credit.license + ")";
  return label;
}

}  // namespace dd::host
