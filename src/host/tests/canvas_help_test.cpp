// The Help dialog's list of canvas gestures and shortcuts (host/canvas_help.hpp).

#include "host/canvas_help.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace dd::host;

namespace {

constexpr std::string_view kSample =
    "// a comment\n"
    "\n"
    "# Select\n"
    "Select everything\t⌘A\tCtrl+A\t-\n"
    "  Clear the selection \t Esc \t=\t=\r\n"
    "\n"
    "#   Edit  \n"
    "Undo\t⌘Z\tCtrl+Z\tCtrl+Z\n"
    "Only on Windows\t-\t-\tCtrl+Y\n"
    "Only on a Mac\t⌥\t-\t=\n";

}  // namespace

TEST(CanvasHelp, ReadsTheColumnForEachKind) {
  EXPECT_EQ(parse_canvas_help(kSample, HelpKeys::mac),
            (std::vector<HelpSection>{
                {"Select", {{"⌘A", "Select everything"}, {"Esc", "Clear the selection"}}},
                {"Edit", {{"⌘Z", "Undo"}, {"⌥", "Only on a Mac"}}},
            }));
  EXPECT_EQ(parse_canvas_help(kSample, HelpKeys::pc),
            (std::vector<HelpSection>{
                {"Select", {{"Ctrl+A", "Select everything"}, {"Esc", "Clear the selection"}}},
                {"Edit", {{"Ctrl+Z", "Undo"}}},
            }));
  // `=` after `-` stays left out.
  EXPECT_EQ(parse_canvas_help(kSample, HelpKeys::windows),
            (std::vector<HelpSection>{
                {"Select", {{"Esc", "Clear the selection"}}},
                {"Edit", {{"Ctrl+Z", "Undo"}, {"Ctrl+Y", "Only on Windows"}}},
            }));
}

TEST(CanvasHelp, LeavesOutWhatIsNotAnEntry) {
  // Entries before any heading, the wrong number of fields, an empty action, `=` in the first
  // key column, and headings left without entries.
  constexpr std::string_view text =
      "Orphan\ta\tb\tc\n"
      "# Empty\n"
      "#\n"
      "# Kept\n"
      "Too few\ta\tb\n"
      "Too many\ta\tb\tc\td\n"
      "\ta\tb\tc\n"
      "Repeats nothing\t=\t=\t=\n"
      "Kept\ta\tb\tc\n";
  for (auto keys : {HelpKeys::mac, HelpKeys::pc, HelpKeys::windows}) {
    auto sections = parse_canvas_help(text, keys);
    ASSERT_EQ(sections.size(), 1u);
    EXPECT_EQ(sections[0].title, "Kept");
    ASSERT_EQ(sections[0].entries.size(), 1u);
    EXPECT_EQ(sections[0].entries[0].action, "Kept");
  }
  EXPECT_TRUE(parse_canvas_help("", HelpKeys::mac).empty());
}

TEST(CanvasHelp, BuiltInListNamesEachPlatformsKeys) {
  for (auto keys : {HelpKeys::mac, HelpKeys::pc, HelpKeys::windows}) {
    const auto& sections = canvas_help(keys);
    ASSERT_GE(sections.size(), 3u);
    std::set<std::string> actions;
    for (const auto& section : sections) {
      EXPECT_FALSE(section.entries.empty()) << section.title;
      for (const auto& entry : section.entries) {
        EXPECT_TRUE(actions.insert(entry.action).second) << "twice: " << entry.action;
        if (keys == HelpKeys::mac) {
          EXPECT_EQ(entry.keys.find("Ctrl"), std::string::npos) << entry.keys;
        } else {
          EXPECT_EQ(entry.keys.find("⌘"), std::string::npos) << entry.keys;
          EXPECT_EQ(entry.keys.find("⌥"), std::string::npos) << entry.keys;
        }
      }
    }
    // The gestures every shell has.
    for (const char* action : {"Connect two elements", "Rename the id everywhere", "Remove what is selected"})
      EXPECT_TRUE(actions.contains(action)) << action;
  }
  EXPECT_EQ(&canvas_help(HelpKeys::mac), &canvas_help(HelpKeys::mac));
}
