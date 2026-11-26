// Python bindings: the `diadraw._core` extension module behind the `diadraw` package.
//
// The surface follows the command line (`check`, `format`, `render`, `import`) plus the model
// editing of the C ABI (`Document`: apply, undo, redo, resolve a view), with Python values in
// place of JSON strings: models, operations and resolved views cross as dicts and lists in the
// same shape as the model JSON. Text offsets and columns count code points, as Python strings do.
//
// Problems in a document raise `DiaDrawError` (a `ValueError`) carrying the diagnostics; what
// the CLI prints as warnings (unresolved libraries, kinds, shapes and themes they do not have,
// layout hints it could not honour) goes through Python's `warnings` module as `DiaDrawWarning`.
//
// Every call holds the GIL: the core keeps process-wide caches (libraries on disk, the draw.io
// stencil index) that are not guarded for concurrent use.

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/highlight.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/io/builder.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/model/history.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/model/views.hpp"
#include "host/diagram.hpp"
#include "host/libraries.hpp"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

/// Core text as a Python `str`. Imported data can carry bytes that are not UTF-8; they become
/// U+FFFD rather than a UnicodeDecodeError that would lose the whole result.
nb::str py_str(std::string_view s) {
  // Valid text (nearly all of it) is decoded once, with no copy on this side.
  if (PyObject* decoded = PyUnicode_DecodeUTF8(s.data(), static_cast<Py_ssize_t>(s.size()), nullptr))
    return nb::steal<nb::str>(decoded);
  PyErr_Clear();
  // Not CPython's "replace" handler: the core puts one U+FFFD per bad byte, as the other hosts do.
  const std::string valid = dd::to_valid_utf8(s);
  return nb::str(valid.data(), valid.size());
}

// ── Text offsets ──────────────────────────────────────────────────────────────────────────────

/// Converts UTF-8 byte offsets into `text` to code point offsets. Successive calls with
/// increasing offsets cost O(distance); a smaller offset restarts from the beginning.
class CodePoints {
 public:
  explicit CodePoints(std::string_view text) : text_(text) {}

  std::size_t at(std::size_t byte) {
    if (byte > text_.size()) byte = text_.size();
    if (byte < byte_) byte_ = point_ = 0;
    for (; byte_ < byte; ++byte_)
      if ((static_cast<unsigned char>(text_[byte_]) & 0xC0) != 0x80) ++point_;
    return point_;
  }

 private:
  std::string_view text_;
  std::size_t byte_ = 0;
  std::size_t point_ = 0;
};

/// Code points in `text[from, to)`.
std::size_t code_points_between(std::string_view text, std::size_t from, std::size_t to) {
  to = std::min(to, text.size());
  std::size_t n = 0;
  for (std::size_t i = from; i < to; ++i)
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++n;
  return n;
}

/// 1-based column, in code points, of byte offset `byte` in `text`.
std::size_t column_at(std::string_view text, std::size_t byte) {
  if (byte > text.size()) byte = text.size();
  std::size_t line_start = text.rfind('\n', byte == 0 ? std::string_view::npos : byte - 1);
  line_start = line_start == std::string_view::npos ? 0 : line_start + 1;
  return 1 + code_points_between(text, line_start, byte);
}

// ── Diagnostics and errors ────────────────────────────────────────────────────────────────────

/// One problem in a document, as Python sees it.
struct Diagnostic {
  std::string severity;  // "error" or "warning"
  std::string message;
  std::size_t line = 1;
  std::size_t column = 1;
  /// Code point offsets of the span in the source.
  std::size_t start = 0;
  std::size_t end = 0;

  [[nodiscard]] std::string str() const {
    return std::to_string(line) + ":" + std::to_string(column) + ": " + severity + ": " + message;
  }
};

