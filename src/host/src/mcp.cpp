#include "host/mcp.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/export/export.hpp"
#include "diadraw/io/codec.hpp"
#include "diadraw/library/builtin.hpp"
#include "diadraw/model/views.hpp"
#include "diadraw/render/svg.hpp"
#include "host/diagram.hpp"
#include "host/examples.hpp"
#include "host/field_editors.hpp"
#include "host/files.hpp"
#include "host/libraries.hpp"

namespace dd::host {

namespace fs = std::filesystem;

namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

/// The longest side of a PNG preview, in pixels: big enough to read every label, small enough
/// for an agent's context.
constexpr double kPreviewMaxSide = 2000;

/// At most this many lines of shapes, kinds or themes from one `reference` call.
constexpr std::size_t kReferenceLimit = 300;

/// Sent with `initialize`: how to use the tools, and enough of the language to start.
constexpr const char* kInstructions = R"(DiaDraw draws diagrams from text. A document (.draw) has a `model` block that defines elements and relationships once, and `view` blocks that pick, arrange and constrain them.

model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}
view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web
}

Work in a loop: write the document, call `check` until it reports no errors, call `render` and look at the image, then fix what looks wrong (direction, spacing, `pin` hints, labels) and render again. Prefer `path` to a .draw file in the user's project so the diagram stays in version control; use `source` for scratch work.

