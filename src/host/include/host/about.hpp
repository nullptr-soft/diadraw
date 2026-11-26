#pragma once

// Help › About DiaDraw in every native shell: the GTK and WinUI dialogs, and the options of the
// standard About panel on macOS (which reads the name and version from the bundle). The version is
// the core's, dd_version(), from the repository's VERSION file. The credits are in credits.hpp.

namespace dd::host {

/// The one-line summary. Keep it the same as in cmake/Packaging.cmake, the GTK desktop entry and
/// AppStream data, and the MSIX manifest.
inline constexpr const char* kSummary = "Diagrams as text, with a canvas that writes the text back";

/// The project's home page.
inline constexpr const char* kProjectUrl = "https://github.com/mykulyak/diadraw";

/// DiaDraw's own licence (the repository's LICENSE file), its copyright line and the licence's
/// address. Keep them the same as LICENSE and the web app's (src/apps/web/src/credits.ts); the host
/// tests and the web app's tests check both against LICENSE, and the macOS Info.plist gets the
/// copyright line from CMake (DIADRAW_COPYRIGHT).
inline constexpr const char* kLicense = "MIT License";
inline constexpr const char* kCopyright = "Copyright © 2026 Andriy Mykulyak";
inline constexpr const char* kLicenseUrl = "https://github.com/mykulyak/diadraw/blob/main/LICENSE";

/// Every credit, with what each one is used for (docs/credits.md, the website's Credits page).
inline constexpr const char* kCreditsUrl = "https://github.com/mykulyak/diadraw/blob/main/docs/credits.md";

}  // namespace dd::host
