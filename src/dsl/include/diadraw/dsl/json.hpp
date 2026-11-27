#pragma once

// JSON forms of DSL results, in the shape of the TypeScript `ParseResult` and `SourceUpdate`.
// Offsets are converted to UTF-16 code units, which is what JavaScript hosts and editors use.

#include "diadraw/base/json.hpp"
#include "diadraw/dsl/patch.hpp"

namespace dd::dsl {

/// `{ model, diagnostics, sourceMap, operations }` (no tree).
Json to_json(const ParseResult& result, std::string_view source);
Json diagnostics_json(const std::vector<Diagnostic>& diagnostics, std::string_view source);
Json source_map_json(const SourceMap& map, std::string_view source);
/// `{ source, edits, model, minimal, diagnostics }`; edit offsets refer to `before`.
Json to_json(const SourceUpdate& update, std::string_view before);
/// Syntax colouring (`highlight`) as `[[from, to, tag], …]`: UTF-16 offsets and CodeMirror tag
/// names, what the JavaScript hosts take.
Json highlight_json(std::string_view text);
/// The foldable blocks (`fold_ranges`) as `[{ from, to, startLine, endLine }, …]`: the hidden
/// text in UTF-16 offsets, lines 0-based.
Json fold_ranges_json(std::string_view text);

}  // namespace dd::dsl