`reference` lists element kinds, shapes, themes, layouts and libraries (`use "c4@^1"`; pass `library` to see a library's shapes). `examples` returns complete sample documents (C4, flowcharts, UML class/sequence/activity/state/use case/component, BPMN): read one close to what you need before writing a new kind of diagram. `import` converts Mermaid, PlantUML, Structurizr, draw.io and BPMN. The full language reference is at https://github.com/mykulyak/diadraw/blob/main/docs/dsl.md.)";

// ── JSON helpers ─────────────────────────────────────────────────────────────────────────────

Json response(const Json& id, Json result) {
  Json out = Json::object();
  out.set("jsonrpc", "2.0");
  out.set("id", id);
  out.set("result", std::move(result));
  return out;
}

Json error_response(const Json& id, int code, std::string message) {
  Json error = Json::object();
  error.set("code", code);
  error.set("message", std::move(message));
  Json out = Json::object();
  out.set("jsonrpc", "2.0");
  out.set("id", id);
  out.set("error", std::move(error));
  return out;
}

const std::string* string_arg(const Json& args, std::string_view key) {
  const Json* v = args.get(key);
  return v && v->is_string() ? &v->as_string() : nullptr;
}

/// A JSON Schema property.
Json prop(const char* type, const char* description) {
  Json p = Json::object();
  p.set("type", type);
  p.set("description", description);
  return p;
}

Json schema(Json properties, std::vector<std::string> required = {}) {
  Json s = Json::object();
  s.set("type", "object");
  s.set("properties", std::move(properties));
  if (!required.empty()) {
    Json r = Json::array();
    for (auto& name : required) r.push(std::move(name));
    s.set("required", std::move(r));
  }
  s.set("additionalProperties", false);
  return s;
}

Json tool(const char* name, const char* title, const char* description, Json input_schema, bool read_only) {
  Json t = Json::object();
  t.set("name", name);
  t.set("title", title);
  t.set("description", description);
  t.set("inputSchema", std::move(input_schema));
  Json annotations = Json::object();
  annotations.set("readOnlyHint", read_only);
  annotations.set("openWorldHint", false);
  t.set("annotations", std::move(annotations));
  return t;
}

/// The `source` and `path` properties every tool that reads a document takes.
Json document_props(const char* what) {
  Json p = Json::object();
  p.set("source", prop("string", (std::string("The ") + what + " text. Give this or `path`.").c_str()));
  p.set("path", prop("string", (std::string("A file holding the ") + what +
                                ", absolute or relative to the server's working directory. Give this or `source`.")
                                   .c_str()));
  return p;
}

Json tools_list() {
  Json tools = Json::array();
  {
    Json p = document_props("DiaDraw document (.draw)");
    tools.push(tool("check", "Check a diagram",
                    "Parses a DiaDraw document and reports its errors and warnings as `line:column: severity: "
                    "message`, followed by its views. Call it after every edit, before rendering.",
                    schema(std::move(p)), true));
  }
  {
    Json p = document_props("DiaDraw document (.draw)");
    p.set("view", prop("string", "The view to render; the first view when left out."));
    Json format = prop("string",
                       "What comes back: `png` (default) an image to look at, `svg` the SVG text, `none` only a "
                       "summary (with `output`).");
    Json formats = Json::array();
    for (const char* f : {"png", "svg", "none"}) formats.push(f);
    format.set("enum", std::move(formats));
    p.set("format", std::move(format));
    p.set("output", prop("string",
                         "Also write the rendering to this file; its extension picks SVG, PNG or PDF (.svg, .png, "
                         ".pdf). Relative to the server's working directory."));
    p.set("scale", prop("number",
                        "Pixel density of PNG output, 0.1 to 4. Defaults to 1.5 for the returned image (whose longest "
                        "side is capped at 2000 px) and 2 for a PNG file."));
    tools.push(tool("render", "Render a diagram",
                    "Lays out and draws one view of a DiaDraw document. By default returns a PNG image so you can "
                    "see the diagram: check that it reads well and fix the document if not.",
                    schema(std::move(p)), false));
  }
  {
    Json p = document_props("DiaDraw document (.draw)");
    p.set("write", prop("boolean", "With `path`, rewrite the file in canonical form."));
    tools.push(tool("format", "Format a diagram",
                    "Returns a DiaDraw document in canonical form (DiaDraw's own layout of the text). Refuses a "
                    "document with errors.",
                    schema(std::move(p)), false));
  }
  {
    Json p = document_props("diagram in another format");
    Json from = prop("string", "The source format; guessed from the file name and content when left out.");
    Json formats = Json::array();
    for (const char* f : {"mermaid", "plantuml", "structurizr", "drawio", "bpmn"}) formats.push(f);
    from.set("enum", std::move(formats));
    p.set("from", std::move(from));
    p.set("output", prop("string", "Also write the DiaDraw document to this file (.draw)."));
    tools.push(tool("import", "Import a diagram",
                    "Converts a Mermaid, PlantUML, Structurizr, draw.io or BPMN diagram into a DiaDraw document, "
                    "and reports what could not be carried over.",
                    schema(std::move(p)), false));
  }
  {
    Json p = Json::object();
    p.set("library", prop("string",
                          "A library's name (such as `c4`, `bpmn` or `plantuml-aws`): list its kinds, shapes and "
                          "themes, and the `use` line that brings it in."));
    p.set("search", prop("string", "Only entries containing this text (case-insensitive)."));
    tools.push(tool("reference", "DiaDraw reference",
                    "Lists what a document can use: built-in element kinds, base shapes, themes, layout "
                    "algorithms and directions, and the built-in libraries; or, with `library`, one library's "
                    "kinds, shapes and themes.",
                    schema(std::move(p)), true));
  }
  {
    Json p = Json::object();
    p.set("name", prop("string", "The example to return; lists the examples when left out."));
    tools.push(tool("examples", "DiaDraw examples",
                    "Complete sample documents: C4 models, flowcharts, UML (class, sequence, activity, state, use "
                    "case, component) and BPMN. Read one close to what you need before writing a new kind of "
                    "diagram.",
                    schema(std::move(p)), true));
  }
  Json out = Json::object();
  out.set("tools", std::move(tools));
  return out;
}

// ── tool results ─────────────────────────────────────────────────────────────────────────────

Json text_content(std::string text) {
  Json c = Json::object();
  c.set("type", "text");
  c.set("text", std::move(text));
  return c;
}

Json image_content(std::string_view png) {
  Json c = Json::object();
  c.set("type", "image");
  c.set("data", io::base64_encode(png));
  c.set("mimeType", "image/png");
  return c;
}

Json tool_result(Json content, bool error = false) {
  Json out = Json::object();
  out.set("content", std::move(content));
  if (error) out.set("isError", true);
  return out;
}

Json text_result(std::string text, bool error = false) {
  Json content = Json::array();
  content.push(text_content(std::move(text)));
  return tool_result(std::move(content), error);
}

/// Diagnostics and library warnings, one per line.
std::string report(const Analysis& analysis) {
  std::string out;
  for (const auto& d : analysis.parse.diagnostics) out += format_diagnostic(d) + "\n";
  for (const auto& w : analysis.library_warnings) out += "warning: " + w + "\n";
  return out;
}

/// A document a tool works on: its text and the file it is (or would be) in.
struct Document {
  std::string source;
  std::string file;
  bool on_disk = false;
};

}  // namespace

