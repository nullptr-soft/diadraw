// embind bindings: the `@diadraw/wasm` surface.
//
// Structured values cross as JSON strings for now, in the same shape as the TypeScript objects, so
// the existing TypeScript types describe them. Scenes and geometry will cross as typed arrays once
// they exist, since those are large and drawn every frame.

#include <emscripten/bind.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/base/result.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/format.hpp"
#include "diadraw/dsl/json.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/io/drawio_stencil.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/library/builtin.hpp"
#include "diadraw/library/library.hpp"
#include "diadraw/library/disk.hpp"
#include "diadraw/lsp/features.hpp"
#include "diadraw/lsp/server.hpp"
#include "diadraw/layout/json.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/render/svg.hpp"
#include "diadraw/model/json.hpp"

namespace {

std::string version() { return dd_version(); }

std::string error_json(const std::string& message) {
  return dd::Json(dd::Json::Object{{"error", message}}).dump();
}

/// Element ids sent as `["id", …]`; anything else (not an array, or an item that is not a string)
/// is an error. Duplicates and unknown ids are passed on for the session to handle.
dd::Result<std::vector<std::string>> parse_ids(const std::string& ids_json) {
  auto json = dd::Json::parse(ids_json);
  if (!json) return json.error();
  const dd::Json& list = *json;
  if (!list.is_array()) return dd::fail("Ids must be a JSON array of strings");
  std::vector<std::string> ids;
  ids.reserve(list.as_array().size());
  for (const auto& item : list.as_array()) {
    if (!item.is_string()) return dd::fail("Ids must be a JSON array of strings");
    ids.push_back(item.as_string());
  }
  return ids;
}

/// `parse(source)`: `{ model, diagnostics, sourceMap, operations }` with UTF-16 offsets.
std::string parse(const std::string& source) {
  return dd::dsl::to_json(dd::dsl::parse(source), source).dump();
}

/// `print(modelJson)`: canonical source, or `{"error": …}` JSON when the model is malformed.
std::string print(const std::string& model_json) {
  auto json = dd::Json::parse(model_json);
  if (!json) return error_json(json.error().message);
  auto model = dd::model_from_json(*json);
  if (!model) return error_json(model.error().message);
  return dd::dsl::print(*model);
}

/// `updateSource(source, operationJson)`: `{ source, edits, model, minimal, diagnostics }`.
std::string update_source(const std::string& source, const std::string& operation_json) {
  auto json = dd::Json::parse(operation_json);
  if (!json) return error_json(json.error().message);
  auto op = dd::operation_from_json(*json);
  if (!op) return error_json(op.error().message);
  auto update = dd::dsl::update_source(source, *op);
  if (!update) return error_json(update.error().message);
  return dd::dsl::to_json(*update, source).dump();
}

// ── Libraries ─────────────────────────────────────────────────────────────────────────────────

/// Bundles the host loaded (draw.io stencil sets, a user's own libraries), next to the built-ins.
std::vector<std::shared_ptr<const dd::Json>>& added_libraries() {
  static std::vector<std::shared_ptr<const dd::Json>> bundles;
  return bundles;
}

/// The added bundles as a source that sees bundles added later too.
dd::library::LibrarySource added_source() {
  dd::library::LibrarySource s;
  s.versions = [](const std::string& name) { return dd::library::memory_source(added_libraries()).versions(name); };
  s.load = [](const std::string& name, const std::string& version) {
    return dd::library::memory_source(added_libraries()).load(name, version);
  };
  return s;
}

// ── The host's file system ────────────────────────────────────────────────────────────────────

/// A file system the JavaScript host provides (Node's fs in the VS Code extension), for libraries
/// on disk: `{ read(path) → Uint8Array | undefined, list(dir) → string[], join(dir, name),
/// parent(path), env(name) → string | undefined }`.
class JsFileSystem final : public dd::library::FileSystem {
 public:
  explicit JsFileSystem(emscripten::val host) : host_(std::move(host)) {}

  std::optional<std::string> read(const std::string& path) const override {
    emscripten::val bytes = host_.call<emscripten::val>("read", path);
    if (bytes.isUndefined() || bytes.isNull()) return std::nullopt;
    if (bytes.isString()) return bytes.as<std::string>();
    std::string out(bytes["length"].as<std::size_t>(), '\0');
    emscripten::val(emscripten::typed_memory_view(out.size(), reinterpret_cast<std::uint8_t*>(out.data())))
        .call<void>("set", bytes);
    return out;
  }
  std::vector<std::string> list(const std::string& dir) const override {
    emscripten::val names = host_.call<emscripten::val>("list", dir);
    std::vector<std::string> out;
    if (names.isUndefined() || names.isNull()) return out;
    for (unsigned i = 0, n = names["length"].as<unsigned>(); i < n; ++i) out.push_back(names[i].as<std::string>());
    return out;
  }
  std::string join(const std::string& dir, const std::string& name) const override {
    return host_.call<std::string>("join", dir, name);
  }
  std::string parent(const std::string& path) const override { return host_.call<std::string>("parent", path); }
  std::string env(const std::string& name) const override {
    emscripten::val v = host_.call<emscripten::val>("env", name);
    return v.isString() ? v.as<std::string>() : "";
  }

