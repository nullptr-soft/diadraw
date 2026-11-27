//! Builds libdiadraw's C ABI (`diadraw_capi`) with CMake and links it statically.
//!
//! By default the C++ core is configured from this checkout (three folders up) into Cargo's
//! `OUT_DIR`, in Release with only the libraries, and the static libraries CMake lists in
//! `diadraw_capi_libs-Release.txt` are linked in that order, followed by the C++ runtime.
//!
//! Environment variables:
//! - `DIADRAW_CAPI_LIBS`: a `diadraw_capi_libs-<config>.txt` from an existing build; nothing is
//!   built and the libraries it lists are linked.
//! - `DIADRAW_SOURCE_DIR`: the DiaDraw checkout to build, instead of this one.
//! - `DIADRAW_CMAKE_ARGS`: extra arguments for the CMake configure step, split on whitespace (for
//!   example `-DFETCHCONTENT_SOURCE_DIR_MINIZ=/src/miniz` to build without network).
//! - `CMAKE`: the cmake executable; `CMAKE_GENERATOR` as CMake reads it.
//! - `MACOSX_DEPLOYMENT_TARGET`: passed on as `CMAKE_OSX_DEPLOYMENT_TARGET` (11.0 on Apple
//!   silicon and 10.15, the oldest with std::filesystem, on Intel when unset).
//! - `DIADRAW_CXX_STDLIB`: the C++ runtime to link (`stdc++`, `c++`), or empty for none.
//!
//! Cross-compiling builds the core for the host, so it is refused, except between macOS
//! architectures; build the core for the target yourself and set `DIADRAW_CAPI_LIBS`.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const CONFIG: &str = "Release";

fn main() {
    for var in [
        "DIADRAW_CAPI_LIBS",
        "DIADRAW_SOURCE_DIR",
        "DIADRAW_CMAKE_ARGS",
        "DIADRAW_CXX_STDLIB",
        "CMAKE",
        "CMAKE_GENERATOR",
        "MACOSX_DEPLOYMENT_TARGET",
    ] {
        println!("cargo:rerun-if-env-changed={var}");
    }

    let list = match env::var_os("DIADRAW_CAPI_LIBS") {
        Some(path) => PathBuf::from(path),
        None => build_with_cmake(),
    };
    println!("cargo:rerun-if-changed={}", list.display());
    link_libraries(&list);
    link_cxx_runtime();
}

