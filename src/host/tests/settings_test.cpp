// The GTK and Windows apps' settings file: Format Document's indent size survives a restart.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "host/settings.hpp"

namespace fs = std::filesystem;
using namespace dd::host;

namespace {

fs::path temp_file(const char* name) {
  fs::path dir = fs::temp_directory_path() / "diadraw-settings-test";
  fs::create_directories(dir);
  fs::path file = dir / name;
  fs::remove(file);
  return file;
}

}  // namespace

TEST(Settings, IndentSizeDefaultsAndIsRemembered) {
  fs::path file = temp_file("indent.ini");
  EXPECT_EQ(read_indent_size(file), 2);
  ASSERT_TRUE(write_indent_size(4, file));
  EXPECT_EQ(read_indent_size(file), 4);
  ASSERT_TRUE(write_indent_size(8, file));
  EXPECT_EQ(read_indent_size(file), 8);
}

TEST(Settings, KeepsOtherKeysAndIgnoresBadValues) {
  fs::path file = temp_file("other.ini");
  {
    std::ofstream out(file, std::ios::binary);
    out << "canvas.grid=10\r\nformat.indent=nope\n";
  }
  EXPECT_EQ(read_indent_size(file), 2);
  ASSERT_TRUE(write_indent_size(3, file));
  EXPECT_EQ(read_indent_size(file), 3);
  EXPECT_EQ(read_setting(file, "canvas.grid"), "10");
  for (const char* bad : {"0", "9", "4x", ""}) {
    ASSERT_TRUE(write_setting(file, "format.indent", bad));
    EXPECT_EQ(read_indent_size(file), 2) << bad;
  }
  EXPECT_FALSE(write_setting(file, "a=b", "1"));
  EXPECT_FALSE(write_setting(file, "k", "two\nlines"));
  EXPECT_FALSE(write_indent_size(4, fs::path()));
}

TEST(Settings, FileFollowsTheConfigFolder) {
#if !defined(_WIN32)
  const char* was = std::getenv("XDG_CONFIG_HOME");
  std::string saved = was ? was : "";
  setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
  EXPECT_EQ(settings_file(), fs::path("/tmp/xdg/diadraw/settings.ini"));
  if (was) setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
  else unsetenv("XDG_CONFIG_HOME");
#endif
}