 private:
  emscripten::val host_;
};

std::shared_ptr<const dd::library::DiskLibraries>& disk() {
  static std::shared_ptr<const dd::library::DiskLibraries> libraries;
  return libraries;
}

/// `setFileSystem(host, drawioDir)`: libraries on disk (lockfile, cache, draw.io sets) for the
/// language server and sessions of saved files; `setFileSystem(undefined, "")` forgets it.
void set_file_system(emscripten::val host, const std::string& drawio_dir) {
  if (host.isUndefined() || host.isNull())
    disk().reset();
  else
    disk() = std::make_shared<dd::library::DiskLibraries>(std::make_shared<JsFileSystem>(std::move(host)), drawio_dir);
}

/// The libraries of a document at `path` ("" when unsaved): on disk when the host gave a file
/// system, otherwise the added and built-in ones.
dd::library::Resolution resolve_libraries_for(const dd::Model& model, const std::string& path) {
  auto uses = model.libraries.value_or(std::vector<dd::LibraryUse>{});
  if (disk() && !path.empty()) return disk()->resolve_for_file(uses, path, {added_source()});
  return dd::library::resolve_uses(uses, {added_source(), dd::library::builtin_source()});
}

/// The libraries a model uses, resolved against the added bundles and the built-in ones.
dd::library::Resolution resolve_libraries(const dd::Model& model) {
  std::vector<dd::library::LibrarySource> sources{dd::library::memory_source(added_libraries()),
                                                  dd::library::builtin_source()};
  return dd::library::resolve_uses(model.libraries.value_or(std::vector<dd::LibraryUse>{}), sources);
}

/// `addLibrary(bundleJson)`: makes a built library available to documents; `{}` or `{ error }`.
std::string add_library(const std::string& bundle_json) {
  auto bundle = dd::library::parse_bundle(bundle_json);
  if (!bundle) return error_json("Not a DiaDraw library");
  added_libraries().push_back(std::make_shared<const dd::Json>(std::move(*bundle)));
  return "{}";
}

dd::Json unresolved_json(const std::vector<dd::library::UnresolvedLibrary>& unresolved) {
  dd::Json::Array out;
  for (const auto& u : unresolved)
    out.emplace_back(dd::Json::Object{{"spec", dd::Json(dd::library_spec(u.use))}, {"message", dd::Json(u.message)}});
  return dd::Json(std::move(out));
}

/// `libraryPalette(modelJson)`: `{ items, unresolved }` for the libraries the model uses.
std::string library_palette(const std::string& model_json) {
  auto json = dd::Json::parse(model_json);
  if (!json) return error_json(json.error().message);
  auto model = dd::model_from_json(*json);
  if (!model) return error_json(model.error().message);
  auto r = resolve_libraries(*model);
  dd::Json::Array items;
  for (const auto& item : r.set.palette()) items.push_back(dd::library::to_json(item));
  return dd::Json(dd::Json::Object{{"items", dd::Json(std::move(items))}, {"unresolved", unresolved_json(r.unresolved)}})
      .dump();
}

/// `expandFragment(modelJson, alias, id, valuesJson)`: `{ text }` or `{ error }`.
std::string expand_fragment(const std::string& model_json, const std::string& alias, const std::string& id,
                            const std::string& values_json) {
  auto json = dd::Json::parse(model_json);
  if (!json) return error_json(json.error().message);
  auto model = dd::model_from_json(*json);
  if (!model) return error_json(model.error().message);
  std::map<std::string, std::string, std::less<>> values;
  if (auto v = dd::Json::parse(values_json); v && v->is_object())
    for (const auto& [k, x] : v->as_object())
      if (x.is_string()) values[k] = x.as_string();
  auto text = resolve_libraries(*model).set.expand(alias, id, values);
  if (!text) return error_json("No fragment " + alias + "." + id);
  return dd::Json(dd::Json::Object{{"text", dd::Json(*text)}}).dump();
}

/// `buildLibrary(manifestJson, read)`: builds a library from its manifest, reading files with
/// `read(path)` (text, or undefined). `{ bundle?, text?, issues }`.
std::string build_library(const std::string& manifest_json, emscripten::val read) {
  auto manifest = dd::Json::parse(manifest_json);
  if (!manifest) return error_json(manifest.error().message);
  auto built = dd::library::build_bundle(*manifest, [&](const std::string& path) -> std::optional<std::string> {
    emscripten::val text = read(path);
    if (!text.isString()) return std::nullopt;
    return text.as<std::string>();
  });
  dd::Json::Array issues;
  for (const auto& i : built.issues) {
    dd::Json issue(dd::Json::Object{{"severity", dd::Json(i.error ? "error" : "warning")}, {"message", dd::Json(i.message)}});
    if (i.path) issue.set("path", dd::Json(*i.path));
    issues.push_back(std::move(issue));
  }
  dd::Json out(dd::Json::Object{{"issues", dd::Json(std::move(issues))}});
  if (built.bundle) {
    out.set("text", dd::Json(dd::library::serialize_bundle(*built.bundle)));
    auto license = dd::library::check_license(*built.bundle);
    out.set("license", dd::Json(dd::Json::Object{{"status", dd::Json(dd::library::to_string(license.status))},
                                                 {"reason", dd::Json(license.reason)}}));
  }
  return out.dump();
}

/// `libraryList()`: `[{ name, version, description?, builtin }]`, the built-in libraries and the
/// ones the host added.
std::string library_list() {
  dd::Json::Array out;
  auto entry = [](const dd::Json& b, bool builtin) {
    dd::Json e(dd::Json::Object{{"name", b.get("name") ? *b.get("name") : dd::Json("")},
                                {"version", b.get("version") ? *b.get("version") : dd::Json("")}});
    if (const dd::Json* d = b.get("description"); d && d->is_string()) e.set("description", *d);
    e.set("builtin", dd::Json(builtin));
    return e;
  };
  for (const auto& b : dd::library::builtin_bundles().as_array()) out.push_back(entry(b, true));
  for (const auto& b : added_libraries()) out.push_back(entry(*b, false));
  return dd::Json(std::move(out)).dump();
}

/// `foldRanges(text)`: the blocks a text pane can fold, `[{ from, to, startLine, endLine }]`, the
/// hidden text in UTF-16 offsets.
std::string fold_ranges(const std::string& text) { return dd::dsl::fold_ranges_json(text).dump(); }

/// `highlight(text)`: the syntax tokens as `[from, to, tag]` triples, with UTF-16 offsets and
/// CodeMirror tag names.
std::string highlight(const std::string& text) { return dd::dsl::highlight_json(text).dump(); }

/// `formatSource(text, indentSize)`: the text reindented and respaced (dsl::format_source).
std::string format_source(const std::string& text, int indent_size) {
  return dd::dsl::format_source(text, dd::dsl::FormatOptions{indent_size});
}

/// `renderView(modelJson, viewId)`: `{ svg }` for a fixed or sequence view, else `{ error }`.
/// Lays the view out with the renderer's measure and the default theme, as the CLI does, drawing
/// library shapes from the libraries the model uses.
std::string render_view(const std::string& model_json, const std::string& view_id) {
  auto json = dd::Json::parse(model_json);
  if (!json) return error_json(json.error().message);
  auto model = dd::model_from_json(*json);
  if (!model) return error_json(model.error().message);
  auto view = dd::resolve_view(dd::with_default_view(*model), view_id);
  if (!view) return error_json(view.error().message);
  dd::LibraryArt art = resolve_libraries(*model).set.renderer();
  dd::Theme theme = dd::theme_for(*view, art.themes);
  auto geometry = dd::layout(*view, dd::create_measure(theme, art.shapes, art.kinds));
  if (!geometry) return error_json(geometry.error().message);
  dd::RenderOptions options;
  options.shapes = art.shapes;
  options.kinds = art.kinds;
  options.theme = &theme;
  return dd::Json(dd::Json::Object{{"svg", dd::render_svg(*view, *geometry, options)}}).dump();
}

/// The draw.io stencil index, once the host hands it over (`loadDrawioIndex`).
std::optional<dd::io::DrawioIndex>& drawio_index() {
  static std::optional<dd::io::DrawioIndex> index;
  return index;
}

/// `loadDrawioIndex(json)`: the index of draw.io stencil sets (index.json, already gunzipped).
std::string load_drawio_index(const std::string& json) {
  auto parsed = dd::Json::parse(json);
  if (!parsed) return error_json(parsed.error().message);
  auto index = dd::io::DrawioIndex::from_json(*parsed);
  if (!index) return error_json(index.error().message);
  drawio_index() = std::move(*index);
  return "{}";
}

/// `importDiagram(text, fileName, stencils, resolve)`: `{ model, report }`, or `{ error }` for an
/// unknown or not yet supported format. `resolve(path, from)` reads local includes, or is undefined.
std::string import_diagram(const std::string& data, const std::string& file_name, bool stencils,
                          emscripten::val resolve) {
  auto format = dd::io::detect_format(file_name, data);
  if (!format) return error_json("Cannot tell the format of " + file_name);
  dd::io::ImportOptions options;
  if (stencils && drawio_index()) options.drawio.stencils = &*drawio_index();
  options.file_name = file_name;
  if (resolve.typeOf().as<std::string>() == "function") {
    // `resolve(path, from)`: `[{ path, text }]`, or undefined when missing.
    options.resolve = [resolve](const std::string& path, const std::optional<std::string>& from)
        -> std::optional<std::vector<dd::io::IncludedFile>> {
      emscripten::val found = resolve(path, from ? emscripten::val(*from) : emscripten::val::undefined());
      if (found.isUndefined() || found.isNull()) return std::nullopt;
      std::vector<dd::io::IncludedFile> files;
      const auto n = found["length"].as<unsigned>();
      for (unsigned i = 0; i < n; ++i) {
        emscripten::val f = found[i];
        files.push_back({f["path"].as<std::string>(), f["text"].as<std::string>()});
      }
      return files;
    };
  }
  auto r = dd::io::import_diagram(data, *format, options);
  if (!r) return error_json(r.error().message);
  return dd::io::to_json(*r).dump();
}

/// `exportDrawio(modelJson, pagesJson)`: draw.io XML. Pages are `[{ view, nodes, edges }]` with
/// the shapes of `Geometry`.
/// Export pages from JSON: `[{ view, nodes, edges }]` with the shapes of `Geometry`.
std::optional<std::vector<dd::io::DrawioPage>> pages_from_json(const std::string& pages_json) {
  auto pj = dd::Json::parse(pages_json);
  if (!pj || !pj->is_array()) return std::nullopt;
  auto num = [](const dd::Json* j) { return j && j->is_number() ? j->as_number() : 0.0; };
  std::vector<dd::io::DrawioPage> pages;
  for (const auto& p : pj->as_array()) {
    dd::io::DrawioPage page;
    if (const dd::Json* v = p.get("view"); v && v->is_string()) page.view = v->as_string();
    if (const dd::Json* nodes = p.get("nodes"); nodes && nodes->is_object())
      for (const auto& [id, b] : nodes->as_object())
        page.nodes.set(id, dd::Box{num(b.get("x")), num(b.get("y")), num(b.get("width")), num(b.get("height"))});
    if (const dd::Json* edges = p.get("edges"); edges && edges->is_object())
      for (const auto& [id, e] : edges->as_object()) {
        dd::EdgeRoute r;
        if (const dd::Json* pts = e.get("points"); pts && pts->is_array())
          for (const auto& pt : pts->as_array()) r.points.push_back({num(pt.get("x")), num(pt.get("y"))});
        page.edges.set(id, std::move(r));
      }
    pages.push_back(std::move(page));
  }
  return pages;
}

dd::Json pages_to_json(const std::vector<dd::io::DrawioPage>& pages) {
  dd::Json::Array out;
  for (const auto& page : pages) {
    dd::Json::Object nodes, edges;
    for (const auto& id : page.nodes.keys()) {
      nodes.emplace_back(id, dd::to_json(*page.nodes.find(id)));
    }
    for (const auto& id : page.edges.keys()) {
      dd::Json::Array points;
      for (const auto& p : page.edges.find(id)->points) points.emplace_back(dd::Json::Object{{"x", p.x}, {"y", p.y}});
      edges.emplace_back(id, dd::Json(dd::Json::Object{{"points", std::move(points)}}));
    }
    out.emplace_back(dd::Json::Object{{"view", page.view}, {"nodes", std::move(nodes)}, {"edges", std::move(edges)}});
  }
  return dd::Json(std::move(out));
}

/// `exportDrawio(modelJson, pagesJson)` / `exportBpmn(modelJson, pagesJson)`: `{ xml }` or `{ error }`.
template <std::string (*Export)(const dd::Model&, const std::vector<dd::io::DrawioPage>&)>
std::string export_pages(const std::string& model_json, const std::string& pages_json) {
  auto mj = dd::Json::parse(model_json);
  if (!mj) return error_json(mj.error().message);
  auto model = dd::model_from_json(*mj);
  if (!model) return error_json(model.error().message);
  auto pages = pages_from_json(pages_json);
  if (!pages) return error_json("pages must be an array");
  return dd::Json(dd::Json::Object{{"xml", Export(*model, *pages)}}).dump();
}

/// `bpmnFixedPages(modelJson)`: the pages of views with fixed geometry, as `exportBpmn` takes them.
std::string bpmn_fixed_pages(const std::string& model_json) {
  auto mj = dd::Json::parse(model_json);
  if (!mj) return error_json(mj.error().message);
  auto model = dd::model_from_json(*mj);
  if (!model) return error_json(model.error().message);
  return dd::Json(dd::Json::Object{{"pages", pages_to_json(dd::io::fixed_pages(*model))}}).dump();
}

dd::Json strings_json(const std::vector<std::string>& v) {
  dd::Json out = dd::Json::array();
  for (const auto& s : v) out.push(s);
  return out;
}

/// The document facade for UIs: source text, parse, layouts and canvas gestures. Every method
/// that changes the document returns the text update (`{ source, edits, model, minimal,
/// diagnostics }`, offsets in UTF-16) so the text editor can apply it as a transaction, and
/// tells the listener: `listener(type, json)` with `change` (`{ update, state }`, `update` null
/// when the host set the text) or `libraries` (`{}`: shapes were resolved again).
class Session {
 public:
  Session(const std::string& source, const std::string& path, emscripten::val listener)
      : s_(source, [path](const dd::Model& model) { return resolve_libraries_for(model, path).set.renderer(); }),
        listener_(std::move(listener)) {
    if (listener_.isUndefined() || listener_.isNull()) return;
    s_.set_listener([this](const dd::editor::SessionEvent& e) {
      if (e.kind == dd::editor::SessionEvent::Kind::Libraries) {
        listener_(std::string("libraries"), std::string("{}"));
        return;
      }
      std::string before(e.before);
      dd::Json event(dd::Json::Object{{"update", e.update ? dd::dsl::to_json(*e.update, before) : dd::Json()},
                                      {"state", state_json()}});
      listener_(std::string("change"), event.dump());
    });
  }