// ── the server ───────────────────────────────────────────────────────────────────────────────

namespace {

class Tools {
 public:
  explicit Tools(const McpOptions& options) : options_(options) {}

 private:
  /// `path` (UTF-8) made absolute against the working directory.
  std::string resolve(const std::string& path) const {
    fs::path p = utf8_path(path);
    if (p.is_absolute() || options_.working_dir.empty()) return path_utf8(p.lexically_normal());
    return path_utf8((utf8_path(options_.working_dir) / p).lexically_normal());
  }

  /// The document of `source` or `path`; an error message when there is neither or it cannot
  /// be read.
  Result<Document> load(const Json& args, const char* untitled) const {
    const std::string* source = string_arg(args, "source");
    const std::string* path = string_arg(args, "path");
    if (source && path) return fail("give either `source` or `path`, not both");
    if (path) {
      std::string file = resolve(*path);
      auto text = read_file(file);
      if (!text) return fail("cannot read ", file);
      return Document{std::move(*text), std::move(file), true};
    }
    if (source) return Document{*source, resolve(untitled), false};
    return fail("give the document as `source` (its text) or `path` (a file)");
  }

 public:
  // The tools, one per entry of kToolTable.
  Json check(const Json& args) const {
    auto doc = load(args, "untitled.draw");
    if (!doc) return text_result(doc.error().message, true);
    Analysis analysis = analyze(doc->source, doc->file);
    std::string text = report(analysis);
    if (analysis.errors > 0) {
      text += std::to_string(analysis.errors) + (analysis.errors == 1 ? " error" : " errors") + "\n";
      return text_result(std::move(text), true);
    }
    Model model = with_default_view(analysis.parse.model);
    text += "ok; views: ";
    bool first = true;
    for (const auto& id : model.views.keys()) {
      text += (first ? "" : ", ") + id;
      first = false;
    }
    if (analysis.parse.model.views.empty()) text += " (no view declared; the default view shows every element)";
    return text_result(text + "\n");
  }

  Json render(const Json& args) const {
    auto doc = load(args, "untitled.draw");
    if (!doc) return text_result(doc.error().message, true);
    const std::string* format = string_arg(args, "format");
    std::string_view kind = format ? std::string_view(*format) : "png";
    if (kind != "png" && kind != "svg" && kind != "none")
      return text_result("`format` is png, svg or none", true);
    const std::string* output = string_arg(args, "output");
    std::optional<std::string> output_file;
    if (output) {
      output_file = resolve(*output);
      if (!ends_with_ci(*output_file, ".svg") && !ends_with_ci(*output_file, ".png") &&
          !ends_with_ci(*output_file, ".pdf"))
        return text_result("`output` must end in .svg, .png or .pdf", true);
    }
    std::optional<double> scale;
    if (const Json* s = args.get("scale")) {
      if (!s->is_number() || !std::isfinite(s->as_number()) || s->as_number() < 0.1 || s->as_number() > 4)
        return text_result("`scale` is a number from 0.1 to 4", true);
      scale = s->as_number();
    }

    Analysis analysis = analyze(doc->source, doc->file);
    if (analysis.errors > 0) return text_result(report(analysis) + "the document has errors; nothing rendered\n", true);
    Model model = with_default_view(analysis.parse.model);
    const std::string* view_arg = string_arg(args, "view");
    std::string view_id = view_arg ? *view_arg : model.views.keys().front();
    LibraryArt art = analysis.libraries.set.renderer();
    auto rendered = render_view(model, view_id, art);
    if (!rendered) return text_result("render: " + rendered.error().message, true);

    std::string summary = report(analysis);
    for (const auto& hint : rendered->ignored_hints) summary += "warning: " + hint_warning(hint) + "\n";
    auto size = exporting::svg_size(rendered->svg);
    summary += "view \"" + view_id + "\"";
    if (size) summary += ", " + js_number_string(size->width) + " × " + js_number_string(size->height) + " px";
    std::string others;
    for (const auto& id : model.views.keys())
      if (id != view_id) others += (others.empty() ? "" : ", ") + id;
    if (!others.empty()) summary += "; other views: " + others;
    summary += "\n";

    std::unique_ptr<exporting::Rasterizer> rasterizer;
    auto png = [&](double s) -> Result<std::string> {
      if (!rasterizer) rasterizer = exporting::make_rasterizer();
      if (!rasterizer) return fail("this build has no PNG backend");
      exporting::PngOptions options;
      options.scale = s;
      return rasterizer->png(rendered->svg, options);
    };

    if (output_file) {
      Result<std::string> bytes = rendered->svg;
      if (ends_with_ci(*output_file, ".png")) {
        bytes = png(scale.value_or(2));
      } else if (ends_with_ci(*output_file, ".pdf")) {
        auto writer = exporting::make_pdf_writer();
        bytes = writer ? writer->pdf(rendered->svg) : Result<std::string>(fail("this build has no PDF backend"));
      }
      if (!bytes) return text_result(summary + "cannot write " + *output_file + ": " + bytes.error().message, true);
      if (!write_file(*output_file, *bytes)) return text_result(summary + "cannot write " + *output_file, true);
      summary += "wrote " + *output_file + "\n";
    }

    Json content = Json::array();
    if (kind == "svg") {
      content.push(text_content(summary));
      content.push(text_content(rendered->svg));
    } else if (kind == "png") {
      double s = scale.value_or(1.5);
      if (size && size->width > 0 && size->height > 0)
        s = std::min(s, kPreviewMaxSide / std::max(size->width, size->height));
      auto bytes = png(s);
      if (!bytes) {
        // Without a PNG backend the SVG is the next best thing to look at.
        content.push(text_content(summary + "no image: " + bytes.error().message + "; the SVG follows\n"));
        content.push(text_content(rendered->svg));
      } else {
        content.push(text_content(summary));
        content.push(image_content(*bytes));
      }
    } else {
      content.push(text_content(summary));
    }
    return tool_result(std::move(content));
  }

