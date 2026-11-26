#pragma once

// Files as native hosts read and write them. Paths are UTF-8 strings everywhere in DiaDraw (the
// shells, the CLI, JSON from MCP clients); these helpers convert them to and from
// std::filesystem::path so that non-ASCII names work on Windows too, where a narrow string would
// be read in the ANSI code page.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dd::host {

/// A UTF-8 path as a filesystem path. Never throws: on Windows ill-formed bytes become U+FFFD;
/// on POSIX the bytes are kept as they are, so names in other encodings still work.
[[nodiscard]] std::filesystem::path utf8_path(std::string_view path);
/// A filesystem path as UTF-8; empty when it has none (Windows names with lone surrogates).
[[nodiscard]] std::string path_utf8(const std::filesystem::path& path);

/// A file's bytes; nothing when it cannot be read or is a directory. `path` is UTF-8.
[[nodiscard]] std::optional<std::string> read_file(const std::string& path);
/// Writes `bytes` to `path` (UTF-8), creating its folder when missing; false when it cannot.
[[nodiscard]] bool write_file(const std::string& path, std::string_view bytes);

/// Whether a file name ends with `suffix` (lower case, such as `.svg`), ignoring ASCII case.
[[nodiscard]] bool ends_with_ci(std::string_view name, std::string_view suffix);

}  // namespace dd::host
