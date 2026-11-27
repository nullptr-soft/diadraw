#pragma once

// The person's preferences that belong to no document, for the GTK and Windows apps: a small
// `key=value` file per user. (The macOS app keeps the same keys in its user defaults, and the web
// app in the browser.)

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dd::host {

/// `$XDG_CONFIG_HOME/diadraw/settings.ini`, else `~/.config/diadraw/settings.ini`; on Windows
/// `%APPDATA%\DiaDraw\settings.ini`. Empty when none of those variables is set.
[[nodiscard]] std::filesystem::path settings_file();

/// One setting from a settings file; nullopt when the file or the key is missing.
[[nodiscard]] std::optional<std::string> read_setting(const std::filesystem::path& file, std::string_view key);
/// Sets one setting, keeping the others, and creates the file and its folder when needed. False
/// when it cannot be written; the app keeps the value for this run.
bool write_setting(const std::filesystem::path& file, std::string_view key, std::string_view value);

/// Format Document's indent size (`format.indent`), 1 to 8; dsl::kDefaultIndentSize when it is not
/// set or not valid.
[[nodiscard]] int read_indent_size(const std::filesystem::path& file = settings_file());
bool write_indent_size(int size, const std::filesystem::path& file = settings_file());

/// The sizes every native app's Indent Size menu offers (the web app's too).
inline constexpr int kIndentSizes[] = {2, 3, 4, 8};

}  // namespace dd::host