std::vector<Diagnostic> diagnostics_of(const std::vector<dd::dsl::Diagnostic>& found, std::string_view source) {
  std::vector<Diagnostic> out;
  out.reserve(found.size());
  CodePoints points(source);
  for (const auto& d : found) {
    Diagnostic p;
    p.severity = d.severity == dd::dsl::Severity::Error ? "error" : "warning";
    p.message = dd::to_valid_utf8(d.message);
    p.line = static_cast<std::size_t>(d.line);
    p.column = column_at(source, d.span.from);
    // Diagnostics are sorted by where they start, so `points` only moves forward; the end is
    // counted from the start, as spans may overlap.
    p.start = points.at(d.span.from);
    p.end = p.start + (d.span.to > d.span.from ? code_points_between(source, d.span.from, d.span.to) : 0);
    out.push_back(std::move(p));
  }
  return out;
}

bool has_errors(const std::vector<dd::dsl::Diagnostic>& found) {
  for (const auto& d : found)
    if (d.severity == dd::dsl::Severity::Error) return true;
  return false;
}

/// Raised as `DiaDrawError`; `diagnostics` become the exception's `diagnostics` attribute.
struct Error : std::runtime_error {
  explicit Error(const std::string& message, std::vector<Diagnostic> found = {})
      : std::runtime_error(message), diagnostics(std::move(found)) {}
  std::vector<Diagnostic> diagnostics;
};

/// The `DiaDrawError` and `DiaDrawWarning` types, created when the module loads.
nb::handle error_type;
nb::handle warning_type;

[[noreturn]] void raise_document_errors(const std::vector<dd::dsl::Diagnostic>& found, std::string_view source) {
  auto diagnostics = diagnostics_of(found, source);
  std::size_t errors = 0;
  const Diagnostic* first = nullptr;
  for (const auto& d : diagnostics)
    if (d.severity == "error" && errors++ == 0) first = &d;
  std::string message = first ? first->str() : "the document has errors";
  if (errors > 1) message += " (and " + std::to_string(errors - 1) + " more)";
  throw Error(message, std::move(diagnostics));
}

void warn(const std::string& message) {
  // Stack level 1 is the Python line that called into the module (there is no Python wrapper).
  const std::string valid = dd::to_valid_utf8(message);
  if (PyErr_WarnEx(warning_type.ptr(), valid.c_str(), 1) != 0) throw nb::python_error();
}

// ── JSON ⇄ Python ─────────────────────────────────────────────────────────────────────────────

/// Deeper than any model; stops runaway recursion on self-referencing containers.
constexpr int kMaxDepth = 256;

nb::object to_python(const dd::Json& j, int depth = 0) {
  if (depth > kMaxDepth) throw Error("JSON nested too deeply");
  if (j.is_null()) return nb::none();
  if (j.is_bool()) return nb::bool_(j.as_bool());
  if (j.is_number()) {
    double d = j.as_number();
    // Whole numbers come back as int, as `json.loads` reads the model JSON.
    constexpr double kSafe = 9007199254740992.0;  // 2^53
    if (std::isfinite(d) && std::trunc(d) == d && std::fabs(d) <= kSafe)
      return nb::int_(static_cast<std::int64_t>(d));
    return nb::float_(d);
  }
  if (j.is_string()) return py_str(j.as_string());
  if (j.is_array()) {
    nb::list out;
    for (const auto& item : j.as_array()) out.append(to_python(item, depth + 1));
    return out;
  }
  nb::dict out;
  for (const auto& [key, value] : j.as_object()) out[py_str(key)] = to_python(value, depth + 1);
  return out;
}

