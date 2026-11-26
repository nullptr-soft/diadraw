// Node-API bindings: the native core behind `@diadraw/node`.
//
// The functions here are the raw layer that src/index.ts wraps. Structured values cross as JSON
// strings in the shapes of the TypeScript types (`@diadraw/wasm`'s types.ts), as in the WebAssembly
// bindings, so one set of types describes both; the wrapper parses and stringifies them. Failures
// are thrown as JavaScript errors.
//
// Documents resolve their libraries like the `diadraw` CLI does (host/diagram.hpp): the lockfile
// next to `file`, the library cache, the built-in libraries and draw.io's stencil sets, offline.

#include <napi.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/base/result.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/json.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/io/builder.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/model/views.hpp"
#include "host/diagram.hpp"

namespace {

// The host layer keeps process-wide state (the library cache and draw.io's stencil index, loaded on
// first use), and so does the PNG backend's font cache; worker threads load this addon into the
// same process, so calls that resolve libraries, draw or import take this lock.
std::mutex& host_mutex() {
  static std::mutex m;
  return m;
}

// ── Arguments ───────────────────────────────────────────────────────────────────────────────────

std::string string_arg(const Napi::CallbackInfo& info, std::size_t i, const char* name) {
  if (info.Length() <= i || !info[i].IsString())
    throw Napi::TypeError::New(info.Env(), std::string(name) + " must be a string");
  return info[i].As<Napi::String>().Utf8Value();
}

double number_arg(const Napi::CallbackInfo& info, std::size_t i, const char* name) {
  if (info.Length() <= i || !info[i].IsNumber())
    throw Napi::TypeError::New(info.Env(), std::string(name) + " must be a number");
  return info[i].As<Napi::Number>().DoubleValue();
}

/// A string, or the bytes of a Buffer or Uint8Array (draw.io PNGs carry the diagram in a chunk).
std::string text_or_bytes_arg(const Napi::CallbackInfo& info, std::size_t i, const char* name) {
  if (info.Length() > i) {
    if (info[i].IsString()) return info[i].As<Napi::String>().Utf8Value();
    if (info[i].IsTypedArray()) {
      auto array = info[i].As<Napi::TypedArray>();
      if (array.TypedArrayType() == napi_uint8_array) {
        auto bytes = array.As<Napi::Uint8Array>();
        return std::string(reinterpret_cast<const char*>(bytes.Data()), bytes.ByteLength());
      }
    }
  }
  throw Napi::TypeError::New(info.Env(), std::string(name) + " must be a string or a Uint8Array");
}

template <class T>
T unwrap(Napi::Env env, dd::Result<T> r) {
  if (!r) throw Napi::Error::New(env, r.error().message);
  return std::move(*r);
}

dd::Model model_arg(const Napi::CallbackInfo& info, std::size_t i) {
  auto json = unwrap(info.Env(), dd::Json::parse(string_arg(info, i, "model")));
  return unwrap(info.Env(), dd::model_from_json(json));
}

/// Runs `body`, turning a C++ exception from the core (malformed input some importers report by
/// throwing, or running out of memory) into a JavaScript error instead of ending the process.
template <class F>
Napi::Value guarded(const Napi::CallbackInfo& info, F&& body) {
  try {
    return std::forward<F>(body)();
  } catch (const Napi::Error&) {
    throw;
  } catch (const std::exception& e) {
    throw Napi::Error::New(info.Env(), e.what());
  } catch (...) {
    throw Napi::Error::New(info.Env(), "internal error in the DiaDraw core");
  }
}

// ── Documents ───────────────────────────────────────────────────────────────────────────────────

/// A document to render or export: DSL source (`isModel` false) or model JSON, with its libraries
/// resolved for `file`, and the default view added when it declares none.
struct Document {
  dd::Model model;
  dd::LibraryArt art;
  /// What the libraries could not supply, as `diadraw render` warns about it.
  std::vector<std::string> warnings;
};

/// "line:column: error: message" lines for the errors in `diagnostics`.
std::string error_lines(const std::vector<dd::dsl::Diagnostic>& diagnostics) {
  std::string out;
  for (const auto& d : diagnostics) {
    if (d.severity != dd::dsl::Severity::Error) continue;
    if (!out.empty()) out += '\n';
    out += dd::host::format_diagnostic(d);
  }
  return out;
}

/// Callers hold `host_mutex()`.
Document load_document(Napi::Env env, const std::string& input, bool is_model, const std::string& file) {
  if (is_model) {
    auto json = unwrap(env, dd::Json::parse(input));
    dd::Model model = dd::with_default_view(unwrap(env, dd::model_from_json(json)));
    auto libraries = dd::host::resolve_for_model(model, file);
    auto warnings = dd::host::library_warnings(model, libraries);
    return {std::move(model), libraries.set.renderer(), std::move(warnings)};
  }
  dd::host::Analysis analysis = dd::host::analyze(input, file);
  if (analysis.errors > 0) throw Napi::Error::New(env, error_lines(analysis.parse.diagnostics));
  return {dd::with_default_view(std::move(analysis.parse.model)), analysis.libraries.set.renderer(),
          std::move(analysis.library_warnings)};
}

// ── Exports ─────────────────────────────────────────────────────────────────────────────────────

Napi::Value version(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), dd_version()); }

