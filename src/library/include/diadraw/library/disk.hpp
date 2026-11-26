#pragma once

// Libraries on disk, resolved offline: the lockfile (`diadraw-lock.json`) next to a document, the
// library cache and draw.io's converted stencil sets. Everything is read through `FileSystem`,
// which the host supplies (std::filesystem natively, Node's fs from WebAssembly), so the same
// resolution runs in the CLI, the language server, the desktop apps and the VS Code extension.
// Nothing here writes or fetches.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "diadraw/library/library.hpp"

namespace dd::library {

inline constexpr const char* kLockfile = "diadraw-lock.json";

/// What offline resolution reads. Paths are the host's own; the core only joins and walks them.
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  /// A file's bytes; nullopt when it cannot be read.
  [[nodiscard]] virtual std::optional<std::string> read(const std::string& path) const = 0;
  /// The names in a directory, in any order; empty when it is not one.
  [[nodiscard]] virtual std::vector<std::string> list(const std::string& dir) const = 0;
  [[nodiscard]] virtual std::string join(const std::string& dir, const std::string& name) const = 0;
  /// The directory holding `path`; `path` itself (or "") at the root.
  [[nodiscard]] virtual std::string parent(const std::string& path) const = 0;
  /// An environment variable; "" when unset.
  [[nodiscard]] virtual std::string env(const std::string& name) const = 0;
};

class DiskLibraries {
 public:
  /// `drawio_dir` holds draw.io's converted stencil sets (`index.json.gz`, `sets/`); "" for none.
  explicit DiskLibraries(std::shared_ptr<const FileSystem> fs, std::string drawio_dir = "");

  [[nodiscard]] const FileSystem& fs() const { return *fs_; }

  /// `$DIADRAW_CACHE`, else `$XDG_CACHE_HOME/diadraw/libraries`, else `~/.cache/diadraw/libraries`.
  [[nodiscard]] std::string cache_dir() const;
  /// The library cache as a source: `<cache>/<name>/<version>.json`.
  [[nodiscard]] LibrarySource cache_source(const std::string& dir) const;
  /// The built-in libraries and draw.io's stencil sets (each set read when a document needs it).
  [[nodiscard]] LibrarySource builtin_source() const;
  /// The index of draw.io's stencil sets (style names → library shapes), when the sets are found.
  [[nodiscard]] const Json* drawio_index() const;

  /// The nearest `diadraw-lock.json` in `dir` or above it.
  [[nodiscard]] std::optional<std::string> find_lockfile(const std::string& dir) const;
  [[nodiscard]] std::optional<Lockfile> read_lockfile(const std::optional<std::string>& file) const;

  /// The libraries of a document at `file` (an absolute path): lockfile pins, then `extra`
  /// sources, then the cache, then the built-in libraries. Missing libraries come back unresolved.
  [[nodiscard]] Resolution resolve_for_file(const std::vector<LibraryUse>& uses, const std::string& file,
                                            const std::vector<LibrarySource>& extra = {}) const;

  /// Every library in the cache, newest version first per name.
  [[nodiscard]] std::vector<std::pair<std::string, std::vector<std::string>>> list_cache(const std::string& dir) const;

 private:
  struct State;
  std::shared_ptr<const FileSystem> fs_;
  std::string drawio_dir_;
  std::shared_ptr<State> state_;
};

}  // namespace dd::library