dd::Json from_python(nb::handle h, int depth = 0) {
  if (depth > kMaxDepth) throw nb::value_error("value nested too deeply (or it contains itself)");
  if (h.is_none()) return {};
  if (nb::isinstance<nb::bool_>(h)) return dd::Json(nb::cast<bool>(h));
  if (nb::isinstance<nb::int_>(h)) {
    double d = PyLong_AsDouble(h.ptr());
    if (d == -1.0 && PyErr_Occurred()) throw nb::python_error();
    return dd::Json(d);
  }
  if (nb::isinstance<nb::float_>(h)) return dd::Json(nb::cast<double>(h));
  if (nb::isinstance<nb::str>(h)) return dd::Json(nb::cast<std::string>(h));
  if (nb::isinstance<nb::list>(h) || nb::isinstance<nb::tuple>(h)) {
    dd::Json::Array out;
    // The real size, not what a subclass's __len__ claims.
    const Py_ssize_t size = nb::isinstance<nb::list>(h) ? PyList_Size(h.ptr()) : PyTuple_Size(h.ptr());
    if (size > 0) out.reserve(static_cast<std::size_t>(size));
    for (nb::handle item : h) out.push_back(from_python(item, depth + 1));
    return dd::Json(std::move(out));
  }
  if (nb::isinstance<nb::dict>(h)) {
    dd::Json::Object out;
    if (const Py_ssize_t size = PyDict_Size(h.ptr()); size > 0) out.reserve(static_cast<std::size_t>(size));
    for (auto [key, value] : nb::borrow<nb::dict>(h)) {
      if (!nb::isinstance<nb::str>(key)) throw nb::type_error("dict keys must be strings");
      out.emplace_back(nb::cast<std::string>(key), from_python(value, depth + 1));
    }
    return dd::Json(std::move(out));
  }
  throw nb::type_error(("cannot convert " + nb::cast<std::string>(nb::str(h.type())) + " to JSON").c_str());
}

dd::Model model_from_python(nb::handle h) {
  auto model = dd::model_from_json(from_python(h));
  if (!model) throw Error("invalid model: " + model.error().message);
  return std::move(*model);
}

dd::Operation operation_from_python(nb::handle h) {
  auto op = dd::operation_from_json(from_python(h));
  if (!op) throw Error("invalid operation: " + op.error().message);
  return std::move(*op);
}

// ── Documents ─────────────────────────────────────────────────────────────────────────────────

/// A model with an undo history: the C ABI's `dd_document`.
class Document {
 public:
  Document() = default;
  explicit Document(dd::Model model) : history_(std::move(model)) {}

  static Document from_source(const std::string& source) {
    auto parsed = dd::dsl::parse(source);
    if (has_errors(parsed.diagnostics)) raise_document_errors(parsed.diagnostics, source);
    return Document(std::move(parsed.model));
  }
  static Document from_model(nb::handle model) { return Document(model_from_python(model)); }

  void apply(nb::handle operation) {
    auto applied = history_.apply(operation_from_python(operation));
    if (!applied) throw Error(applied.error().message);
  }
  bool undo() {
    if (!history_.can_undo()) return false;
    history_.undo();
    return true;
  }
  bool redo() {
    if (!history_.can_redo()) return false;
    history_.redo();
    return true;
  }
  [[nodiscard]] bool can_undo() const { return history_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return history_.can_redo(); }
  [[nodiscard]] nb::object model() const { return to_python(dd::to_json(history_.model())); }
  [[nodiscard]] nb::list views() const {
    nb::list out;
    for (const auto& id : history_.model().views.keys()) out.append(py_str(id));
    return out;
  }
  [[nodiscard]] nb::str source() const { return py_str(dd::dsl::print(history_.model())); }
  [[nodiscard]] nb::object resolve_view(const std::string& view_id) const {
    auto resolved = dd::resolve_view(history_.model(), view_id);
    if (!resolved) throw Error(resolved.error().message);
    return to_python(dd::to_json(*resolved));
  }
  [[nodiscard]] const dd::Model& raw() const { return history_.model(); }

 private:
  dd::History history_;
};

/// A document given as DiaDraw source (`str`), a model (`dict`) or a `Document`, with a default
/// view added when it has none (`with_default_view`) and the libraries it uses resolved offline
/// for `file`. Parse errors raise; library problems warn.
struct Loaded {
  dd::Model model;
  dd::library::Resolution libraries;
};