  Json format(const Json& args) const {
    auto doc = load(args, "untitled.draw");
    if (!doc) return text_result(doc.error().message, true);
    const Json* write = args.get("write");
    bool rewrite = write && write->is_bool() && write->as_bool();
    if (rewrite && !doc->on_disk) return text_result("`write` needs `path`", true);
    dsl::ParseResult parsed = dsl::parse(doc->source);
    if (std::ranges::any_of(parsed.diagnostics, [](const auto& d) { return d.severity == dsl::Severity::Error; })) {
      std::string diagnostics;
      for (const auto& d : parsed.diagnostics) diagnostics += format_diagnostic(d) + "\n";
      return text_result(diagnostics + "the document has errors; not formatted\n", true);
    }
    std::string text = dsl::print(parsed.model);
    if (rewrite) {
      if (!write_file(doc->file, text)) return text_result("cannot write " + doc->file, true);
      return text_result("wrote " + doc->file + "\n" + text);
    }
    return text_result(std::move(text));
  }

  Json import(const Json& args) const {
    auto doc = load(args, "untitled");
    if (!doc) return text_result(doc.error().message, true);
    std::optional<io::Format> format;
    if (const std::string* from = string_arg(args, "from")) {
      format = format_named(*from);
      if (!format) return text_result("`from` is mermaid, plantuml, structurizr, drawio or bpmn", true);
    } else {
      format = io::detect_format(doc->on_disk ? doc->file : "", doc->source);
      if (!format) return text_result("cannot tell the format; pass `from`", true);
    }
    auto result = import_text(doc->source, *format, doc->file, true);
    if (!result) return text_result("import: " + result.error().message, true);
    std::string notes;
    for (const auto& d : result->report.dropped)
      notes += "dropped" + (d.line ? " (line " + std::to_string(*d.line) + ")" : std::string()) + ": " + d.message + "\n";
    for (const auto& note : result->report.notes) notes += "note: " + note + "\n";
    std::string text = dsl::print(result->model);
    if (const std::string* output = string_arg(args, "output")) {
      std::string file = resolve(*output);
      if (!write_file(file, text)) return text_result("cannot write " + file, true);
      notes += "wrote " + file + "\n";
    }
    Json content = Json::array();
    content.push(text_content(std::move(text)));
    if (!notes.empty()) content.push(text_content(std::move(notes)));
    return tool_result(std::move(content));
  }

