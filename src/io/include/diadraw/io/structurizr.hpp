#pragma once

// Structurizr import (DSL and workspace JSON), a port of packages/import/src/structurizr.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/io/builder.hpp"

namespace dd::io {

struct StructurizrOptions {
  /// Positions in the workspace make fixed-layout views (the default); else auto-layout.
  bool keep_geometry = true;
  /// The file being imported, for resolving relative includes.
  std::optional<std::string> file_name;
  IncludeResolver resolve;
};

/// Imports a Structurizr workspace, DSL or JSON (detected from the text). Every view becomes a
/// DiaDraw view; JSON views with positions keep them (fixed layout) unless geometry is dropped.
ImportResult import_structurizr(std::string_view source, const StructurizrOptions& options = {});

}  // namespace dd::io
