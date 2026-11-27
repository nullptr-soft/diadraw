# DiaDraw for Rust

Rust bindings to `libdiadraw`, the DiaDraw C++ core, over its C ABI
([`diadraw.h`](../../src/capi/include/diadraw/diadraw.h)). Two crates:

| Crate | What it is |
| --- | --- |
| [`diadraw-sys`](diadraw-sys) | Raw `extern "C"` declarations; its build script compiles the core with CMake and links it statically |
| [`diadraw`](diadraw) | The safe API: check, format, parse, lay out, render (SVG, PNG, PDF, draw.io, BPMN) and import (Mermaid, PlantUML, Structurizr, draw.io, BPMN), plus an editable `Document` with undo |

```rust
use diadraw::{Format, RenderOptions};

let source = std::fs::read_to_string("corpus/c4-02-banking.draw")?;
let options = RenderOptions { view: Some("containers"), ..Default::default() };
std::fs::write("banking.png", diadraw::render(&source, Format::Png, &options)?)?;
```

Structured results (parse output, diagnostics, layout geometry, import reports) are JSON strings in
the same shape as the `@diadraw/wasm` TypeScript types; parse them with `serde_json` or whatever
you already use. Errors carry a kind (`Rejected` for source with errors, `NotFound` for an unknown
view, and so on) and the core's message.

The core keeps caches without locks, so the bindings take one process-wide lock around every
call: they are safe from any thread, and calls run one at a time. `Document` is `Send`.

## Building

Needs Rust 1.77 or newer, CMake 3.25+ and a C++20 compiler (Xcode's clang on macOS, GCC or Clang on
Linux, Visual Studio 2022 on Windows). From this folder:

```bash
cargo test
cargo run --example diadraw -- render ../../corpus/c4-02-banking.draw banking.svg containers
```

The first build configures the core from this checkout into Cargo's target folder (Release, only
the libraries, nothing installed) and downloads its pinned dependencies (miniz, lunasvg) from
github.com, as every CMake build does. To use one in another project, depend on it by path or git:

```toml
[dependencies]
diadraw = { git = "https://github.com/mykulyak/diadraw" }
```

| Variable | Effect |
| --- | --- |
| `DIADRAW_CAPI_LIBS` | Link an existing build instead: the `diadraw_capi_libs-<config>.txt` a CMake build of the repository writes, listing its static libraries in link order |
| `DIADRAW_SOURCE_DIR` | Build another DiaDraw checkout |
| `DIADRAW_CMAKE_ARGS` | Extra CMake configure arguments, split on whitespace (e.g. `-G Ninja`, or `-DFETCHCONTENT_SOURCE_DIR_MINIZ=…` to build offline) |
| `DIADRAW_CXX_STDLIB` | The C++ runtime to link (`stdc++`, `c++`, or empty for none) when the default for the target is wrong |

On Intel Macs the core needs macOS 10.15 (for `std::filesystem`), newer than rustc's default of
10.12: set `MACOSX_DEPLOYMENT_TARGET=10.15` so that Rust and the core agree, or the linker warns
once per object.

The crates are not on crates.io: the build compiles the C++ tree around them, which a published
crate would have to carry inside its package.