/// Configures and builds `diadraw_capi`; returns the list of libraries to link.
fn build_with_cmake() -> PathBuf {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let source = env::var_os("DIADRAW_SOURCE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| manifest.join("../../.."));
    let source = source
        .canonicalize()
        .unwrap_or_else(|e| panic!("DiaDraw source folder {}: {e}", source.display()));
    if !source.join("src/capi/include/diadraw/diadraw.h").exists() {
        panic!(
            "{} is not a DiaDraw checkout; set DIADRAW_SOURCE_DIR or DIADRAW_CAPI_LIBS",
            source.display()
        );
    }
    // Only the C++ sources and the files compiled into the core; not src/ as a whole, which holds
    // the apps and the web packages.
    for path in [
        "CMakeLists.txt",
        "VERSION",
        "cmake",
        "assets/builtin",
        "corpus",
        "src/CMakeLists.txt",
    ] {
        println!("cargo:rerun-if-changed={}", source.join(path).display());
    }
    for module in [
        "base", "canvas", "capi", "dsl", "editor", "export", "host", "io", "layout", "library",
        "lsp", "markdown", "model", "render",
    ] {
        println!(
            "cargo:rerun-if-changed={}",
            source.join("src").join(module).display()
        );
    }

    let target = env::var("TARGET").unwrap();
    let host = env::var("HOST").unwrap();
    let mut target_args = Vec::new();
    if target.contains("apple-darwin") && host.contains("apple-darwin") {
        // One architecture per build, the one rustc links.
        let arch = match env::var("CARGO_CFG_TARGET_ARCH").as_deref() {
            Ok("aarch64") if target.starts_with("arm64e") => "arm64e",
            Ok("aarch64") => "arm64",
            Ok("x86_64") => "x86_64",
            Ok(other) => panic!("diadraw-sys does not know the macOS architecture of {other}"),
            Err(_) => panic!("Cargo did not set CARGO_CFG_TARGET_ARCH"),
        };
        target_args.push(format!("-DCMAKE_OSX_ARCHITECTURES={arch}"));
        // When unset: rustc's default (11.0) on Apple silicon; on Intel 10.15 rather than rustc's
        // 10.12, since the core needs std::filesystem.
        let version = env::var("MACOSX_DEPLOYMENT_TARGET")
            .unwrap_or_else(|_| if arch == "x86_64" { "10.15" } else { "11.0" }.into());
        target_args.push(format!("-DCMAKE_OSX_DEPLOYMENT_TARGET={version}"));
    } else if target != host {
        panic!(
            "diadraw-sys builds the C++ core for the host ({host}) only; to target {target}, build \
             the core for it with CMake and set DIADRAW_CAPI_LIBS to its diadraw_capi_libs-<config>.txt"
        );
    }

    let build = PathBuf::from(env::var("OUT_DIR").unwrap()).join("cmake-build");
    let cmake = env::var("CMAKE").unwrap_or_else(|_| "cmake".into());
    let mut configure = Command::new(&cmake);
    configure
        .arg("-S")
        .arg(&source)
        .arg("-B")
        .arg(&build)
        .arg(format!("-DCMAKE_BUILD_TYPE={CONFIG}"))
        .args([
            "-DDIADRAW_BUILD_TESTS=OFF",
            "-DDIADRAW_BUILD_CLI=OFF",
            "-DDIADRAW_BUILD_GTK=OFF",
            "-DDIADRAW_BUILD_MACOS=OFF",
            "-DDIADRAW_BUILD_WINUI=OFF",
            "-DDIADRAW_SANITIZE=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            // Rust links the release C runtime DLL on Windows, whatever its own profile.
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
            "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
        ])
        .args(&target_args);
    if let Ok(args) = env::var("DIADRAW_CMAKE_ARGS") {
        configure.args(args.split_whitespace());
    }
    run(&mut configure);

    let mut compile = Command::new(&cmake);
    compile
        .arg("--build")
        .arg(&build)
        .args(["--config", CONFIG, "--target", "diadraw_capi"]);
    if let Ok(jobs) = env::var("NUM_JOBS") {
        compile.args(["--parallel", &jobs]);
    }
    run(&mut compile);

    build.join(format!("diadraw_capi_libs-{CONFIG}.txt"))
}

fn run(command: &mut Command) {
    let status = command
        .status()
        .unwrap_or_else(|e| panic!("cannot run {command:?}: {e} (is CMake installed?)"));
    if !status.success() {
        panic!("{command:?} failed with {status}");
    }
}

/// Links every library in `list`, one path per line, in order.
fn link_libraries(list: &Path) {
    let text =
        fs::read_to_string(list).unwrap_or_else(|e| panic!("cannot read {}: {e}", list.display()));
    for line in text.lines().map(str::trim).filter(|l| !l.is_empty()) {
        let path = Path::new(line);
        if !path.exists() {
            panic!(
                "{} lists {}, which does not exist",
                list.display(),
                path.display()
            );
        }
        let dir = path.parent().expect("a library path has a folder");
        let file = path.file_name().unwrap().to_string_lossy();
        let name = file
            .strip_suffix(".lib")
            .or_else(|| file.strip_suffix(".a"))
            .unwrap_or_else(|| panic!("{} is not a static library", path.display()));
        let name = if file.ends_with(".a") {
            name.strip_prefix("lib").unwrap_or(name)
        } else {
            name
        };
        println!("cargo:rerun-if-changed={}", path.display());
        println!("cargo:rustc-link-search=native={}", dir.display());
        println!("cargo:rustc-link-lib=static={name}");
    }
}

/// The C++ standard library the core needs, after the core's own libraries.
fn link_cxx_runtime() {
    if let Ok(lib) = env::var("DIADRAW_CXX_STDLIB") {
        if !lib.is_empty() {
            println!("cargo:rustc-link-lib={lib}");
        }
        return;
    }
    let target = env::var("TARGET").unwrap();
    if target.contains("msvc") {
        return; // the MSVC C++ runtime comes with the C runtime
    }
    if target.contains("apple") || target.contains("freebsd") || target.contains("openbsd") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=m");
    }
}
