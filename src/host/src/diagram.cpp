#include "host/diagram.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "diadraw/base/json.hpp"
#include "diadraw/export/export.hpp"
#include "diadraw/io/bpmn.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/model/views.hpp"
#include "diadraw/render/svg.hpp"
#include "host/files.hpp"
#include "host/libraries.hpp"

namespace dd::host {

namespace fs = std::filesystem;

namespace {

/// Local include resolution: relative to the including file; never the network. A folder
/// includes every file under it, depth first with each folder's entries in name order (as
/// Structurizr reads a folder), leaving out names that start with a dot.
std::optional<std::vector<io::IncludedFile>> include_from_disk(const std::string& path, const std::string& from) {
  std::error_code ec;
  fs::path target = fs::absolute(utf8_path(from).parent_path() / utf8_path(path), ec).lexically_normal();
  if (ec || !fs::exists(target, ec)) return std::nullopt;
  std::vector<std::string> files;
  if (fs::is_directory(target, ec)) {
    std::vector<fs::path> found;
    // Linked folders are not followed (two links back up the tree would never end), and a folder
    // that cannot be read fails the include rather than leaving part of it out.
    fs::recursive_directory_iterator it(target, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      std::error_code entry_ec;
      std::string name = path_utf8(it->path().filename());
      if (name.empty()) continue;  // a Windows name with no UTF-8 spelling
      if (name[0] == '.') {
        if (it->is_directory(entry_ec)) it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file(entry_ec)) found.push_back(it->path());
    }
    if (ec) return std::nullopt;  // a partial folder is not the folder
    std::ranges::sort(found);     // element by element: `people/u.dsl` before `people-rel.dsl`
    for (const auto& p : found) files.push_back(path_utf8(p));
  } else {
    files.push_back(path_utf8(target));
  }
  std::vector<io::IncludedFile> out;
  for (const auto& f : files) {
    auto text = read_file(f);
    if (!text) return std::nullopt;
    out.push_back({f, *text});
  }
  return out;
}

}  // namespace

std::string format_diagnostic(const dsl::Diagnostic& d) {
  return std::to_string(d.line) + ":" + std::to_string(d.column) + ": " +
         (d.severity == dsl::Severity::Error ? "error" : "warning") + ": " + d.message;
}

std::optional<io::Format> format_named(std::string_view name) {
  static const std::map<std::string, io::Format, std::less<>> kFormats{
      {"mermaid", io::Format::Mermaid}, {"plantuml", io::Format::PlantUml}, {"structurizr", io::Format::Structurizr},
      {"drawio", io::Format::Drawio},   {"bpmn", io::Format::Bpmn}};
  auto it = kFormats.find(name);
  return it == kFormats.end() ? std::nullopt : std::optional(it->second);
}

Result<io::ImportResult> import_text(std::string_view data, io::Format format, const std::string& file,
                                     bool keep_geometry) {
  io::ImportOptions options;
  options.keep_geometry = keep_geometry;
  options.drawio.keep_geometry = keep_geometry;
  options.bpmn.keep_geometry = keep_geometry;
  options.file_name = file;
  if (!file.empty())
    options.resolve = [file](const std::string& path, const std::optional<std::string>& from) {
      return include_from_disk(path, from ? *from : file);
    };
  // draw.io stencils draw with the artwork of the drawio-* libraries, as the TypeScript CLI does.
  std::optional<io::DrawioIndex> stencils;
  if (const Json* index = drawio_index())
    if (auto parsed = io::DrawioIndex::from_json(*index)) stencils = std::move(*parsed);
  if (stencils) options.drawio.stencils = &*stencils;
  return io::import_diagram(data, format, options);
}

std::vector<std::string> library_warnings(const Model& model, const library::Resolution& libraries) {
  std::vector<std::string> out;
  for (const auto& u : libraries.unresolved) out.push_back(u.message);
  for (const auto& u : library::unknown_references(model, libraries.set))
    out.push_back(u.element + ": no " + (u.kind ? "kind" : "shape") + " \"" + u.ref + "\" in its library");
  // Library themes the document picks or extends.
  const auto aliases = libraries.set.aliases();
  std::vector<std::string> refs;
  if (model.theme) refs.push_back(*model.theme);
  for (const auto& v : model.views)
    if (v.theme) refs.push_back(*v.theme);
  for (const auto& t : model.themes)
    if (t.base) refs.push_back(*t.base);
  for (const auto& ref : refs) {
    auto dot = ref.find('.');
    if (dot == std::string::npos) continue;
    bool known = std::ranges::find(aliases, ref.substr(0, dot)) != aliases.end();
    if (known && !libraries.set.theme(ref)) out.push_back("no theme \"" + ref + "\" in its library");
  }
  return out;
}

std::string hint_warning(std::string_view hint) {
  return "layout could not honour hint " + std::string(hint);
}

namespace {

/// `source` parsed, with its libraries from `resolution` and what they could not supply.
Analysis analyze_with(dsl::ParseResult parsed, library::Resolution resolution) {
  Analysis out{std::move(parsed), std::move(resolution), {}, 0};
  out.errors = static_cast<int>(std::ranges::count(out.parse.diagnostics, dsl::Severity::Error,
                                                   &dsl::Diagnostic::severity));
  out.library_warnings = library_warnings(out.parse.model, out.libraries);
  return out;
}

}  // namespace

Analysis analyze(std::string_view source, const std::string& file) {
  dsl::ParseResult parsed = dsl::parse(source);
  auto uses = parsed.model.libraries.value_or(std::vector<LibraryUse>{});
  return analyze_with(std::move(parsed), resolve_for_file(uses, file));
}

Analysis analyze(std::string_view source) {
  dsl::ParseResult parsed = dsl::parse(source);
  auto uses = parsed.model.libraries.value_or(std::vector<LibraryUse>{});
  return analyze_with(std::move(parsed), library::resolve_uses(uses, {builtin_source()}));
}

namespace {

/// `no view "id" (views: a, b)`.
std::string no_view_message(const Model& model, std::string_view id) {
  std::string list;
  for (const auto& k : model.views.keys()) list += (list.empty() ? "" : ", ") + k;
  return "no view \"" + std::string(id) + "\" (views: " + list + ")";
}

}  // namespace

Result<LaidOut> layout_view(const Model& model, std::string_view view_id, const LibraryArt& art) {
  if (!model.views.contains(view_id)) return Error{no_view_message(model, view_id)};
  auto view = resolve_view(model, view_id);
  if (!view) return view.error();
  Theme theme = theme_for(*view, art.themes);
  auto geometry = layout(*view, create_measure(theme, art.shapes, art.kinds));
  if (!geometry) return geometry.error();
  return LaidOut{std::move(*view), std::move(theme), std::move(*geometry)};
}

Result<RenderedView> render_view(const Model& model, std::string_view view_id, const LibraryArt& art) {
  auto laid = layout_view(model, view_id, art);
  if (!laid) return laid.error();
  RenderedView out;
  for (const auto& hint : laid->geometry.ignored_hints) out.ignored_hints.push_back(to_json(hint).dump());
  RenderOptions options;
  options.shapes = art.shapes;
  options.kinds = art.kinds;
  options.theme = &laid->theme;
  out.svg = render_svg(laid->view, laid->geometry, options);
  return out;
}

std::variant<Exported, ExportError> export_diagram(const Model& model, ExportFormat format,
                                                   const ExportOptions& options, const LibraryArt& art) {
  using Kind = ExportError::Kind;
  if (options.view && !model.views.contains(*options.view))
    return ExportError{Kind::NotFound, no_view_message(model, *options.view)};

  if (format == ExportFormat::Drawio || format == ExportFormat::Bpmn) {
    std::vector<io::DrawioPage> pages;
    std::vector<std::string> ids = options.view ? std::vector<std::string>{*options.view} : model.views.keys();
    for (const auto& id : ids) {
      auto laid = layout_view(model, id, art);
      if (!laid) return ExportError{Kind::Failed, laid.error().message};
      pages.push_back({id, std::move(laid->geometry.nodes), std::move(laid->geometry.edges)});
    }
    return Exported{format == ExportFormat::Bpmn ? io::export_bpmn(model, pages) : io::export_drawio(model, pages), {}};
  }

  std::unique_ptr<exporting::Rasterizer> rasterizer;
  std::unique_ptr<exporting::PdfWriter> pdf_writer;
  if (auto invalid = check_export_options(format, options)) return *invalid;
  if (format == ExportFormat::Png) {
    rasterizer = exporting::make_rasterizer();
    if (!rasterizer) return ExportError{Kind::Unsupported, "this build has no PNG backend"};
  } else if (format == ExportFormat::Pdf) {
    pdf_writer = exporting::make_pdf_writer();
    if (!pdf_writer) return ExportError{Kind::Unsupported, "this build has no PDF backend"};
  }

  if (model.views.keys().empty()) return ExportError{Kind::NotFound, no_view_message(model, "")};
  const std::string id = options.view ? *options.view : model.views.keys().front();
  auto rendered = render_view(model, id, art);
  if (!rendered) return ExportError{Kind::Failed, rendered.error().message};
  Exported out{std::move(rendered->svg), std::move(rendered->ignored_hints)};
  if (format == ExportFormat::Svg) return out;
  Result<std::string> bytes = rasterizer ? rasterizer->png(out.bytes, {options.scale, options.background})
                                         : pdf_writer->pdf(out.bytes);
  if (!bytes) return ExportError{Kind::Failed, bytes.error().message};
  out.bytes = std::move(*bytes);
  return out;
}

std::optional<ExportError> check_export_options(ExportFormat format, const ExportOptions& options) {
  using Kind = ExportError::Kind;
  if (format != ExportFormat::Png) return std::nullopt;
  if (!std::isfinite(options.scale) || options.scale <= 0 || options.scale > kMaxPngScale)
    return ExportError{Kind::InvalidArgument,
                       "the PNG scale must be above 0 and at most " + js_number_string(kMaxPngScale)};
  if (options.background)
    if (auto colour = exporting::parse_png_background(*options.background); !colour)
      return ExportError{Kind::InvalidArgument, colour.error().message};
  return std::nullopt;
}

std::optional<ExportFormat> export_format_named(std::string_view name) {
  if (name == "svg") return ExportFormat::Svg;
  if (name == "png") return ExportFormat::Png;
  if (name == "pdf") return ExportFormat::Pdf;
  if (name == "drawio") return ExportFormat::Drawio;
  if (name == "bpmn") return ExportFormat::Bpmn;
  return std::nullopt;
}

library::Resolution resolve_for_model(const Model& model, const std::string& file) {
  return resolve_for_file(model.libraries.value_or(std::vector<LibraryUse>{}), file);
}

}  // namespace dd::host