/// `parse(source)`: `{ model, diagnostics, sourceMap, operations }`, offsets in UTF-16.
Napi::Value parse(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    std::string source = string_arg(info, 0, "source");
    return Napi::String::New(info.Env(), dd::dsl::to_json(dd::dsl::parse(source), source).dump());
  });
}

/// `print(modelJson)`: canonical source.
Napi::Value print(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    return Napi::String::New(info.Env(), dd::dsl::print(model_arg(info, 0)));
  });
}

/// `format(source)`: the source printed canonically, as `diadraw format`; throws on parse errors.
Napi::Value format(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    auto parsed = dd::dsl::parse(string_arg(info, 0, "source"));
    std::string errors = error_lines(parsed.diagnostics);
    if (!errors.empty()) throw Napi::Error::New(info.Env(), errors);
    return Napi::String::New(info.Env(), dd::dsl::print(parsed.model));
  });
}

/// `highlight(text)`: `[from, to, tag]` triples, UTF-16 offsets, CodeMirror tag names.
Napi::Value highlight(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    return Napi::String::New(info.Env(), dd::dsl::highlight_json(string_arg(info, 0, "text")).dump());
  });
}

/// `updateSource(source, operationJson)`: `{ source, edits, model, minimal, diagnostics }`.
Napi::Value update_source(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    Napi::Env env = info.Env();
    std::string source = string_arg(info, 0, "source");
    auto json = unwrap(env, dd::Json::parse(string_arg(info, 1, "operation")));
    auto op = unwrap(env, dd::operation_from_json(json));
    auto update = unwrap(env, dd::dsl::update_source(source, op));
    return Napi::String::New(env, dd::dsl::to_json(update, source).dump());
  });
}

/// `check(source, file)`: `{ diagnostics, libraryWarnings }`, as `diadraw check` reports them.
Napi::Value check(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    std::string source = string_arg(info, 0, "source");
    std::string file = string_arg(info, 1, "file");
    std::lock_guard lock(host_mutex());
    dd::host::Analysis analysis = dd::host::analyze(source, file);
    dd::Json warnings = dd::Json::array();
    for (auto& w : analysis.library_warnings) warnings.push(dd::Json(std::move(w)));
    dd::Json out(dd::Json::Object{{"diagnostics", dd::dsl::diagnostics_json(analysis.parse.diagnostics, source)},
                                  {"libraryWarnings", std::move(warnings)}});
    return Napi::String::New(info.Env(), out.dump());
  });
}

