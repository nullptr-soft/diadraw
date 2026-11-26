// DiaDraw for Windows: a WinUI 3 shell over the C++ core, written in C++/WinRT without XAML
// files. It runs unpackaged (the portable ZIP: the Windows App Runtime is found through the
// bootstrapper, Win2D is registered in app.manifest) or packaged (MSIX: the package manifest
// declares both).
//
// Test hooks (environment):
//   DIADRAW_SCRIPT=<file>    runs canvas gestures from the file, writes <file>.log and quits
//   DIADRAW_SNAPSHOT=<png>   writes the window to a PNG and quits
// In a test run the window is shown without being activated and ignores real input.

#include "winrt.hpp"

#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <appmodel.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "files.hpp"
#include "host/document.hpp"
#include "host/examples.hpp"
#include "host/libraries.hpp"
#include "window.hpp"

namespace {

using namespace winrt::Microsoft::UI::Xaml;
namespace markup = winrt::Microsoft::UI::Xaml::Markup;

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

bool test_run() { return !env("DIADRAW_SCRIPT").empty() || !env("DIADRAW_SNAPSHOT").empty(); }

/// The app: windows, and the actions that are not about one document. It provides XAML type
/// information for the built-in controls, which a code-only WinUI app must do itself.
struct App : ApplicationT<App, markup::IXamlMetadataProvider> {
  void OnLaunched(LaunchActivatedEventArgs const&) {
    Resources().MergedDictionaries().Append(Controls::XamlControlsResources());
    // Files named on the command line, else an empty document.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool opened = false;
    for (int i = 1; i < argc; ++i) {
      open_path(winrt::to_string(argv[i]));
      opened = true;
    }
    LocalFree(argv);
    if (!opened) open_window("", "");
  }

  markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) {
    return provider_.GetXamlType(type);
  }
  markup::IXamlType GetXamlType(winrt::hstring const& name) { return provider_.GetXamlType(name); }
  winrt::com_array<markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider_.GetXmlnsDefinitions(); }

 private:
  dd::winui::DocumentWindow* open_window(const std::string& path, const std::string& text) {
    dd::winui::AppActions actions{
        .new_document = [this] { open_window("", ""); },
        .open = [this] { choose_and_open(); },
        .import = [this] { choose_and_import(); },
        .open_example =
            [this](std::string_view name) {
              if (const auto* example = dd::host::find_example(name)) open_window("", std::string(example->source));
            },
        .open_text = [this](const std::string& text) { return open_window("", text); },
        .quit = [this] { Exit(); },
    };
    auto window = std::make_unique<dd::winui::DocumentWindow>(actions, path, text);
    auto* raw = window.get();
    raw->on_closed = [this, raw] {
      std::erase_if(windows_, [raw](const auto& w) { return w.get() == raw; });
    };
    windows_.push_back(std::move(window));
    raw->show(!test_run());
    if (test_run()) schedule_tests(*raw);
    return raw;
  }

  void open_path(const std::string& path) {
    open_window(path, dd::host::read_file(path).value_or(""));
  }

  HWND owner() const { return windows_.empty() ? nullptr : dd::winui::handle_of(windows_.back()->window()); }

  void choose_and_open() {
    if (auto path = dd::winui::choose_open(owner(), L"Open a DiaDraw file")) open_path(*path);
  }

  void choose_and_import() {
    auto path = dd::winui::choose_open(owner(), L"Import a Mermaid, PlantUML, Structurizr, draw.io or BPMN file");
    if (!path) return;
    auto imported = dd::host::import_file(*path);
    if (!imported) {
      MessageBoxW(owner(), winrt::to_hstring(imported.error().message).c_str(), L"DiaDraw", MB_OK | MB_ICONWARNING);
      return;
    }
    auto window = open_window("", imported->text);
    if (!imported->report.empty()) window->flash(imported->report);
  }

  /// The test hooks: a script one second after the window appears, a snapshot after two.
  void schedule_tests(dd::winui::DocumentWindow& window) {
    auto queue = window.window().DispatcherQueue();
    auto after = [queue](int ms, std::function<void()> f) {
      auto timer = queue.CreateTimer();
      timer.Interval(std::chrono::milliseconds(ms));
      timer.IsRepeating(false);
      timer.Tick([timer, f](auto const&, auto const&) { f(); });
      timer.Start();
    };
    std::string script = env("DIADRAW_SCRIPT"), snapshot = env("DIADRAW_SNAPSHOT");
    if (!script.empty())
      after(1000, [this, &window, script, snapshot] {
        std::string log = window.run_script(dd::host::read_file(script).value_or(""));
        std::ofstream(std::filesystem::u8path(script + ".log"), std::ios::binary) << log;
        if (snapshot.empty()) Exit();
      });
    if (!snapshot.empty())
      after(2000, [this, &window, snapshot] { window.snapshot(snapshot, [this] { Exit(); }); });
  }

  winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider_;
  std::vector<std::unique_ptr<dd::winui::DocumentWindow>> windows_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  // Unpackaged (the portable ZIP), the Windows App Runtime is found through the bootstrapper; it
  // is installed separately (see README.md). Packaged (MSIX), the package declares it instead.
  UINT32 length = 0;
  bool packaged = GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
  if (!packaged) {
    HRESULT hr = MddBootstrapInitialize2(WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
                                         PACKAGE_VERSION{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64},
                                         MddBootstrapInitializeOptions_OnNoMatch_ShowUI);
    if (FAILED(hr)) return hr;
  }
  // draw.io's stencil sets: next to the executable ($DIADRAW_DRAWIO overrides).
  std::string drawio = env("DIADRAW_DRAWIO");
  if (drawio.empty()) drawio = dd::winui::executable_dir() + "/drawio";
  if (std::filesystem::exists(std::filesystem::u8path(drawio + "/index.json.gz"))) dd::host::set_drawio_dir(drawio);
  Application::Start([](auto&&) { winrt::make<App>(); });
  if (!packaged) MddBootstrapShutdown();
  return 0;
}
