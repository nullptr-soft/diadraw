#pragma once

// Importers, a port of packages/import/src: each turns a foreign format into a model, with a
// report of what could not be carried over. Formats are added milestone by milestone.

#include <optional>
#include <string>
#include <string_view>

#include "diadraw/io/builder.hpp"
#include "diadraw/io/bpmn.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/io/structurizr.hpp"

namespace dd::io {

enum class Format { Mermaid, PlantUml, Structurizr, Drawio, Bpmn };

/// Guesses the format from the file name, then from the content.
std::optional<Format> detect_format(std::string_view file_name, std::string_view text);

/// A Mermaid flowchart (`flowchart` / `graph`): nodes and shapes, links with labels, chains and
/// `&`, subgraphs as groups, the direction. Styling and interaction are reported.
ImportResult import_mermaid(std::string_view source);

struct ImportOptions {
  /// draw.io: positions, stencil artwork.
  DrawioOptions drawio;
  /// BPMN: keep the diagram interchange geometry.
  BpmnOptions bpmn;
  /// Keep positions where the format has them (Structurizr workspace JSON; draw.io and BPMN
  /// have their own switches above).
  bool keep_geometry = true;
  /// The file being imported, for resolving relative includes.
  std::optional<std::string> file_name;
  /// Reads local includes (Structurizr and PlantUML); none when unset.
  IncludeResolver resolve;
};

/// Imports a file's contents (text, or raw bytes for draw.io PNGs) in a format this core supports
/// so far; others are refused.
Result<ImportResult> import_diagram(std::string_view data, Format format, const ImportOptions& options = {});

}  // namespace dd::io