Loaded load(nb::handle document, const std::string& file) {
  if (nb::isinstance<nb::str>(document)) {
    std::string source = nb::cast<std::string>(document);
    dd::host::Analysis analysis = dd::host::analyze(source, file);
    if (analysis.errors > 0) raise_document_errors(analysis.parse.diagnostics, source);
    for (const auto& w : analysis.library_warnings) warn(w);
    return {with_default_view(std::move(analysis.parse.model)), std::move(analysis.libraries)};
  }
  // A Document's model is copied once; a converted dict is used as it is.
  dd::Model model = nb::isinstance<Document>(document) ? dd::Model(nb::cast<const Document&>(document).raw())
                                                        : model_from_python(document);
  model = with_default_view(std::move(model));
  auto libraries = dd::host::resolve_for_model(model, file);
  for (const auto& w : dd::host::library_warnings(model, libraries)) warn(w);
  return {std::move(model), std::move(libraries)};
}

// ── Functions ─────────────────────────────────────────────────────────────────────────────────

/// `parse(source)`: `(model, diagnostics)`; never raises for problems in the source.
std::tuple<nb::object, std::vector<Diagnostic>> parse(const std::string& source) {
  auto parsed = dd::dsl::parse(source);
  return {to_python(dd::to_json(parsed.model)), diagnostics_of(parsed.diagnostics, source)};
}

/// `check(source, file)`: every diagnostic, then the library problems as warnings (no span).
std::vector<Diagnostic> check(const std::string& source, const std::string& file) {
  dd::host::Analysis analysis = dd::host::analyze(source, file);
  auto out = diagnostics_of(analysis.parse.diagnostics, source);
  for (const auto& w : analysis.library_warnings) out.push_back({"warning", dd::to_valid_utf8(w), 1, 1, 0, 0});
  return out;
}

/// `format(source)`: the canonical text; raises when the source has errors.
nb::str format(const std::string& source) {
  auto parsed = dd::dsl::parse(source);
  if (has_errors(parsed.diagnostics)) raise_document_errors(parsed.diagnostics, source);
  return py_str(dd::dsl::print(parsed.model));
}

/// `print_model(model)`: a model (dict) as canonical DiaDraw source.
nb::str print_model(nb::handle model) { return py_str(dd::dsl::print(model_from_python(model))); }

/// `update_source(source, operation)`: applies an operation to the text, keeping the rest of it
/// as written: `{source, model, minimal, diagnostics}`.
nb::dict update_source(const std::string& source, nb::handle operation) {
  auto update = dd::dsl::update_source(source, operation_from_python(operation));
  if (!update) throw Error(update.error().message);
  nb::dict out;
  out["source"] = py_str(update->source);
  out["model"] = to_python(dd::to_json(update->model));
  out["minimal"] = update->minimal;
  out["diagnostics"] = diagnostics_of(update->result.diagnostics, update->source);
  return out;
}

/// `highlight(text)`: `[(start, end, kind)]` with code point offsets and CodeMirror tag names.
std::vector<std::tuple<std::size_t, std::size_t, std::string>> highlight(const std::string& text) {
  std::vector<std::tuple<std::size_t, std::size_t, std::string>> out;
  CodePoints points(text);
  for (const auto& t : dd::dsl::highlight(text)) {
    std::size_t from = points.at(t.from);
    std::size_t to = points.at(t.to);
    out.emplace_back(from, to, std::string(dd::dsl::token_kind_name(t.kind)));
  }
  return out;
}

