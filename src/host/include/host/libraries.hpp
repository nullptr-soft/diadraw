#pragma once

// Libraries on disk for native hosts (the CLI, the language server, the desktop apps), a port of packages/library/src/node.ts and the `lib`
// commands of packages/cli/src/lib.ts. Rendering and importing only read the cache, the lockfile,
// the built-in libraries and the draw.io stencil sets; nothing here uses the network.

#include <optional>
#include <string>
#include <vector>

#include "diadraw/library/disk.hpp"
#include "diadraw/library/library.hpp"

namespace dd::host {

using library::kLockfile;

/// Offline library resolution over this machine's file system (draw.io sets as `drawio_index`).
const library::DiskLibraries& disk();

/// `$DIADRAW_CACHE`, else `$XDG_CACHE_HOME/diadraw/libraries`, else `~/.cache/diadraw/libraries`.
std::string cache_dir();
/// The library cache as a source: `<cache>/<name>/<version>.json`.
library::LibrarySource cache_source(const std::string& dir);
/// The built-in libraries and draw.io's stencil sets (read from disk when a document needs them).
library::LibrarySource builtin_source();
std::string integrity_of(const Json& bundle);
/// Where draw.io's converted stencil sets are read from (`index.json.gz`, `sets/`), for hosts that
/// ship them somewhere else, such as inside an app bundle. Call before the first lookup.
void set_drawio_dir(const std::string& dir);
/// The index of draw.io's stencil sets (style names → library shapes), when the sets are found.
const Json* drawio_index();

std::optional<std::string> find_lockfile(const std::string& from);

/// Every library in the cache, newest version first per name.
std::vector<std::pair<std::string, std::vector<std::string>>> list_cache(const std::string& dir);

/// The libraries of a document, resolved offline: lockfile pins, then the cache, then the
/// built-in libraries. Missing libraries come back unresolved.
library::Resolution resolve_for_file(const std::vector<LibraryUse>& uses, const std::string& file);

/// `diadraw lib <command>`; returns the exit code.
struct LibOptions {
  std::optional<std::string> output, name, range, drawio, set;
};
int run_lib(const std::vector<std::string>& args, const LibOptions& options);

extern const char* const kLibHelp;

}  // namespace dd::host
