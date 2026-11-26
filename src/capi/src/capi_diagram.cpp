// ABI 2 of the C ABI (diadraw/diadraw.h): the DSL, layout, rendering and import, the way the
// `diadraw` command line does them (host/diagram.hpp). Built into `diadraw_capi`, apart from ABI 1
// in `diadraw`, because it needs the host and export libraries, which themselves depend on
// `diadraw`.

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/json.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/layout/json.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/model/views.hpp"
#include "host/diagram.hpp"
#include "internal.hpp"

using dd::capi::copy_bytes;
using dd::capi::guarded;
using dd::capi::set_error;

namespace {

/// A parsed document with its libraries: for a file as the CLI resolves them, without one from
/// the built-in libraries and draw.io's stencil sets.
dd::host::Analysis analyze(const char* source, const char* file) {
  return file ? dd::host::analyze(source, file) : dd::host::analyze(source);
}

/// The error diagnostics, one `line:column: error: message` per line.
std::string error_lines(const std::vector<dd::dsl::Diagnostic>& diagnostics) {
  std::string out;
  for (const auto& d : diagnostics) {
    if (d.severity != dd::dsl::Severity::Error) continue;
    if (!out.empty()) out += '\n';
    out += dd::host::format_diagnostic(d);
  }
  return out;
}

/// Hands `text` to the caller, or reports that it could not be copied.
char* hand_over(std::string_view text, char** error) {
  char* out = copy_bytes(text);
  if (!out) set_error(error, "out of memory");
  return out;
}

}  // namespace

