# DiaDraw C++ core (`libdiadraw`)

The platform-independent C++20 core of DiaDraw. It is built five ways from this one CMake tree:

- a native static library with a stable C ABI (`capi/include/diadraw/diadraw.h`) for native shells
  and language bindings (the Rust crates in `rust/`);
- a WebAssembly module (`diadraw-build/wasm/diadraw.mjs` + `.wasm` + `.d.ts`) for the web app and VS Code;
- the native `diadraw` command-line tool;
- a Python extension module (`src/python`, nanobind) for the `diadraw` Python package, built by
  `pip install .` from the repository root (see [python/README.md](python/README.md));
- a Node.js addon (`src/node`, `@diadraw/node`), built by cmake-js with `DIADRAW_BUILD_NODE=ON`
  (see [node/README.md](node/README.md)).

It began as a port of a TypeScript implementation, checked against it on the corpus module by
module; the TypeScript engine has since been removed, and every JavaScript host (the web app, the
VS Code extension) runs this core as one WebAssembly module (`@diadraw/wasm`). The architecture
proposal and milestones are in the project doc "DiaDraw C++ core: architecture proposal".

## Decisions

- Native UIs: AppKit with Objective-C++ (macOS), WinUI with C++/WinRT (Windows), gtkmm (Linux),
  each calling the core directly. The web UI is built with Web Components over the WASM module.
- Layout: an in-house layered engine in C++ (no ELK, Graphviz or OGDF).
- SVG output stayed byte-identical to the TypeScript renderer while both existed; snapshots in
  `src/wasm` now pin it.
- Tests use googletest. DiaDraw is under the MIT License; `docs/credits.md` lists third-party code.

## Status

| Milestone | Module | State |
| --- | --- | --- |
| C0 | CMake presets, native + WASM builds, C ABI, CLI stub | done |
| C1 | `model`: model, operations with inverses, history, `resolve_view`, JSON | done: TS core tests ported, corpus parity clean |
| C2 | `dsl`: lossless parser, lowering, print, patch | done: TS DSL tests ported, corpus + random-edit parity clean |
| C3 | `render` + fixed/sequence `layout`: theme, shapes, measure, SVG, router | done: render parity byte-identical on all 916 fixed/sequence corpus views |
| C4 | `layout`: in-house layered engine, constraint pass, stability | done: beats ELK on the benchmark except bends (+0.8%), see below |
| C5 | ship: `@diadraw/wasm`, Web Components front end, native CLI | done |
| C6 | `io`, `library`, `lsp`, `export`: importers, exporters, libraries, language server, PNG/PDF | done: import, export, library, CLI and language-server parity clean (see below) |
| C7 | native shells: macOS (AppKit), Linux (gtkmm), Windows (WinUI) | macOS and gtkmm done; WinUI written, not yet built |

## Build

Needs CMake 3.25+, Ninja and a C++20 compiler; the WASM preset also needs emsdk. Third-party code
comes through `FetchContent`, so nothing else has to be installed. Run presets from the repository
root. [src/CMakeLists.txt](CMakeLists.txt) registers the source modules; the dependency and
packaging modules live in [cmake/](../cmake/).
Each core module declares its implementation files and public build include path in its
`CMakeLists.txt`. They all share the same `diadraw` static library.

Numbers parse with `std::from_chars` where the standard library has it for `double`, and with
`strtod_l` in the C locale where it does not (Apple's libc++ for macOS before the Python wheels'
13.3 target); CMake checks once (`DD_HAVE_FROM_CHARS_DOUBLE`), and `-DDD_HAVE_FROM_CHARS_DOUBLE=0`
forces the fallback to test it. Reconfigure with `--fresh` after changing the macOS deployment
target.

Each workflow preset runs configure, build and test (and package, where it says so) in one
command:

```bash
cmake --workflow --preset dev               # Debug with ASan/UBSan: core, CLI, the macOS or Linux app, all tests
cmake --workflow --preset dev-headless      # the same without the app's gesture tests (label gui), which open a window
cmake --workflow --preset core              # core, CLI and tests only: no AppKit, gtkmm or WinUI needed
cmake --workflow --preset release           # Release: build, test (without gesture tests) and package the app
cmake --workflow --preset windows           # from a VS developer prompt: build, test and package the WinUI app
cmake --workflow --preset windows-headless  # the same without gesture tests or packaging, as CI runs it
source "$EMSDK/emsdk_env.sh" && cmake --workflow --preset wasm  # configure and build; tests run with npm test
```

