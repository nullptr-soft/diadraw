#pragma once

// Operations → minimal text patches, a port of packages/dsl/src/patch.ts.
//
// A canvas edit becomes the smallest change to the source that produces the same model, keeping
// every comment and all formatting outside the touched statements. When an operation cannot be
// expressed as a local patch, the file is reprinted instead.

#include <string>
#include <vector>

#include "diadraw/base/result.hpp"
#include "diadraw/dsl/parse.hpp"

namespace dd::dsl {

/// A replacement of `from..to` (byte offsets in the text before the edit) with `insert`.
struct TextEdit {
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  std::string insert;
  bool operator==(const TextEdit&) const = default;
};

struct SourceUpdate {
  std::string source;
  /// Non-overlapping edits against the old source, ready for an editor transaction.
  std::vector<TextEdit> edits;
  /// Parse of the new source.
  ParseResult result;
  /// The model the operation produces; the new source parses to the same thing.
  Model model;
  /// False when the operation could not be patched in place and the file was reprinted.
  bool minimal = true;
};

std::string apply_edits(std::string_view source, std::vector<TextEdit> edits);

/// One edit covering everything that differs between two texts (never splitting a character).
std::vector<TextEdit> diff_edit(std::string_view before, std::string_view after);

/// Applies an operation to source text. Fails when the operation is invalid for the source's
/// model. `parsed` may pass in an existing parse of `source` to save reparsing it.
Result<SourceUpdate> update_source(std::string_view source, const Operation& op,
                                   const ParseResult* parsed = nullptr);

}  // namespace dd::dsl