  Json reference(const Json& args) const {
    const std::string* search = string_arg(args, "search");
    std::string needle = search ? js::to_lower(*search) : std::string();
    auto matches = [&](std::string_view a, std::string_view b = {}) {
      return needle.empty() || js::to_lower(std::string(a)).find(needle) != std::string::npos ||
             js::to_lower(std::string(b)).find(needle) != std::string::npos;
    };
    std::string out;
    std::size_t lines = 0, skipped = 0;
    auto line = [&](const std::string& text, std::string_view a, std::string_view b = {}) {
      if (!matches(a, b)) return;
      if (lines >= kReferenceLimit) {
        ++skipped;
        return;
      }
      out += text + "\n";
      ++lines;
    };
    auto words = [&](const char* heading, const std::vector<std::string>& items) {
      std::string list;
      for (const auto& item : items)
        if (matches(item)) list += (list.empty() ? "" : " ") + item;
      if (!list.empty()) out += std::string(heading) + ": " + list + "\n";
    };

    if (const std::string* name = string_arg(args, "library")) {
      // Resolved as `use "<name>"` would resolve it (a range is allowed), with the alias that gives.
      LibrarySpec spec = parse_library_spec(*name);
      LibraryUse use{spec.name, spec.range, default_library_alias(spec.name)};
      library::Resolution resolution = resolve_for_file({use}, resolve("untitled.draw"));
      if (!resolution.unresolved.empty() || resolution.set.libraries().empty()) {
        std::string message = "no library \"" + *name + "\"";
        if (!resolution.unresolved.empty()) message += ": " + resolution.unresolved.front().message;
        return text_result(message + "\ncall `reference` without `library` for the built-in libraries", true);
      }
      const auto& lib = resolution.set.libraries().front();
      const Json* version = lib.bundle->get("version");
      std::string v = version && version->is_string() ? version->as_string() : "";
      std::string major = v.substr(0, v.find('.'));
      out += "use \"" + spec.name + (major.empty() ? "" : "@^" + major) + "\"\n";
      if (const Json* d = lib.bundle->get("description"); d && d->is_string()) out += d->as_string() + "\n";
      out += "Refer to its kinds as `x = " + lib.use.alias + ".<kind> \"Name\"` and its shapes as `shape " +
             lib.use.alias + ".<shape>`.\n";
      std::vector<std::string> kinds, shapes;
      for (const auto& item : resolution.set.palette()) {
        std::string label = item.label.empty() || item.label == item.id ? "" : " (" + item.label + ")";
        std::string category = item.category.empty() ? "" : " [" + item.category + "]";
        if (item.type == "kind") line("kind " + item.ref + label, item.ref, item.label);
        else if (item.type == "shape" || item.type == "composite")
          line("shape " + item.ref + label + category, item.ref, item.label + " " + item.category);
        else if (item.type == "fragment") line("fragment " + item.ref + label, item.ref, item.label);
      }
      for (const auto& theme : resolution.set.theme_refs()) line("theme " + theme, theme);
    } else {
      std::vector<std::string> layouts(std::begin(kLayoutAlgorithms), std::end(kLayoutAlgorithms));
      words("Element kinds", builtin_kinds());
      words("Base shapes (`shape …`)", base_shapes());
      words("Themes (`theme …`)", theme_names());
      words("Layouts (`layout …`)", layouts);
      words("Directions (`layout layered direction …`)", {"right", "left", "down", "up"});
      out += "Built-in libraries (`use \"name@^1\"`; call `reference` with `library` for their shapes):\n";
      for (const auto& b : library::builtin_bundles().as_array()) {
        const Json* n = b.get("name");
        if (!n || !n->is_string()) continue;
        const Json* d = b.get("description");
        std::string description = d && d->is_string() ? d->as_string() : "";
        line("  " + n->as_string() + (description.empty() ? "" : ": " + description), n->as_string(), description);
      }
      if (drawio_index())
        out += "draw.io's stencil sets are libraries too: `use \"drawio-<set>\"` (for example drawio-aws4).\n";
    }
    if (skipped > 0)
      out += "… and " + std::to_string(skipped) + " more; narrow the list with `search`\n";
    if (out.empty()) out = "nothing matches \"" + (search ? *search : std::string()) + "\"\n";
    return text_result(std::move(out));
  }

  Json examples(const Json& args) const {
    if (const std::string* name = string_arg(args, "name")) {
      std::string_view wanted = *name;
      if (wanted.size() > 5 && wanted.substr(wanted.size() - 5) == ".draw") wanted.remove_suffix(5);
      if (const Example* example = find_example(wanted)) return text_result(std::string(example->source));
      return text_result("no example \"" + *name + "\"; call `examples` without `name` for the list", true);
    }
    std::string out = "Examples (pass one as `name`):\n";
    for (const auto& example : host::examples()) out += "  " + std::string(example.name) + "\n";
    return text_result(std::move(out));
  }