| Platform | App | Day to day | Package |
| --- | --- | --- | --- |
| macOS | AppKit | `dev` | `release` (DMG) |
| Linux | gtkmm 4 | `dev` (`xvfb-run -a cmake --workflow --preset dev` without a display) | `release` (tarball, plus DEB and RPM where their tools are installed) |
| Windows | WinUI 3 | `windows` | `windows` (ZIP) |
| Web | WebAssembly core | `wasm`, or `npm run build -w @diadraw/wasm` | `npm run build` |

On a fresh build tree, `dev` and `release` build the app of the platform they run on
(`DIADRAW_BUILD_MACOS` on macOS, `DIADRAW_BUILD_GTK` on Linux, which needs gtkmm 4 and libcurl).
They follow the option's cached value, so a tree configured earlier with `-DDIADRAW_BUILD_GTK=OFF`
keeps it until `cmake --fresh --preset dev`. On Windows they build no app and MSVC gets no
sanitizers; use `windows` there. Where no GUI toolkit is installed, as in a cloud container, use
`core`. The `release` workflow runs the tests before packaging, so it needs them built (the
default `DIADRAW_BUILD_TESTS=ON`). The gesture tests also run on their own with
`ctest --preset dev-gui` (or `windows-gui`).

The individual presets still work on their own. For a release build without testing or
packaging, use `cmake --preset release && cmake --build --preset release`.

Every preset builds outside the repository, in `diadraw-build/<preset>` next to the checkout
(for `~/Developer/diadraw` that is `~/Developer/diadraw-build/dev`, `.../release` and so on).
If a preset directory was configured before the move to the repository root, reconfigure it
with `cmake --fresh --preset <name>` from the root so its cache uses the new source directory.

## Native shells

The shells only hold widgets. What they share lives in C++: `canvas/canvas.hpp` (hit-testing,
selection, drag-to-place with hint preview, connecting, inline-edit requests and the overlay, as
a display list), `host/include/host/document.hpp` (the session, views, status line, gestures turned into
text edits, exports, importing from disk) and `dsl/highlight.hpp` (syntax colouring). Each shell
adds a painter for the display list and its own text view.

| Shell | Where | Build |
| --- | --- | --- |
| macOS, AppKit + Objective-C++ | `apps/macos` | on by default on macOS: `diadraw-build/dev/apps/macos/DiaDraw.app` |
| Linux, gtkmm 4 | `apps/gtk` | on by default on Linux; on macOS `brew install gtkmm4` and `-DDIADRAW_BUILD_GTK=ON` |
| Windows, WinUI 3 + C++/WinRT | `apps/winui` | `cmake --preset windows` in a VS developer prompt; not yet compiled, see `apps/winui/README.md` |

File › Cloud Storage (Google Drive and OneDrive) is shared too: `host/include/host/cloud.hpp`
signs in through the system browser and a loopback redirect (OAuth with PKCE), and reads and
writes files over each service's REST API with the platform's HTTPS client (libcurl on macOS and
Linux, WinHTTP on Windows), off the UI thread. Its OAuth client IDs are CMake cache variables;
see [docs/guide/cloud-storage.md](../docs/guide/cloud-storage.md). Without libcurl the
`diadraw_cloud` library and its tests are left out, and the macOS and GTK apps refuse to configure.

Both run the same gesture scripts (`apps/shell-tests`, `ctest -L gui`) and must leave the same
text. `DIADRAW_SCRIPT=<file>` and `DIADRAW_SNAPSHOT=<png>` drive an app from the environment; such
runs never take focus and ignore real input.

## PNG and PDF export

`diadraw render file.draw -o out.png|out.pdf` goes through the interfaces in
`export/include/diadraw/export/export.hpp`. Each backend is one source file, picked at configure time, so
replacing one means adding `export/src/png_<name>.cpp` or `pdf_<name>.cpp` and setting the option:

| Option | Values | Default |
| --- | --- | --- |
| `DIADRAW_PNG_BACKEND` | `lunasvg` (MIT, fetched by CMake, system fonts), `none` | `lunasvg` |
| `DIADRAW_PDF_BACKEND` | `builtin` (vector, standard Helvetica, nothing embedded), `none` | `builtin` |

The built-in PDF writer covers the SVG DiaDraw renders (shapes, paths, markers, nested drawings,
opacity, dashes, text); gradients, filters and images in library art are left out.

## Checks beyond the unit tests

```bash
node tools/parity-highlight.mjs     # syntax tokens against the web editor's CodeMirror tokenizer
```

```bash
node tools/import-coverage.mjs corpus/plantuml corpus/structurizr   # what the importers drop
```

