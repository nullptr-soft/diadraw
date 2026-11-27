#pragma once

// The Help dialog's content: what the canvas does with the mouse and the keyboard, from
// src/host/data/canvas-help.txt (compiled into the core). Every app shows the same list; the web
// app reads the same file (src/apps/web/src/help.ts).

#include <string>
#include <string_view>
#include <vector>

namespace dd::host {

/// Whose keys the list names: the file has one key column for each.
enum class HelpKeys {
  mac,      ///< macOS, and the web app on a Mac: ⌘, ⌥, ⇧
  pc,       ///< Linux (the GTK app, on macOS too), and the web app elsewhere: Ctrl, Alt, Shift
  windows,  ///< the Windows app
};

/// One gesture or shortcut and what it does.
struct HelpEntry {
  std::string keys;    ///< `⌘A`, `Drag a corner`
  std::string action;  ///< `Select everything`
  bool operator==(const HelpEntry&) const = default;
};

/// A heading and its entries.
struct HelpSection {
  std::string title;
  std::vector<HelpEntry> entries;
  bool operator==(const HelpSection&) const = default;
};

/// Reads the file's format for `keys`' column: `# Heading` starts a section; other lines are
/// `action<TAB>mac<TAB>pc<TAB>windows`, where `=` repeats the column before and `-` leaves the
/// line out. Blank lines, `//` comments, malformed lines and sections left empty are skipped.
[[nodiscard]] std::vector<HelpSection> parse_canvas_help(std::string_view text, HelpKeys keys);

/// The built-in list for `keys`, read once from the file compiled into the core.
[[nodiscard]] const std::vector<HelpSection>& canvas_help(HelpKeys keys);

}  // namespace dd::host
