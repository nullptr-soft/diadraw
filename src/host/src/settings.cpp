#include "host/settings.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "diadraw/dsl/format.hpp"
#include "host/files.hpp"

namespace dd::host {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kIndentKey = "format.indent";

/// An environment variable as a path; empty when unset.
fs::path env_path(const char* name) {
#if defined(_WIN32)
  std::wstring wide(name, name + std::char_traits<char>::length(name));  // names are ASCII
  const wchar_t* v = _wgetenv(wide.c_str());  // NOLINT(concurrency-mt-unsafe)
  return v && *v ? fs::path(v) : fs::path();
#else
  const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return v && *v ? utf8_path(v) : fs::path();
#endif
}

std::vector<std::string> read_lines(const fs::path& file) {
  std::vector<std::string> lines;
  std::ifstream in(file, std::ios::binary);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
  }
  return lines;
}

/// The value of a `key=value` line for `key`, or nullopt.
std::optional<std::string_view> value_of(std::string_view line, std::string_view key) {
  if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != '=') return std::nullopt;
  return line.substr(key.size() + 1);
}

}  // namespace

fs::path settings_file() {
#if defined(_WIN32)
  fs::path base = env_path("APPDATA");
  return base.empty() ? fs::path() : base / "DiaDraw" / "settings.ini";
#else
  if (fs::path xdg = env_path("XDG_CONFIG_HOME"); !xdg.empty()) return xdg / "diadraw" / "settings.ini";
  fs::path home = env_path("HOME");
  return home.empty() ? fs::path() : home / ".config" / "diadraw" / "settings.ini";
#endif
}

std::optional<std::string> read_setting(const fs::path& file, std::string_view key) {
  if (file.empty()) return std::nullopt;
  for (const auto& line : read_lines(file))
    if (auto v = value_of(line, key)) return std::string(*v);
  return std::nullopt;
}

bool write_setting(const fs::path& file, std::string_view key, std::string_view value) {
  if (file.empty() || key.empty() || key.find_first_of("=\n\r") != std::string_view::npos ||
      value.find_first_of("\n\r") != std::string_view::npos)
    return false;
  std::vector<std::string> lines = read_lines(file);
  std::string entry = std::string(key) + "=" + std::string(value);
  bool replaced = false;
  for (auto& line : lines)
    if (value_of(line, key)) {
      line = entry;
      replaced = true;
      break;
    }
  if (!replaced) lines.push_back(std::move(entry));
  std::error_code ec;
  if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
  // Written beside the file and moved over it, so a failed write never leaves half a file.
  fs::path temp = file;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    for (const auto& line : lines) out << line << '\n';
    if (!out.flush()) return false;
  }
  fs::rename(temp, file, ec);
  if (ec) fs::remove(temp, ec);
  return !ec;
}

int read_indent_size(const fs::path& file) {
  auto text = read_setting(file, kIndentKey);
  int size = 0;
  if (!text || text->empty()) return dsl::kDefaultIndentSize;
  auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), size);
  if (error != std::errc() || end != text->data() + text->size() || size < dsl::kMinIndentSize ||
      size > dsl::kMaxIndentSize)
    return dsl::kDefaultIndentSize;
  return size;
}

bool write_indent_size(int size, const fs::path& file) {
  return write_setting(file, kIndentKey, std::to_string(size));
}

}  // namespace dd::host