Both run from the repo root after `npm run build -w @diadraw/wasm`. `parity-highlight` tokenizes
every corpus file, and the DSL each importable file turns into, with the core and with the web
app's stream tokenizer (2,853 texts, 1.48M tokens, 0 differences). `import-coverage` reports, per
format and PlantUML diagram type, how many files import cleanly and what is dropped most.
`src/wasm` pins the SVG of every view of the sample documents as a snapshot.

### The TypeScript reference, retired

The core began as a port of a TypeScript implementation (`@diadraw/core`, `dsl`, `layout`,
`render`, `import`, `library`, `language-server`). Parity tools compared the two over the corpus
until the TypeScript packages were removed; their last results:

| Tool | Compared | Last result |
| --- | --- | --- |
| `parity-model` | model JSON and `resolveView` of every corpus file | 1,343 files, 0 differences |
| `parity-dsl` | parse, print, source maps, 5,943 random `updateSource` edits | 0 differences |
| `parity-render` | SVG of every `fixed` and `sequence` view, byte for byte | 916 views, 0 differences |
| `parity-import [--stencils]` | every importer, model and report | 1,381 files, 0 differences |
| `parity-export [--format bpmn]` | draw.io and BPMN XML, byte for byte | 1,345 models, 0 differences |
| `parity-regex` | `dd::js::Regex` against V8 on the importers' own calls | 1.5M calls, 0 differences |
| `parity-library [--drawio 1000]` | library builds, semver, palettes with art | 0 differences |
| `parity-lsp`, `parity-lsp-server` | language features and the server (native and WebAssembly) | 681 requests, 0 differences |

The generated tables the core compiles in ([tools/gen/](../tools/gen/)) come from the same sources as
before and were regenerated byte for byte by the C++ generators: built-in libraries
(`builtin_libraries.mjs`, from `assets/builtin/`), C4-PlantUML signatures (`c4_signatures.mjs`,
from [third_party/C4-PlantUML/](../third_party/C4-PlantUML/)) and draw.io's stencil sets
(`drawio_stencils.mjs`, into `assets/drawio/`).

## The layered engine (C4)

`layout/src/layered.cpp` replaces ELK: depth-first cycle breaking, network-simplex layering (pins
along the flow become layering constraints, labelled edges get a label layer), dummy nodes,
layer sweeps with transposition from several starts (order hints and pins across the flow are
kept), Brandes–Köpf coordinates, channel routing with crossing-aware tracks, and recursive layout
of containers. Edges between containers and ports on sides the flow cannot reach are routed
around the nodes afterwards; hints still unmet go through the constraint pass
(`constraints.cpp`, a port of the TypeScript one). With a previous layout, its order seeds the
ordering and wins ties.

`@diadraw/bench` (corpus views plus synthetic flows of 50, 100 and 200 nodes, since removed) ran
on both engines. Totals, C++ against ELK
([full table](layout/bench/c4-vs-elk.md)):

| metric | ELK | C++ | change |
| --- | ---: | ---: | ---: |
| crossings | 616 | 428 | −31% |
| bends | 759 | 765 | +0.8% |
| area (px²) | 36.1 M | 34.0 M | −6% |
| edge length (px) | 474,020 | 365,512 | −23% |
| label overlaps | 0 | 0 | |
| unmet hints | 0 | 0 | |
| drift after an edit (px) | 6,565 | 4,339 | −34% |
| order flips after an edit (%) | 121.4 | 102.8 | −15% |
| time (ms) | 226 | 34 | 6.6× faster |

Every layered view in the corpus (588, from all importers) lays out and renders without error,
in 360 ms in total.

## Layout

```
src/                   CMake project and presets
  <module>/
    include/           public headers, with their existing include paths
    src/               implementation and private headers
    tests/             module tests, where applicable
    bench/             benchmarks, where applicable (layout/bench/)
  apps/                native CLI, app shells, icons and shared shell tests
  wasm/                JavaScript package and C++ embind bindings
  tests/               shared googletest helpers
  tools/               generators, highlighter check and import coverage
assets/                built-in libraries, draw.io stencil sets and notices
```

## Conventions

- Never move an object in one argument of a call and read it in another
  (`map.set(e.id, std::move(e))`): GCC on x86-64 and MSVC evaluate arguments right to left, so
  the read sees a moved-from object. Take what you need first. `python3 tools/check-move-order.py`
  finds these.
- No I/O, threads, clocks or platform headers in the core; hosts pass those in.
- Errors are `dd::Result<T>` values; nothing throws across the public API.
- Models are values: collections share their items between snapshots, so copying a `Model` is
  cheap and an operation only copies what it changes.
- JSON keeps the shapes the TypeScript implementation used; `src/wasm/src/types.ts` describes them.