/// `exportDocument(input, isModel, file, format, view, scale, background)`: `[output, warnings]`,
/// the document written as `diadraw render` writes it (host::export_diagram) with what it warns
/// about (libraries it could not supply, layout hints it could not honour). The output of `svg`,
/// `drawio` and `bpmn` is a string, of `png` and `pdf` a Buffer. An empty `view` is the first
/// view for SVG, PNG and PDF and every view for draw.io and BPMN; an empty `background` is
/// transparent.
Napi::Value export_document(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    using Format = dd::host::ExportFormat;
    Napi::Env env = info.Env();
    std::string input = string_arg(info, 0, "input");
    if (info.Length() <= 1 || !info[1].IsBoolean()) throw Napi::TypeError::New(env, "isModel must be a boolean");
    bool is_model = info[1].As<Napi::Boolean>().Value();
    std::string file = string_arg(info, 2, "file");
    std::string format_name = string_arg(info, 3, "format");
    std::string view = string_arg(info, 4, "view");
    double scale = number_arg(info, 5, "scale");
    std::string background = string_arg(info, 6, "background");
    std::optional<Format> format = dd::host::export_format_named(format_name);
    if (!format) throw Napi::TypeError::New(env, "format must be svg, png, pdf, drawio or bpmn");
    dd::host::ExportOptions options;
    if (!view.empty()) options.view = view;
    options.scale = scale;
    if (!background.empty()) options.background = background;
    // Before loading, so a bad scale or background is reported before any work.
    if (auto invalid = dd::host::check_export_options(*format, options))
      throw Napi::RangeError::New(env, invalid->message);

    std::variant<dd::host::Exported, dd::host::ExportError> exported;
    std::vector<std::string> warnings;
    {
      // The whole export, rasterising included: lunasvg's font cache is process-wide and plutovg
      // locks it only where <threads.h> exists (not on macOS).
      std::lock_guard lock(host_mutex());
      Document doc = load_document(env, input, is_model, file);
      exported = dd::host::export_diagram(doc.model, *format, options, doc.art);
      warnings = std::move(doc.warnings);
    }
    if (const auto* failure = std::get_if<dd::host::ExportError>(&exported)) {
      if (failure->kind == dd::host::ExportError::Kind::InvalidArgument)
        throw Napi::RangeError::New(env, failure->message);
      throw Napi::Error::New(env, failure->message);
    }
    const auto& done = std::get<dd::host::Exported>(exported);
    for (const auto& hint : done.ignored_hints) warnings.push_back(dd::host::hint_warning(hint));
    Napi::Array out = Napi::Array::New(env, 2);
    if (*format == Format::Png || *format == Format::Pdf)
      out.Set(0u, Napi::Buffer<char>::Copy(env, done.bytes.data(), done.bytes.size()));
    else
      out.Set(0u, Napi::String::New(env, done.bytes));
    Napi::Array list = Napi::Array::New(env, warnings.size());
    for (std::size_t i = 0; i < warnings.size(); ++i)
      list.Set(static_cast<std::uint32_t>(i), Napi::String::New(env, warnings[i]));
    out.Set(1u, list);
    return out;
  });
}

/// `importDiagram(data, fileName, format, keepGeometry)`: `{ model, report }`. An empty `format`
/// is detected from the file name and the content. Includes resolve on disk next to `fileName`.
Napi::Value import_diagram(const Napi::CallbackInfo& info) {
  return guarded(info, [&]() -> Napi::Value {
    Napi::Env env = info.Env();
    std::string data = text_or_bytes_arg(info, 0, "data");
    std::string file = string_arg(info, 1, "fileName");
    std::string format_name = string_arg(info, 2, "format");
    if (info.Length() <= 3 || !info[3].IsBoolean()) throw Napi::TypeError::New(env, "keepGeometry must be a boolean");
    bool keep_geometry = info[3].As<Napi::Boolean>().Value();
    std::optional<dd::io::Format> format =
        format_name.empty() ? dd::io::detect_format(file, data) : dd::host::format_named(format_name);
    if (!format)
      throw Napi::Error::New(env, format_name.empty() ? "cannot tell the format of " + file
                                                      : "unknown format \"" + format_name + "\"");
    std::lock_guard lock(host_mutex());
    auto result = unwrap(env, dd::host::import_text(data, *format, file, keep_geometry));
    return Napi::String::New(env, dd::io::to_json(result).dump());
  });
}

Napi::Object init(Napi::Env env, Napi::Object exports) {
  exports.Set("version", Napi::Function::New(env, version, "version"));
  exports.Set("parse", Napi::Function::New(env, parse, "parse"));
  exports.Set("print", Napi::Function::New(env, print, "print"));
  exports.Set("format", Napi::Function::New(env, format, "format"));
  exports.Set("highlight", Napi::Function::New(env, highlight, "highlight"));
  exports.Set("updateSource", Napi::Function::New(env, update_source, "updateSource"));
  exports.Set("check", Napi::Function::New(env, check, "check"));
  exports.Set("exportDocument", Napi::Function::New(env, export_document, "exportDocument"));
  exports.Set("importDiagram", Napi::Function::New(env, import_diagram, "importDiagram"));
  return exports;
}

}  // namespace

NODE_API_MODULE(diadraw, init)
