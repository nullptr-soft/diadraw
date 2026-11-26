#pragma once

// BPMN 2.0 import and export, a port of packages/import/src/bpmn.

#include <string>
#include <string_view>

#include "diadraw/io/builder.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/io/xml.hpp"

namespace dd::io {

struct BpmnOptions {
  /// BPMN DI becomes fixed views (the default); else one auto-laid-out view.
  bool keep_geometry = true;
};

/// Imports BPMN 2.0 XML: processes, collaborations (pools, lanes, message flows), every flow node
/// type, data, artifacts, conversations and choreographies. Each diagram plane becomes a fixed
/// view. Shapes come from the built-in `bpmn` library; what DiaDraw has no field for is kept in
/// `bpmn.*` properties so the exporter can write it back. Anything not imported is reported.
ImportResult import_bpmn(std::string_view xml, const BpmnOptions& options = {});

/// One diagram to write: a view and its laid-out geometry (the same shape as a draw.io page).
using BpmnPage = DrawioPage;

/// Writes a model as BPMN 2.0 XML with diagram interchange, one diagram per page (a port of
/// bpmn/export.ts). Elements imported from BPMN get their type, ids, event definitions, loops,
/// conditions, scripts, vendor attributes and extension elements back from their `bpmn.*`
/// properties; others are mapped by shape and kind.
std::string export_bpmn(const Model& model, const std::vector<BpmnPage>& pages);

/// Pages from views with fixed geometry (`pin … at … size`, `route`), without running a layout:
/// what an imported BPMN file's views hold.
std::vector<BpmnPage> fixed_pages(const Model& model);

namespace bpmn {

/// The local part of a qualified XML name.
std::string_view local(std::string_view name);
/// The element kind in the DiaDraw model for a BPMN type.
std::string kind_of(std::string_view type);
/// Serialises an XML node back to text (extension elements kept as passthrough).
std::string serialize_xml(const XmlNode& node);
std::string escape_attr(std::string_view v);
std::string escape_text(std::string_view v);

}  // namespace bpmn

}  // namespace dd::io
