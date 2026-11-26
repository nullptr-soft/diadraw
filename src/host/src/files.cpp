#include "host/files.hpp"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include "diadraw/base/text.hpp"
#endif

namespace dd::host {

namespace fs = std::filesystem;

fs::path utf8_path(std::string_view path) {
#ifdef _WIN32
  // MSVC throws on ill-formed UTF-8 (a Latin-1 `!include` name, say); repaired, it is just a
  // name that is not found.
  const std::string valid = to_valid_utf8(path);
  return fs::path(std::u8string(valid.begin(), valid.end()));
#else
  return fs::path(std::string(path));  // POSIX paths are bytes: keep them as given
#endif
}

std::string path_utf8(const fs::path& path) {
  try {
    std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
  } catch (...) {
    return {};  // a Windows name with an unpaired surrogate has no UTF-8 spelling
  }
}

std::optional<std::string> read_file(const std::string& path) {
  fs::path p = utf8_path(path);
  std::error_code ec;
  // A directory opens as an empty stream on POSIX; it is not an empty document.
  if (fs::is_directory(p, ec)) return std::nullopt;
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool write_file(const std::string& path, std::string_view bytes) {
  fs::path p = utf8_path(path);
  std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();  // flush now, so that a full disk is reported rather than lost in the destructor
  return !out.fail();
}

bool ends_with_ci(std::string_view name, std::string_view suffix) {
  if (name.size() < suffix.size()) return false;
  name.remove_prefix(name.size() - suffix.size());
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

}  // namespace dd::host
