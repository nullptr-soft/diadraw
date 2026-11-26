#pragma once

// The DiaDraw language server, independent of any transport: JSON-RPC messages go in through
// `handle`, and replies and notifications (diagnostics) come out through the `Send` callback.
// `diadraw lsp` wraps it in stdio framing; the WebAssembly module hands it to JavaScript, where
// the VS Code extension runs it. Diagnostics (syntax, missing libraries, unknown library shapes),
// outline, go to definition, hover and completion. Positions are LSP's: lines and UTF-16 units.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/library/disk.hpp"
#include "diadraw/lsp/features.hpp"

namespace dd::lsp {

/// The libraries a document can use: what it resolved to, and what completion offers.
struct DocumentLibraries {
  library::Resolution resolution;
  std::vector<LibraryContext::Available> available;
};

/// Resolves a document's libraries; `path` is its file, nullopt for unsaved documents.
using LibraryResolver =
    std::function<DocumentLibraries(const std::vector<LibraryUse>& uses, const std::optional<std::string>& path)>;

/// The built-in libraries and `extra` sources (libraries the host loaded), without any disk.
LibraryResolver memory_resolver(std::vector<library::LibrarySource> extra = {});

/// Libraries on disk: the lockfile next to the document, `extra` sources, the cache, the built-in
/// libraries and draw.io's stencil sets. Unsaved documents resolve as if they were in `fallback_dir`.
LibraryResolver disk_resolver(std::shared_ptr<const library::DiskLibraries> disk,
                              std::vector<library::LibrarySource> extra = {}, std::string fallback_dir = "");

/// The file a `file://` URI names; nullopt for other schemes (unsaved documents).
std::optional<std::string> path_of_uri(const std::string& uri);

class Server {
 public:
  using Send = std::function<void(const Json& message)>;

  explicit Server(Send send, LibraryResolver libraries = memory_resolver());

  /// Handles one JSON-RPC request or notification.
  void handle(const Json& message);

  /// True once `shutdown` was requested.
  [[nodiscard]] bool shutting_down() const { return shutdown_; }
  /// Set once `exit` arrived: 0 after `shutdown`, 1 otherwise (as LSP asks).
  [[nodiscard]] std::optional<int> exit_code() const { return exit_code_; }

 private:
  struct Document {
    std::string text;
    int version = 0;
    dsl::ParseResult parsed;
    std::string library_key;
    std::optional<LibraryContext> libraries;
    std::vector<library::UnresolvedLibrary> unresolved;
  };

  void respond(const Json& id, Json result);
  void respond_error(const Json& id, int code, const std::string& message);
  void notify(const std::string& method, Json params);
  void update_libraries(Document& doc, const std::string& uri);
  void publish(const std::string& uri, Document& doc);

  Send send_;
  LibraryResolver libraries_;
  std::map<std::string, Document, std::less<>> documents_;
  bool shutdown_ = false;
  std::optional<int> exit_code_;
};

}  // namespace dd::lsp