/// `render(document, view, format, scale, background, file)`: `str` for svg, drawio and bpmn,
/// `bytes` for png and pdf.
nb::object render(nb::handle document, std::optional<std::string> view, const std::string& format, double scale,
                  std::optional<std::string> background, const std::string& file) {
  using Format = dd::host::ExportFormat;
  std::optional<Format> named = dd::host::export_format_named(format);
  if (!named) throw nb::value_error(("unknown format \"" + format + "\" (svg, png, pdf, drawio or bpmn)").c_str());
  const Format chosen = *named;
  dd::host::ExportOptions options;
  options.view = std::move(view);
  options.scale = scale;
  options.background = std::move(background);
  // Checked before loading, so the mistake shows before the document is laid out.
  if (auto invalid = dd::host::check_export_options(chosen, options)) throw Error(invalid->message);

  Loaded loaded = load(document, file);
  auto exported = dd::host::export_diagram(loaded.model, chosen, options, loaded.libraries.set.renderer());
  if (const auto* failure = std::get_if<dd::host::ExportError>(&exported)) throw Error(failure->message);
  const auto& done = std::get<dd::host::Exported>(exported);
  for (const auto& hint : done.ignored_hints) warn(dd::host::hint_warning(hint));
  if (chosen == Format::Png || chosen == Format::Pdf) return nb::bytes(done.bytes.data(), done.bytes.size());
  return py_str(done.bytes);
}

/// `import_diagram(data, format, file_name, keep_geometry)`: `{source, model, report}`.
nb::dict import_diagram(nb::handle data, std::optional<std::string> format, const std::string& file_name,
                        bool keep_geometry) {
  std::string text;
  if (nb::isinstance<nb::bytes>(data)) {
    auto bytes = nb::borrow<nb::bytes>(data);
    text.assign(bytes.c_str(), bytes.size());
  } else if (nb::isinstance<nb::str>(data)) {
    text = nb::cast<std::string>(data);
  } else {
    throw nb::type_error("data must be str or bytes");
  }
  std::optional<dd::io::Format> which =
      format ? dd::host::format_named(*format) : dd::io::detect_format(file_name, text);
  if (!which) {
    if (format) throw nb::value_error(("unknown format \"" + *format + "\" (mermaid, plantuml, structurizr, drawio or bpmn)").c_str());
    throw Error("cannot tell the format of " + (file_name.empty() ? std::string("the data") : file_name) +
                "; pass format=\"mermaid\", \"plantuml\", \"structurizr\", \"drawio\" or \"bpmn\"");
  }
  auto result = dd::host::import_text(text, *which, file_name, keep_geometry);
  if (!result) throw Error(result.error().message);
  nb::dict out;
  out["source"] = py_str(dd::dsl::print(result->model));
  out["model"] = to_python(dd::to_json(result->model));
  out["report"] = to_python(dd::io::to_json(result->report));
  return out;
}

}  // namespace