  [[nodiscard]] std::string source() const { return s_.source(); }
  /// Draws library shapes again after the host added libraries.
  void refreshLibraries() { s_.refresh_libraries(); }
  void setSource(const std::string& source) { s_.set_source(source); }

  /// `{ model, diagnostics, views: [{ id, algorithm }] }` for the panes around the canvas.
  [[nodiscard]] std::string state() const { return state_json().dump(); }

  /// `{ content, viewBox, width, height, algorithm, nodes, edges, rotations, ignoredHints, elements,
  /// relationships, ends }`.
  std::string render(const std::string& view_id) {
    auto r = s_.render(view_id);
    if (!r) return error_json(r.error().message);
    dd::Json nodes = dd::Json::object(), edges = dd::Json::object(), ignored = dd::Json::array();
    for (const auto& id : r->geometry.nodes.keys()) nodes.add(id, dd::to_json(*r->geometry.nodes.find(id)));
    for (const auto& id : r->geometry.edges.keys()) edges.add(id, dd::to_json(*r->geometry.edges.find(id)));
    for (const auto& h : r->geometry.ignored_hints) ignored.push(dd::to_json(h));
    dd::Json rotations = dd::Json::object();
    for (const auto& id : r->geometry.rotations.keys()) rotations.add(id, *r->geometry.rotations.find(id));
    std::vector<std::string> elements, relationships, parents;
    for (const auto& e : r->view.elements) {
      elements.push_back(e.id);
      if (e.parent) parents.push_back(*e.parent);
    }
    dd::Json ends = dd::Json::object();
    for (const auto& rel : r->view.relationships) {
      relationships.push_back(rel.id);
      if (!rel.implied)
        ends.add(rel.id, dd::Json(dd::Json::Object{{"source", rel.source.element}, {"target", rel.target.element}}));
    }
    return dd::Json(dd::Json::Object{{"content", r->scene.content},
                                     {"viewBox", r->scene.view_box},
                                     {"width", r->scene.width},
                                     {"height", r->scene.height},
                                     {"algorithm", r->view.view.layout.algorithm},
                                     {"nodes", nodes},
                                     {"edges", edges},
                                     {"rotations", rotations},
                                     {"ignoredHints", ignored},
                                     {"elements", strings_json(elements)},
                                     {"containers", strings_json(parents)},
                                     {"relationships", strings_json(relationships)},
                                     {"ends", ends}})
        .dump();
  }