extern "C" {

char* dd_parse_json(const char* source) {
  if (!source) return nullptr;
  return guarded<char*>(nullptr, nullptr,
                        [&] { return copy_bytes(dd::dsl::to_json(dd::dsl::parse(source), source).dump()); });
}

char* dd_check_json(const char* source, const char* file) {
  if (!source) return nullptr;
  return guarded<char*>(nullptr, nullptr, [&] {
    dd::host::Analysis a = analyze(source, file);
    dd::Json diagnostics = dd::Json::array(), warnings = dd::Json::array();
    for (const auto& d : a.parse.diagnostics)
      diagnostics.push(dd::Json(dd::Json::Object{
          {"line", d.line},
          {"column", d.column},
          {"severity", dd::Json(d.severity == dd::dsl::Severity::Error ? "error" : "warning")},
          {"message", dd::Json(d.message)}}));
    for (const auto& w : a.library_warnings) warnings.push(dd::Json(w));
    return copy_bytes(dd::Json(dd::Json::Object{{"errors", a.errors},
                                                {"diagnostics", std::move(diagnostics)},
                                                {"warnings", std::move(warnings)}})
                          .dump());
  });
}

char* dd_format_source(const char* source, char** error) {
  if (!source) {
    set_error(error, "source is NULL");
    return nullptr;
  }
  return guarded<char*>(error, nullptr, [&]() -> char* {
    dd::dsl::ParseResult parsed = dd::dsl::parse(source);
    if (std::string errors = error_lines(parsed.diagnostics); !errors.empty()) {
      set_error(error, errors);
      return nullptr;
    }
    return hand_over(dd::dsl::print(parsed.model), error);
  });
}

char* dd_print_model(const char* model_json, char** error) {
  if (!model_json) {
    set_error(error, "model_json is NULL");
    return nullptr;
  }
  return guarded<char*>(error, nullptr, [&]() -> char* {
    auto json = dd::Json::parse(model_json);
    if (!json) {
      set_error(error, json.error().message);
      return nullptr;
    }
    auto model = dd::model_from_json(*json);
    if (!model) {
      set_error(error, model.error().message);
      return nullptr;
    }
    return hand_over(dd::dsl::print(*model), error);
  });
}

dd_document* dd_document_from_source(const char* source, char** error) {
  if (!source) {
    set_error(error, "source is NULL");
    return nullptr;
  }
  return guarded<dd_document*>(error, nullptr, [&]() -> dd_document* {
    dd::dsl::ParseResult parsed = dd::dsl::parse(source);
    if (std::string errors = error_lines(parsed.diagnostics); !errors.empty()) {
      set_error(error, errors);
      return nullptr;
    }
    return new dd_document{dd::History(std::move(parsed.model))};
  });
}

char* dd_document_source(const dd_document* doc) {
  if (!doc) return nullptr;
  return guarded<char*>(nullptr, nullptr, [&] { return copy_bytes(dd::dsl::print(doc->history.model())); });
}

char* dd_layout_json(const char* source, const char* file, const char* view_id, char** error) {
  if (!source) {
    set_error(error, "source is NULL");
    return nullptr;
  }
  return guarded<char*>(error, nullptr, [&]() -> char* {
    dd::host::Analysis a = analyze(source, file);
    if (a.errors > 0) {
      set_error(error, error_lines(a.parse.diagnostics));
      return nullptr;
    }
    dd::Model model = dd::with_default_view(a.parse.model);
    const auto& views = model.views.keys();
    std::string id = view_id ? std::string(view_id) : views.empty() ? std::string() : views.front();
    auto laid = dd::host::layout_view(model, id, a.libraries.set.renderer());
    if (!laid) {
      set_error(error, laid.error().message);
      return nullptr;
    }
    return hand_over(dd::to_json(laid->geometry).dump(), error);
  });
}

dd_status dd_render(const char* source, const char* file, const char* view_id, dd_format format, double scale,
                    char** out, size_t* out_len, char** error) {
  if (out) *out = nullptr;
  if (out_len) *out_len = 0;
  if (!source || !out || !out_len) {
    set_error(error, "NULL argument");
    return DD_INVALID_ARGUMENT;
  }
  using Format = dd::host::ExportFormat;
  Format host_format = Format::Svg;
  switch (format) {
    case DD_FORMAT_SVG: host_format = Format::Svg; break;
    case DD_FORMAT_PNG: host_format = Format::Png; break;
    case DD_FORMAT_PDF: host_format = Format::Pdf; break;
    case DD_FORMAT_DRAWIO: host_format = Format::Drawio; break;
    case DD_FORMAT_BPMN: host_format = Format::Bpmn; break;
    default:
      set_error(error, "unknown format");
      return DD_INVALID_ARGUMENT;
  }
  dd::host::ExportOptions options;
  if (view_id) options.view = view_id;
  // 0 or below means the default; NaN, infinity and scales above the limit are refused.
  if (!std::isfinite(scale) || scale > dd::host::kMaxPngScale) {
    set_error(error, "scale must be a finite number no larger than " + dd::js_number_string(dd::host::kMaxPngScale));
    return DD_INVALID_ARGUMENT;
  }
  if (scale > 0) options.scale = scale;

  return guarded<dd_status>(error, DD_FAILED, [&]() -> dd_status {
    dd::host::Analysis a = analyze(source, file);
    if (a.errors > 0) {
      set_error(error, error_lines(a.parse.diagnostics));
      return DD_REJECTED;
    }
    auto exported = dd::host::export_diagram(dd::with_default_view(a.parse.model), host_format, options,
                                             a.libraries.set.renderer());
    if (const auto* failure = std::get_if<dd::host::ExportError>(&exported)) {
      set_error(error, failure->message);
      switch (failure->kind) {
        case dd::host::ExportError::Kind::InvalidArgument: return DD_INVALID_ARGUMENT;
        case dd::host::ExportError::Kind::NotFound: return DD_NOT_FOUND;
        case dd::host::ExportError::Kind::Unsupported: return DD_UNSUPPORTED;
        case dd::host::ExportError::Kind::Failed: break;
      }
      return DD_FAILED;
    }
    const std::string& bytes = std::get<dd::host::Exported>(exported).bytes;
    *out = hand_over(bytes, error);
    if (!*out) return DD_FAILED;
    *out_len = bytes.size();
    return DD_OK;
  });
}

char* dd_import_json(const char* data, const char* file, const char* format, int keep_geometry, char** error) {
  if (!data) {
    set_error(error, "data is NULL");
    return nullptr;
  }
  return guarded<char*>(error, nullptr, [&]() -> char* {
    const std::string name = file ? file : "";
    std::optional<dd::io::Format> fmt = format ? dd::host::format_named(format) : dd::io::detect_format(name, data);
    if (!fmt) {
      set_error(error, format ? std::string("unknown format \"") + format +
                                    "\" (mermaid, plantuml, structurizr, drawio, bpmn)"
                              : "cannot tell the format of \"" + name + "\"; pass a format");
      return nullptr;
    }
    // Without a file, includes are not read at all (host::import_text).
    auto result = dd::host::import_text(data, *fmt, name, keep_geometry != 0);
    if (!result) {
      set_error(error, result.error().message);
      return nullptr;
    }
    dd::Json json = dd::io::to_json(*result);
    json.set("source", dd::Json(dd::dsl::print(result->model)));
    return hand_over(json.dump(), error);
  });
}

}  // extern "C"
