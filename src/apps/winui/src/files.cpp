#include "files.hpp"

#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>

#include <filesystem>

namespace dd::winui {

HWND handle_of(winrt::Microsoft::UI::Xaml::Window const& window) {
  HWND hwnd = nullptr;
  window.as<IWindowNative>()->get_WindowHandle(&hwnd);
  return hwnd;
}

namespace {

std::optional<std::string> result_of(IFileDialog* dialog) {
  winrt::com_ptr<IShellItem> item;
  if (FAILED(dialog->GetResult(item.put()))) return std::nullopt;
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
  std::string out = winrt::to_string(path);
  CoTaskMemFree(path);
  return out;
}

}  // namespace

std::optional<std::string> choose_open(HWND owner, const wchar_t* title) {
  auto dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
  dialog->SetTitle(title);
  if (dialog->Show(owner) != S_OK) return std::nullopt;
  return result_of(dialog.get());
}

std::optional<std::string> choose_save(HWND owner, const std::string& name) {
  auto dialog = winrt::create_instance<IFileSaveDialog>(CLSID_FileSaveDialog);
  dialog->SetFileName(winrt::to_hstring(name).c_str());
  if (dialog->Show(owner) != S_OK) return std::nullopt;
  return result_of(dialog.get());
}

std::string executable_dir() {
  wchar_t path[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  auto dir = std::filesystem::path(std::wstring(path, n)).parent_path().u8string();
  return std::string(dir.begin(), dir.end());
}

}  // namespace dd::winui