NB_MODULE(_core, m) {
  m.doc() = "The DiaDraw C++ core (libdiadraw). Use the `diadraw` package rather than this module.";

  // DiaDrawError(ValueError) with `diagnostics`, and DiaDrawWarning(UserWarning).
  nb::object error = nb::steal(PyErr_NewExceptionWithDoc(
      "diadraw.DiaDrawError", "A document DiaDraw cannot use; `diagnostics` lists what is wrong in its source.",
      PyExc_ValueError, nullptr));
  if (!error.is_valid()) throw nb::python_error();
  error.attr("diagnostics") = nb::tuple();
  m.attr("DiaDrawError") = error;
  error_type = error.release();  // kept alive for the life of the process, like the module
  nb::object warning = nb::steal(PyErr_NewExceptionWithDoc(
      "diadraw.DiaDrawWarning", "Something DiaDraw left out: an unresolved library, a layout hint it could not honour.",
      PyExc_UserWarning, nullptr));
  if (!warning.is_valid()) throw nb::python_error();
  m.attr("DiaDrawWarning") = warning;
  warning_type = warning.release();

  nb::class_<Diagnostic>(m, "Diagnostic", "A problem in DiaDraw source; line and column are 1-based, in code points.")
      .def_ro("severity", &Diagnostic::severity, "\"error\" or \"warning\".")
      .def_ro("message", &Diagnostic::message)
      .def_ro("line", &Diagnostic::line)
      .def_ro("column", &Diagnostic::column)
      .def_ro("start", &Diagnostic::start, "Code point offset where the problem starts.")
      .def_ro("end", &Diagnostic::end, "Code point offset where it ends.")
      .def("__str__", &Diagnostic::str)
      .def("__repr__", [](const Diagnostic& d) {
        return "<Diagnostic " + d.str() + ">";
      });

  nb::register_exception_translator([](const std::exception_ptr& p, void*) {
    try {
      std::rethrow_exception(p);
    } catch (const Error& e) {
      nb::str message = py_str(e.what());
      nb::object exc = nb::steal(PyObject_CallFunctionObjArgs(error_type.ptr(), message.ptr(), nullptr));
      if (!exc.is_valid()) return;  // the call set its own Python error
      nb::list diagnostics;
      for (const auto& d : e.diagnostics) diagnostics.append(nb::cast(d));
      if (PyObject_SetAttrString(exc.ptr(), "diagnostics", diagnostics.ptr()) != 0) return;
      PyErr_SetObject(error_type.ptr(), exc.ptr());
    }
  });

  m.def("_set_drawio_dir", [](const std::string& dir) { dd::host::set_drawio_dir(dir); }, "dir"_a,
        "Where draw.io's stencil sets are (the package's own copy); call before anything resolves libraries.");
  m.def("version", [] { return std::string(dd_version()); }, "The version of the DiaDraw core.");

  nb::class_<Document>(m, "Document", "A model with an undo history.")
      .def(nb::init<>(), "An empty model.")
      .def_static("from_source", &Document::from_source, "source"_a, "A document from DiaDraw source; raises DiaDrawError on errors.")
      .def_static("from_model", &Document::from_model, "model"_a, "A document from a model dict (the model JSON).")
      .def("apply", &Document::apply, "operation"_a,
           "Applies one operation, e.g. {\"type\": \"addElement\", \"element\": {...}}.")
      .def("undo", &Document::undo, "Undoes the last operation; False when there was none.")
      .def("redo", &Document::redo, "Redoes the last undone operation; False when there was none.")
      .def_prop_ro("can_undo", &Document::can_undo)
      .def_prop_ro("can_redo", &Document::can_redo)
      .def_prop_ro("model", &Document::model, "The model as a new dict, in the shape of the model JSON; each access converts the whole model.")
      .def_prop_ro("views", &Document::views, "The ids of the views, in order.")
      .def("to_source", &Document::source, "The model as canonical DiaDraw source.")
      .def("resolve_view", &Document::resolve_view, "view"_a,
           "The elements, relationships and groups a view shows, as a dict.");

  m.def("parse", &parse, "source"_a, "Parses DiaDraw source: (model dict, diagnostics). Never raises for errors in it.");
  m.def("check", &check, "source"_a, nb::kw_only(), "file"_a = "", "Every problem in DiaDraw source, libraries included.");
  m.def("format", &format, "source"_a, "DiaDraw source in canonical form; raises DiaDrawError on errors.");
  m.def("print_model", &print_model, "model"_a, "A model dict as canonical DiaDraw source.");
  m.def("update_source", &update_source, "source"_a, "operation"_a,
        "Applies an operation to DiaDraw source, keeping the rest of the text as written.");
  m.def("highlight", &highlight, "text"_a, "Syntax tokens: (start, end, kind) with code point offsets.");
  m.def("render", &render, "document"_a, "view"_a = nb::none(), nb::kw_only(), "format"_a = "svg", "scale"_a = 2.0,
        "background"_a = nb::none(), "file"_a = "", "Lays out and draws a document.");
  m.def("import_diagram", &import_diagram, "data"_a, "format"_a = nb::none(), nb::kw_only(), "file_name"_a = "",
        "keep_geometry"_a = true, "Converts a Mermaid, PlantUML, Structurizr, draw.io or BPMN diagram.");
}