  std::string apply(const std::string& operation_json) {
    auto json = dd::Json::parse(operation_json);
    if (!json) return error_json(json.error().message);
    auto op = dd::operation_from_json(*json);
    if (!op) return error_json(op.error().message);
    std::string before = s_.source();  // a copy: apply() replaces the text the edits refer to
    return update(before, s_.apply(*op));
  }

  std::string drop(const std::string& view_id, const std::string& id, double x, double y, double width, double height,
                   bool freeform) {
    std::string before = s_.source();
    auto r = s_.drop(view_id, id, dd::Box{x, y, width, height}, freeform);
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// A node resized (`pin … size` in a fixed view, `size …` in others): the text update, or `null`
  /// when nothing changed.
  std::string resize(const std::string& view_id, const std::string& id, double x, double y, double width,
                     double height) {
    std::string before = s_.source();
    auto r = s_.resize(view_id, id, dd::Box{x, y, width, height});
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// Elements sized to fit their content (`ids_json`: `["id", …]`; their `size` hints and the sizes
  /// of their pins go): the text update, or `null` when nothing changed.
  std::string fit(const std::string& view_id, const std::string& ids_json) {
    auto ids = parse_ids(ids_json);
    if (!ids) return error_json(ids.error().message);
    std::string before = s_.source();
    auto r = s_.fit(view_id, *ids);
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// Several elements dragged together by (`dx`, `dy`) (`ids_json`: `["id", …]`), as one change
  /// (see `move_operation`): the text update, or `null` when nothing changed. A non-finite offset
  /// is an error.
  std::string move(const std::string& view_id, const std::string& ids_json, double dx, double dy) {
    auto ids = parse_ids(ids_json);
    if (!ids) return error_json(ids.error().message);
    std::string before = s_.source();
    auto r = s_.move(view_id, *ids, dd::Point{dx, dy});
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// A node turned `degrees` clockwise (`rotate …`; 0 removes it): the text update, or `null`.
  std::string rotate(const std::string& view_id, const std::string& id, double degrees) {
    std::string before = s_.source();
    auto r = s_.rotate(view_id, id, degrees);
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// Bend points dragged in a fixed view (`[{ x, y }, …]`): the text update, or `null`.
  std::string reroute(const std::string& view_id, const std::string& id, const std::string& points_json) {
    auto json = dd::Json::parse(points_json);
    if (!json || !json->is_array()) return error_json("Bend points must be a JSON array");
    std::vector<dd::Point> points;
    for (const auto& p : json->as_array()) {
      const dd::Json* x = p.get("x");
      const dd::Json* y = p.get("y");
      if (!x || !y || !x->is_number() || !y->is_number()) return error_json("A bend point needs x and y");
      points.push_back({x->as_number(), y->as_number()});
    }
    std::string before = s_.source();
    auto r = s_.reroute(view_id, id, points);
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  /// The hint a drop would write (JSON), or `null`.
  std::string dropHint(const std::string& view_id, const std::string& id, double x, double y, double width,
                       double height, bool freeform) {
    auto r = s_.drop_hint(view_id, id, dd::Box{x, y, width, height}, freeform);
    if (!r) return error_json(r.error().message);
    return *r ? dd::to_json(**r).dump() : "null";
  }

  std::string exportSvg(const std::string& view_id) {
    auto r = s_.export_svg(view_id);
    if (!r) return error_json(r.error().message);
    return dd::Json(dd::Json::Object{{"svg", *r}}).dump();
  }

  std::string connect(const std::string& source, const std::string& target, const std::string& label) {
    std::string before = s_.source();
    return update(before, s_.connect(source, target, label.empty() ? std::nullopt : std::optional(label)));
  }

  /// `{ id, update }` (`id` is the relationship's id afterwards), or `null` when the end is
  /// already on `element`. `end` is "source" or "target".
  std::string reconnect(const std::string& id, const std::string& end, const std::string& element) {
    if (end != "source" && end != "target") return error_json("End must be \"source\" or \"target\"");
    auto which = end == "source" ? dd::editor::End::Source : dd::editor::End::Target;
    std::string before = s_.source();
    auto r = s_.reconnect(id, which, dd::Endpoint{element, std::nullopt});
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::Json(dd::Json::Object{{"id", (*r)->id}, {"update", dd::dsl::to_json((*r)->update, before)}}).dump();
  }

  /// `{ id, update }`
  std::string create(const std::string& kind, const std::string& parent, const std::string& name,
                     const std::string& shape) {
    std::string before = s_.source();
    auto r = s_.create(kind, parent.empty() ? std::nullopt : std::optional(parent),
                       name.empty() ? std::nullopt : std::optional(name),
                       shape.empty() ? std::nullopt : std::optional(shape));
    if (!r) return error_json(r.error().message);
    return dd::Json(dd::Json::Object{{"id", r->first}, {"update", dd::dsl::to_json(r->second, before)}}).dump();
  }

  /// `selectionJson`: `{ elements: [...], relationships: [...] }`.
  std::string remove(const std::string& selection_json) {
    auto json = dd::Json::parse(selection_json);
    if (!json) return error_json(json.error().message);
    dd::editor::Selection selection;
    auto read = [&](const char* key, std::vector<std::string>& into) {
      if (const dd::Json* list = json->get(key); list && list->is_array())
        for (const auto& item : list->as_array())
          if (item.is_string()) into.push_back(item.as_string());
    };
    read("elements", selection.elements);
    read("relationships", selection.relationships);
    std::string before = s_.source();
    auto r = s_.remove(selection);
    if (!r) return error_json(r.error().message);
    if (!*r) return "null";
    return dd::dsl::to_json(**r, before).dump();
  }

  // Views (see dd::editor::add_view_operation and friends). Names are trimmed as typed.

  /// A free id for a new view: `view1`, `view2`, …
  std::string freshViewId() const { return dd::editor::fresh_view_id(s_.model()); }

  /// Adds view `id` (of `scope`, "" for the whole model); writes the implied default view first
  /// when the text declares none.
  std::string addView(const std::string& id, const std::string& scope) {
    auto op = dd::editor::add_view_operation(s_.parsed().model, std::string(dd::trim(id)),
                                             scope.empty() ? std::nullopt : std::optional(scope));
    if (!op) return error_json(op.error().message);
    std::string before = s_.source();  // a copy: apply() replaces the text the edits refer to
    return update(before, s_.apply(*op));
  }

  std::string removeView(const std::string& id) {
    auto op = dd::editor::remove_view_operation(s_.parsed().model, id);
    if (!op) return error_json(op.error().message);
    std::string before = s_.source();  // a copy: apply() replaces the text the edits refer to
    return update(before, s_.apply(*op));
  }

  /// The text update, or `null` when the name stays.
  std::string renameView(const std::string& id, const std::string& to) {
    auto op = dd::editor::rename_view_operation(s_.parsed().model, id, std::string(dd::trim(to)));
    if (!op) return error_json(op.error().message);
    if (!*op) return "null";
    std::string before = s_.source();  // a copy: apply() replaces the text the edits refer to
    return update(before, s_.apply(**op));
  }

  /// Renames an element (see dd::editor::rename_element): `to` replaces the last part of its id and
  /// every reference follows. `{ id, update }`, with `id` the element's id afterwards and `update`
  /// null when the name stays.
  std::string renameElement(const std::string& id, const std::string& to) {
    std::string before = s_.source();  // a copy: the rename replaces the text the edits refer to
    auto r = s_.rename_element(id, dd::trim(to));
    if (!r) return error_json(r.error().message);
    if (!*r) return dd::Json(dd::Json::Object{{"id", id}, {"update", dd::Json()}}).dump();
    return dd::Json(dd::Json::Object{{"id", (*r)->id}, {"update", dd::dsl::to_json((*r)->update, before)}}).dump();
  }

  /// The fields an inspector's edit of an element would refuse (see dd::editor::element_problems),
  /// without changing anything: `{ field: message }`, `{}` when it applies. `element_json` is the
  /// element with the typed fields applied, `local_id` the Id field as typed.
  std::string elementProblems(const std::string& element_json, const std::string& local_id) const {
    auto json = dd::Json::parse(element_json);
    if (!json) return error_json(json.error().message);
    auto element = dd::element_from_json(*json);
    if (!element) return error_json(element.error().message);
    return problems_json(dd::editor::element_problems(s_.source(), s_.parsed(), *element, dd::trim(local_id)));
  }

  /// The same for a relationship (see dd::editor::relationship_problems): `name` as
  /// updateRelationship takes it.
  std::string relationshipProblems(const std::string& relationship_json, const std::string& name) const {
    auto json = dd::Json::parse(relationship_json);
    if (!json) return error_json(json.error().message);
    auto relationship = dd::relationship_from_json(*json);
    if (!relationship) return error_json(relationship.error().message);
    return problems_json(dd::editor::relationship_problems(s_.model(), *relationship,
                                                           name.empty() ? std::nullopt : std::optional(name)));
  }

  /// The element whose name is written at `offset` (UTF-16, as the text editor counts), in its
  /// declaration or a reference to it; "" elsewhere.
  std::string elementAt(double offset) {
    if (!(offset >= 0)) return "";
    const std::string& text = s_.source();
    // UTF-16 units never outnumber UTF-8 bytes; clamped, the cast below is defined (Infinity too).
    offset = std::min(offset, static_cast<double>(text.size()));
    auto target = dd::editor::rename_target(s_.parsed(), dd::byte_offset(text, static_cast<std::size_t>(offset)));
    return target ? target->element : "";
  }

  /// An inspector's edit of a relationship (see dd::editor::update_relationship_operation): `name`
  /// is its explicit id, "" for one derived from its ends. `{ id, update }`, with `id` the
  /// relationship's id afterwards and `update` null when nothing changed.
  std::string updateRelationship(const std::string& relationship_json, const std::string& name) {
    auto json = dd::Json::parse(relationship_json);
    if (!json) return error_json(json.error().message);
    auto relationship = dd::relationship_from_json(*json);
    if (!relationship) return error_json(relationship.error().message);
    std::string before = s_.source(), id;
    auto r = s_.update_relationship(*relationship, name.empty() ? std::nullopt : std::optional(name), &id);
    if (!r) return error_json(r.error().message);
    dd::Json update = *r ? dd::dsl::to_json(**r, before) : dd::Json();
    return dd::Json(dd::Json::Object{{"id", id}, {"update", update}}).dump();
  }

 private:
  static std::string update(const std::string& before, const dd::Result<dd::dsl::SourceUpdate>& r) {
    if (!r) return error_json(r.error().message);
    return dd::dsl::to_json(*r, before).dump();
  }

  static std::string problems_json(const std::vector<dd::editor::FieldProblem>& problems) {
    dd::Json::Object out;
    for (const auto& p : problems) out.emplace_back(p.field, dd::Json(p.message));
    return dd::Json(std::move(out)).dump();
  }

  [[nodiscard]] dd::Json state_json() const {
    dd::Json views = dd::Json::array();
    for (const auto& v : s_.model().views)
      views.push(dd::Json(dd::Json::Object{{"id", v.id}, {"algorithm", v.layout.algorithm}}));
    return dd::Json(dd::Json::Object{{"model", dd::to_json(s_.model())},
                                     {"diagnostics", dd::dsl::diagnostics_json(s_.parsed().diagnostics, s_.source())},
                                     {"views", views}});
  }

  dd::editor::Session s_;
  emscripten::val listener_;
};

/// The language server (dd::lsp::Server): `send(messageJson)` hands it one JSON-RPC message;
/// replies and notifications go to `listener(messageJson)`. Libraries resolve on disk when the
/// host gave a file system, otherwise from the added and built-in ones.
class LanguageServer {
 public:
  LanguageServer(emscripten::val listener, const std::string& fallback_dir)
      : listener_(std::move(listener)),
        server_([this](const dd::Json& message) { listener_(message.dump()); },
                disk() ? dd::lsp::disk_resolver(disk(), {added_source()}, fallback_dir)
                       : dd::lsp::memory_resolver({added_source()})) {}

  /// `{}`, or `{ error }` when the message is not JSON.
  std::string send(const std::string& message_json) {
    auto message = dd::Json::parse(message_json);
    if (!message) return error_json(message.error().message);
    server_.handle(*message);
    return "{}";
  }

  /// The exit code once `exit` arrived, else -1.
  [[nodiscard]] int exitCode() const { return server_.exit_code().value_or(-1); }

 private:
  emscripten::val listener_;
  dd::lsp::Server server_;
};

}  // namespace

/// `readStencilSets(text, file)`: draw.io's stencil sources (a set's .xml or stencils.min.js)
/// drawn as SVG: `{ sets: [{ name, file, shapes: [{ style, id, label, width, height, body }],
/// skipped: [{ name, reason }] }], colors: { fill, stroke, glyph } }`.
std::string read_stencil_sets(const std::string& text, const std::string& file) {
  dd::Json::Array sets;
  for (const auto& set : dd::io::read_stencil_sets(text, file)) {
    dd::Json::Array shapes, skipped;
    for (const auto& s : set.shapes)
      shapes.emplace_back(dd::Json::Object{{"style", dd::Json(s.style)},
                                           {"id", dd::Json(s.id)},
                                           {"label", dd::Json(s.label)},
                                           {"width", dd::Json(s.width)},
                                           {"height", dd::Json(s.height)},
                                           {"body", dd::Json(s.body)}});
    for (const auto& s : set.skipped)
      skipped.emplace_back(dd::Json::Object{{"name", dd::Json(s.name)}, {"reason", dd::Json(s.reason)}});
    sets.emplace_back(dd::Json::Object{{"name", dd::Json(set.name)},
                                       {"file", dd::Json(set.file)},
                                       {"shapes", dd::Json(std::move(shapes))},
                                       {"skipped", dd::Json(std::move(skipped))}});
  }
  return dd::Json(dd::Json::Object{{"sets", dd::Json(std::move(sets))},
                                   {"colors", dd::Json(dd::Json::Object{
                                                  {"fill", dd::Json(std::string(dd::io::kStencilFill))},
                                                  {"stroke", dd::Json(std::string(dd::io::kStencilStroke))},
                                                  {"glyph", dd::Json(std::string(dd::io::kGlyphFill))}})}})
      .dump();
}

EMSCRIPTEN_BINDINGS(diadraw) {
  emscripten::function("version", &version);
  emscripten::function("parse", &parse);
  emscripten::function("print", &print);
  emscripten::function("updateSource", &update_source);
  emscripten::function("renderView", &render_view);
  emscripten::function("addLibrary", &add_library);
  emscripten::function("libraryList", &library_list);
  emscripten::function("libraryPalette", &library_palette);
  emscripten::function("expandFragment", &expand_fragment);
  emscripten::function("buildLibrary", &build_library);
  emscripten::function("foldRanges", &fold_ranges);
  emscripten::function("highlight", &highlight);
  emscripten::function("formatSource", &format_source);
  emscripten::function("importDiagram", &import_diagram);
  emscripten::function("loadDrawioIndex", &load_drawio_index);
  emscripten::function("readStencilSets", &read_stencil_sets);
  emscripten::function("exportDrawio", &export_pages<dd::io::export_drawio>);
  emscripten::function("exportBpmn", &export_pages<dd::io::export_bpmn>);
  emscripten::function("bpmnFixedPages", &bpmn_fixed_pages);


  emscripten::function("setFileSystem", &set_file_system);

  emscripten::class_<LanguageServer>("LanguageServer")
      .constructor<emscripten::val, std::string>()
      .function("send", &LanguageServer::send)
      .function("exitCode", &LanguageServer::exitCode);

  emscripten::class_<Session>("Session")
      .constructor<std::string, std::string, emscripten::val>()
      .function("source", &Session::source)
      .function("setSource", &Session::setSource)
      .function("state", &Session::state)
      .function("render", &Session::render)
      .function("apply", &Session::apply)
      .function("drop", &Session::drop)
      .function("dropHint", &Session::dropHint)
      .function("resize", &Session::resize)
      .function("reroute", &Session::reroute)
      .function("fit", &Session::fit)
      .function("rotate", &Session::rotate)
      .function("move", &Session::move)
      .function("exportSvg", &Session::exportSvg)
      .function("connect", &Session::connect)
      .function("reconnect", &Session::reconnect)
      .function("create", &Session::create)
      .function("remove", &Session::remove)
      .function("updateRelationship", &Session::updateRelationship)
      .function("renameElement", &Session::renameElement)
      .function("elementProblems", &Session::elementProblems)
      .function("relationshipProblems", &Session::relationshipProblems)
      .function("elementAt", &Session::elementAt)
      .function("freshViewId", &Session::freshViewId)
      .function("addView", &Session::addView)
      .function("removeView", &Session::removeView)
      .function("renameView", &Session::renameView)
      .function("refreshLibraries", &Session::refreshLibraries);

}