 private:
  const McpOptions& options_;
};

/// The tools by name, in the order tools/list gives them.
struct ToolEntry {
  std::string_view name;
  Json (Tools::*run)(const Json&) const;
};
constexpr ToolEntry kToolTable[] = {{"check", &Tools::check},         {"render", &Tools::render},
                                    {"format", &Tools::format},       {"import", &Tools::import},
                                    {"reference", &Tools::reference}, {"examples", &Tools::examples}};

const ToolEntry* find_tool(std::string_view name) {
  auto it = std::ranges::find(kToolTable, name, &ToolEntry::name);
  return it == std::end(kToolTable) ? nullptr : &*it;
}

}  // namespace

McpServer::McpServer(McpOptions options) : options_(std::move(options)) {}

std::string McpServer::serialize(const Json& message) { return to_valid_utf8(message.dump()); }

Json McpServer::parse_error(std::string_view detail) {
  return error_response(Json(), kParseError, "Parse error: " + std::string(detail));
}

std::optional<Json> McpServer::handle(const Json& message) const {
  if (!message.is_object()) return error_response(Json(), kInvalidRequest, "Invalid Request: not an object");
  const Json* method = message.get("method");
  const Json* id_value = message.get("id");
  if (!method || !method->is_string()) {
    // A response to something we never send, or garbage: answer only what carries an id.
    if (id_value && !message.get("result") && !message.get("error"))
      return error_response(*id_value, kInvalidRequest, "Invalid Request: no method");
    return std::nullopt;
  }
  if (!id_value) return std::nullopt;  // notifications: initialized, cancelled, …
  const Json& id = *id_value;
  const std::string& name = method->as_string();
  static const Json kNoParams = Json::object();
  const Json* params_value = message.get("params");
  const Json& params = params_value && params_value->is_object() ? *params_value : kNoParams;
  try {
    if (name == "initialize") return response(id, initialize(params));
    if (name == "ping") return response(id, Json::object());
    if (name == "tools/list") return response(id, tools_list());
    if (name == "tools/call") {
      const Json* tool_name = params.get("name");
      if (!tool_name || !tool_name->is_string() || !find_tool(tool_name->as_string()))
        return error_response(id, kInvalidParams,
                              "Unknown tool" + (tool_name && tool_name->is_string() ? ": " + tool_name->as_string()
                                                                                     : std::string()));
      return response(id, call_tool(params));
    }
    // Lists the client may ask for although the server declares none of them.
    if (name == "resources/list") return response(id, Json(Json::Object{{"resources", Json::array()}}));
    if (name == "resources/templates/list")
      return response(id, Json(Json::Object{{"resourceTemplates", Json::array()}}));
    if (name == "prompts/list") return response(id, Json(Json::Object{{"prompts", Json::array()}}));
    return error_response(id, kMethodNotFound, "Method not found: " + name);
  } catch (const std::exception& e) {
    return error_response(id, kInternalError, std::string("Internal error: ") + e.what());
  } catch (...) {
    return error_response(id, kInternalError, "Internal error");
  }
}

Json McpServer::initialize(const Json& params) const {
  std::string_view version = kProtocolVersions[0];
  if (const Json* requested = params.get("protocolVersion"); requested && requested->is_string())
    for (std::string_view v : kProtocolVersions)
      if (v == requested->as_string()) version = v;
  Json capabilities = Json::object();
  capabilities.set("tools", Json::object());
  Json info = Json::object();
  info.set("name", "diadraw");
  info.set("title", "DiaDraw");
  info.set("version", options_.version);
  Json out = Json::object();
  out.set("protocolVersion", version);
  out.set("capabilities", std::move(capabilities));
  out.set("serverInfo", std::move(info));
  out.set("instructions", kInstructions);
  return out;
}

Json McpServer::call_tool(const Json& params) const {
  static const Json kNoArgs = Json::object();
  const Json* args = params.get("arguments");
  const ToolEntry* entry = find_tool(params.get("name")->as_string());
  return (Tools(options_).*entry->run)(args && args->is_object() ? *args : kNoArgs);
}

}  // namespace dd::host
