# DiaDraw for macOS (AppKit)

An AppKit shell, in Objective-C++, over the C++ core; `src/README.md` describes what the native
shells share.

## Build and package

From the repository root:

```bash
cmake --preset release && cmake --build --preset release   # diadraw-build/release/apps/macos/DiaDraw.app
cpack --preset release                                       # diadraw-build/release/DiaDraw-<version>-Darwin-<arch>.dmg
```

The DMG holds `DiaDraw.app` and a link to Applications. The app links only system frameworks
(the core, lunasvg and miniz are static), and carries draw.io's stencil sets with their NOTICE in
`Contents/Resources/drawio`.

## Running a development build

Start the app through Launch Services, so it comes up in front with its window focused:

```bash
open ../diadraw-build/dev/apps/macos/DiaDraw.app                 # --args file.draw opens a file; -W waits
```

Running `DiaDraw.app/Contents/MacOS/DiaDraw` directly from a terminal also works, but macOS may
leave the window behind the terminal: since macOS 14 an app takes focus only when the app in
front gives it up. The app asks for focus once launch finishes, which is all it can do.

## Signing and notarization

By default the app is signed ad hoc (`DIADRAW_CODESIGN_IDENTITY=-`), which is enough on the Mac
that built it. A copy downloaded elsewhere is blocked by Gatekeeper until it is signed with a
Developer ID and notarized:

```bash
cmake --preset release -DDIADRAW_CODESIGN_IDENTITY="Developer ID Application: <name> (<team>)"
cmake --build --preset release && cpack --preset release
xcrun notarytool submit ../diadraw-build/release/DiaDraw-*.dmg --keychain-profile <profile> --wait
xcrun stapler staple ../diadraw-build/release/DiaDraw-*.dmg
```

With a Developer ID the build also turns on the hardened runtime, which notarization requires.
