#pragma once

// PlantUML import, a port of packages/import/src/plantuml.

#include <optional>
#include <string>
#include <string_view>

#include "diadraw/io/builder.hpp"

namespace dd::io {

struct PlantUmlOptions {
  /// The file being imported, for resolving relative includes.
  std::optional<std::string> file_name;
  /// Reads local includes; only the first file a path resolves to is used.
  IncludeResolver resolve;
};

/// Imports PlantUML: the preprocessor, then class, object, component, deployment, use case, state,
/// activity (new and legacy), sequence, C4-PlantUML, mindmap and WBS diagrams. Each
/// `@start…`/`@end…` block becomes a view. Lines that are not understood are reported with their
/// line numbers and a construct name.
ImportResult import_plantuml(std::string_view source, const PlantUmlOptions& options = {});

}  // namespace dd::io
