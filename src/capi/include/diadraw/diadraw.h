/*
 * DiaDraw C ABI.
 *
 * The stable entry point for native shells and language bindings (Swift, C#, Kotlin through JNI,
 * Rust: bindings/rust). Rules:
 *   - Handles are opaque. Every `dd_*_new` has a matching `dd_*_free`.
 *   - Strings are UTF-8. Strings the library returns are owned by the caller and released with
 *     `dd_string_free`.
 *   - Structured data crosses as JSON, in the same shape as the TypeScript objects.
 *   - Functions never throw or abort on bad input; they return an error code and, when `error`
 *     is not NULL, set `*error` to a message the caller frees with `dd_string_free`.
 *   - The ABI only grows. `DD_ABI_VERSION` changes when something is added.
 *   - Text inputs are NUL-terminated UTF-8. Diagnostics count columns in UTF-16 code units, and the
 *     offsets in parse JSON are UTF-16 code units too, as in `@diadraw/wasm`.
 *
 * Linking: ABI 1 is in the `diadraw` library. ABI 2 (the DSL, layout, rendering and import) is in
 * `diadraw_capi`, which also needs `diadraw_host` and `diadraw_export` and their dependencies; the
 * build writes the whole list, in link order, to `<build>/diadraw_capi_libs-<config>.txt`.
 * The core keeps caches without locks: call it from one thread at a time.
 */
#ifndef DIADRAW_H
#define DIADRAW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DD_ABI_VERSION 2

typedef struct dd_document dd_document;

typedef enum dd_status {
  DD_OK = 0,
  DD_INVALID_ARGUMENT = 1, /* NULL handle or malformed JSON */
  DD_REJECTED = 2,         /* well-formed, but the model refused it (e.g. a duplicate id) */
  DD_NOT_FOUND = 3,
  DD_UNSUPPORTED = 4,      /* since ABI 2: this build cannot do it (e.g. it has no PNG backend) */
  DD_FAILED = 5            /* since ABI 2: the core failed (a view it could not lay out or draw,
                              an exception, out of memory) */
} dd_status;

/* ABI 2: output formats of `dd_render`; any other value is refused with DD_INVALID_ARGUMENT. */
typedef enum dd_format {
  DD_FORMAT_SVG = 0,
  DD_FORMAT_PNG = 1,
  DD_FORMAT_PDF = 2,
  DD_FORMAT_DRAWIO = 3, /* draw.io XML, one page per view */
  DD_FORMAT_BPMN = 4    /* BPMN 2.0 XML, one diagram per view */
} dd_format;

/* Library version, e.g. "0.0.1"; the repository's VERSION file. Static storage; do not free. */
const char* dd_version(void);
int dd_abi_version(void);

void dd_string_free(char* s);

/* A document with an empty model. */
dd_document* dd_document_new(void);
/* A document from model JSON; NULL on error. */
dd_document* dd_document_from_json(const char* model_json, char** error);
void dd_document_free(dd_document* doc);

/* Applies one operation (JSON, e.g. {"type":"addElement","element":{...}}). */
dd_status dd_document_apply(dd_document* doc, const char* operation_json, char** error);
/* Undo and redo; return 1 when something changed, 0 otherwise. */
int dd_document_undo(dd_document* doc);
int dd_document_redo(dd_document* doc);
int dd_document_can_undo(const dd_document* doc);
int dd_document_can_redo(const dd_document* doc);

/* The model as JSON. */
char* dd_document_model_json(const dd_document* doc);
/* The elements, relationships and groups a view shows, as JSON; NULL on error. */
char* dd_document_resolve_view_json(const dd_document* doc, const char* view_id, char** error);

/* ── ABI 2: the DSL, layout, rendering and import (library `diadraw_capi`) ────────────────────
 *
 * Functions that take `file` resolve the document's libraries as the `diadraw` command line does:
 * the lockfile next to `file` (which need not exist), the library cache, then the built-in
 * libraries. With `file` NULL they resolve from the built-in libraries and draw.io's stencil sets
 * only. Functions that return a string without an `error` parameter return NULL when an argument
 * is NULL or the core failed (out of memory).
 */

/* Parses DSL source: `{ model, diagnostics, sourceMap, operations }` as JSON, the shape of
 * `parse` in `@diadraw/wasm`. */
char* dd_parse_json(const char* source);

/* Checks DSL source as `diadraw check` does: `{ "errors": n, "diagnostics": [{ "line", "column",
 * "severity": "error"|"warning", "message" }], "warnings": ["…"] }` where `warnings` are what the
 * libraries could not supply (unresolved libraries, unknown kinds, shapes and themes). */
char* dd_check_json(const char* source, const char* file);

/* Canonical source for DSL source, as `diadraw format`; NULL with `*error` set when it has errors. */
char* dd_format_source(const char* source, char** error);

/* Canonical source for model JSON; NULL on error. */
char* dd_print_model(const char* model_json, char** error);

/* A document from DSL source; NULL with `*error` set (the diagnostics, one per line) when the
 * source has errors. */
dd_document* dd_document_from_source(const char* source, char** error);

/* The document's model as canonical DSL source. NULL when `doc` is NULL. */
char* dd_document_source(const dd_document* doc);

/* Lays out view `view_id` of DSL source (NULL: the first view, or the default view of a document
 * without views): `{ "width", "height", "nodes": { id: { x, y, width, height } }, "edges": { id:
 * { "points": [{ x, y }], "label"?: { x, y, width, height } } }, "rotations": { id: degrees },
 * "ignoredHints": [hint] }`. NULL on error (source with errors, an unknown view). */
char* dd_layout_json(const char* source, const char* file, const char* view_id, char** error);

/* Renders DSL source. SVG, PNG and PDF draw one view (`view_id`, or the first when NULL); draw.io
 * and BPMN write `view_id` or, when NULL, every view. `scale` is the PNG pixel density: 0 or less
 * means 2, and above 8, infinite or NaN is DD_INVALID_ARGUMENT (so is a PNG over 16384 pixels a
 * side or 2^28 pixels, as DD_FAILED). On DD_OK, `*out` holds `*out_len` bytes (plus a NUL past the end), freed with
 * `dd_string_free`. Returns DD_REJECTED when the source has errors, DD_NOT_FOUND for an unknown
 * view and DD_UNSUPPORTED when this build has no backend for `format`. */
dd_status dd_render(const char* source, const char* file, const char* view_id, dd_format format, double scale,
                    char** out, size_t* out_len, char** error);

/* Imports a diagram in another format: `{ "source": "…DSL…", "model": {…}, "report": { "dropped",
 * "notes", … } }`. `format` is "mermaid", "plantuml", "structurizr", "drawio" or "bpmn", or NULL
 * to tell from `file` and the data. Includes (Structurizr `!include`, PlantUML `!include`) are
 * read from disk only when `file` is given: relative to it, or at an absolute path, never over
 * the network. With `file` NULL they are not read and the report says so; pass NULL for data
 * from an untrusted source. `keep_geometry` non-zero keeps the positions of formats that
 * have them. NULL on error. */
char* dd_import_json(const char* data, const char* file, const char* format, int keep_geometry, char** error);

#ifdef __cplusplus
}
#endif

#endif /* DIADRAW_H */
