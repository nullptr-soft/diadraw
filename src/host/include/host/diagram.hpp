#pragma once

// Checking and rendering a document the way the `diadraw` command line does, for every host that
// works on files outside an editor: the CLI's `check` and `render`, its MCP server
// (host/mcp.hpp), the C ABI, the Python package (bindings/python) and the Node.js addon
// (bindings/node). Libraries resolve offline, as in host/libraries.hpp.

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/layout/types.hpp"
#include "diadraw/library/library.hpp"
#include "diadraw/model/model.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::host {

/// A parsed document with its libraries.
struct Analysis {
  dsl::ParseResult parse;
  library::Resolution libraries;
  /// What the libraries could not supply: unresolved `use`s, unknown library kinds, shapes and
  /// themes. Messages only, without a file or a severity (they are all warnings).
  std::vector<std::string> library_warnings;
  /// Diagnostics of severity error in `parse`.
  int errors = 0;
};

/// `line:column: error|warning: message`, without a newline.
[[nodiscard]] std::string format_diagnostic(const dsl::Diagnostic& d);

/// Parses `source` and resolves its libraries for `file` (whose folder is where the lockfile is
/// looked for; it need not exist).
[[nodiscard]] Analysis analyze(std::string_view source, const std::string& file);

/// Parses `source` of a document that is not in a file: its libraries resolve from the built-in
/// ones and draw.io's stencil sets only (no lockfile, no cache).
[[nodiscard]] Analysis analyze(std::string_view source);

/// One view resolved and laid out with the theme it draws in.
struct LaidOut {
  ResolvedView view;
  Theme theme;
  Geometry geometry;
};

/// Resolves and lays out view `view_id` of `model` with the libraries' measure and theme. An
/// unknown view's error lists the views there are.
[[nodiscard]] Result<LaidOut> layout_view(const Model& model, std::string_view view_id, const LibraryArt& art);

/// One view laid out and drawn as SVG.
struct RenderedView {
  std::string svg;
  /// Layout hints the layout could not honour, each as its JSON.
  std::vector<std::string> ignored_hints;
};

/// Renders view `view_id` of `model` (which should already have a default view, see
/// `with_default_view`). An unknown view's error lists the views there are.
[[nodiscard]] Result<RenderedView> render_view(const Model& model, std::string_view view_id, const LibraryArt& art);

/// What `export_diagram` writes.
enum class ExportFormat { Svg, Png, Pdf, Drawio, Bpmn };

/// The largest PNG scale `export_diagram` takes.
inline constexpr double kMaxPngScale = 8;

struct ExportOptions {
  /// The view to draw. SVG, PNG and PDF draw the first view without one; draw.io and BPMN write
  /// every view.
  std::optional<std::string> view;
  /// PNG pixel density, above 0 and at most `kMaxPngScale`.
  double scale = 2;
  /// PNG background, `#rgb` or `#rrggbb`; transparent without one. Other formats ignore it.
  std::optional<std::string> background;
};

/// A file's bytes, with the layout hints that could not be honoured (SVG, PNG and PDF).
struct Exported {
  std::string bytes;
  std::vector<std::string> ignored_hints;
};

struct ExportError {
  enum class Kind { InvalidArgument, NotFound, Unsupported, Failed };
  Kind kind = Kind::Failed;
  std::string message;
};

/// Lays out and writes views of `model` (which should already have a default view, see
/// `with_default_view`) as the `diadraw render` command does. Fails with `NotFound` for an unknown
/// view, `Unsupported` when the build has no backend for the format and `InvalidArgument` for a
/// PNG scale out of range or a PNG background that is not a colour.
[[nodiscard]] std::variant<Exported, ExportError> export_diagram(const Model& model, ExportFormat format,
                                                                 const ExportOptions& options, const LibraryArt& art);

/// The options `export_diagram` would refuse for `format` before drawing anything (a PNG scale
/// out of range, a PNG background that is not a colour), as an `InvalidArgument` error; bindings
/// check them before loading a document.
[[nodiscard]] std::optional<ExportError> check_export_options(ExportFormat format, const ExportOptions& options);

/// The export format called `name`: `svg`, `png`, `pdf`, `drawio` or `bpmn`.
[[nodiscard]] std::optional<ExportFormat> export_format_named(std::string_view name);

/// The libraries `model` uses, resolved offline for `file` as `analyze` resolves a source's.
[[nodiscard]] library::Resolution resolve_for_model(const Model& model, const std::string& file);

/// What `libraries` could not supply for `model`: unresolved `use`s, unknown library kinds, shapes
/// and themes (`Analysis::library_warnings` of a source).
[[nodiscard]] std::vector<std::string> library_warnings(const Model& model, const library::Resolution& libraries);

/// The warning for a layout hint the layout could not honour (`Exported::ignored_hints`), worded
/// the same in every host.
[[nodiscard]] std::string hint_warning(std::string_view hint);

/// The import format called `name`: `mermaid`, `plantuml`, `structurizr`, `drawio` or `bpmn`.
[[nodiscard]] std::optional<io::Format> format_named(std::string_view name);

/// Imports a diagram in another format. Includes resolve on disk relative to `file` (never the
/// network); with `file` empty, includes are not read at all and the importer reports them.
/// draw.io stencils draw with the artwork of the drawio-* libraries.
/// `keep_geometry` keeps the positions of formats that have them.
[[nodiscard]] Result<io::ImportResult> import_text(std::string_view data, io::Format format, const std::string& file,
                                     bool keep_geometry);

}  // namespace dd::host
