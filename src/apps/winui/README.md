# DiaDraw for Windows (WinUI 3)

A WinUI 3 shell over the C++ core, in C++/WinRT and code only (no XAML files, so no XAML compiler
step). It shares everything but widgets with the macOS and gtkmm apps: `dd::canvas::Canvas` for
the canvas, `dd::host::Document` for the document, `dd::dsl::highlight` for syntax colours.

| Piece                                        | Here                                                                |
| -------------------------------------------- | ------------------------------------------------------------------- |
| Canvas drawing                               | Win2D `CanvasControl` (Direct2D and DirectWrite), `src/painter.cpp` |
| Canvas input, inline rename                  | `src/canvas_view.cpp`                                               |
| Text pane, menus, status, undo, test scripts | `src/window.cpp` (RichEditBox; the window keeps the undo history)   |
| App, runtime bootstrap, test hooks           | `src/main.cpp`                                                      |
| Win2D registration, UTF-8 code page, DPI     | `app.manifest`                                                      |

## Build

Needs Visual Studio 2022 (Desktop development with C++, a Windows 11 SDK), CMake 3.25+ and
Ninja (both ship with Visual Studio), and the Windows App Runtime 1.6 installed
(`winget install Microsoft.WindowsAppRuntime.1.6`, or the installer from Microsoft's Windows App
SDK downloads page). Open an "x64 Native Tools Command Prompt for VS 2022" at the repository root:

```bat
cmake --preset windows
cmake --build --preset windows
..\diadraw-build\windows\apps\winui\DiaDraw.exe
ctest --preset windows-gui
```

Configuring downloads four NuGet packages into `diadraw-build/windows/nuget` (C++/WinRT, Windows App
SDK, Win2D, WebView2) and generates the C++/WinRT headers from them; versions are cache variables
in `CMakeLists.txt` (`DIADRAW_WINDOWSAPPSDK_VERSION` and friends).

## Packages

From the repository root, after the build:

- **Portable ZIP:** `cpack --preset windows` makes `diadraw-build/windows/DiaDraw-<version>-Windows-<arch>.zip`
  with `DiaDraw.exe`, the `diadraw` CLI, the bootstrapper and Win2D DLLs, and draw.io's stencil
  sets with their NOTICE. It needs the Windows App Runtime 1.6 installed (see Build).
- **MSIX:** `powershell -ExecutionPolicy Bypass -File .\src\apps\winui\msix\pack.ps1 -Build ..\diadraw-build\windows -Pfx cert.pfx -Password <pw>`.
  The package declares the Windows App Runtime as a dependency, so Windows installs it with the
  app, and registers `.draw` files. The certificate's subject must equal `DIADRAW_MSIX_PUBLISHER`
  (`CN=DiaDraw` by default). For a local test, a self-signed one does:

  ```powershell
  $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=DiaDraw" -KeyUsage DigitalSignature `
    -CertStoreLocation Cert:\CurrentUser\My -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
  Export-PfxCertificate -Cert $cert -FilePath cert.pfx -Password (ConvertTo-SecureString <pw> -AsPlainText -Force)
  Import-PfxCertificate -FilePath cert.pfx -CertStoreLocation Cert:\LocalMachine\TrustedPeople -Password (ConvertTo-SecureString <pw> -AsPlainText -Force)
  ```

## Not yet built

This app was written on a Mac. It now compiles and links with MSVC (Visual Studio 2026, Windows SDK
10.0.26100), and the core's tests pass there, but it has not been run yet (status in the
[backlog](../../../docs/backlog.md#apps-and-distribution)). The parts most likely
to need a correction on first run:

- **Package layouts.** `CMakeLists.txt` assumes where each package keeps its `.winmd` files, the
  bootstrapper's `.lib`/`.dll` and Win2D's DLL. If configuring stops with "Package metadata not
  found", look under `diadraw-build/windows/nuget` and fix the globs.
- **Duplicate metadata.** If `cppwinrt` reports a type defined twice, two of the Windows App SDK's
  `lib/uap10.0*` folders hold the same `.winmd`; keep one.
- **The core under MSVC.** The core has only been built with Clang so far. `/utf-8`, `/bigobj`
  and `/Zc:__cplusplus` are set; other MSVC complaints in the core modules are likely to be small.
- **RichEdit's text.** `DocumentWindow::box_text` assumes RichEdit returns lines separated by
  `\r` plus one final paragraph mark. If saved files gain or lose a last newline, adjust it there.
- **MSIX.** The framework dependency's name and version are read from the Windows App SDK's
  `WindowsAppSDK-VersionInfo.h` at configure time; if CMake warns it could not, fill in
  `PackageDependency` in `diadraw-build/windows/apps/winui/AppxManifest.xml`. If the packaged app cannot
  create Win2D's classes, the reg-free entries in `app.manifest` may be clashing with the
  package's: build the MSIX's exe without them.
- **Code-only XAML resources.** If controls show without styles, the app is not finding
  `XamlControlsResources`; check that the Windows App Runtime version matches the package version.
