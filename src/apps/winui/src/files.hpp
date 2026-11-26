// Open and save dialogs (the Win32 common item dialog: it works for unpackaged apps without the
// window-handle dance of the WinRT pickers), and the folder next to the executable.

#pragma once

#include "winrt.hpp"

#include <optional>
#include <string>

namespace dd::winui {

/// The window handle of a WinUI window.
HWND handle_of(winrt::Microsoft::UI::Xaml::Window const& window);

/// Asks for a file to open; UTF-8 path, or nothing when cancelled.
std::optional<std::string> choose_open(HWND owner, const wchar_t* title);
/// Asks where to save; `name` is the suggested file name.
std::optional<std::string> choose_save(HWND owner, const std::string& name);

/// The folder holding DiaDraw.exe (UTF-8).
std::string executable_dir();

}  // namespace dd::winui
