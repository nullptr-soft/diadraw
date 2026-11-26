// Port of the dispatch in packages/import/src/index.ts.

#include "diadraw/io/import.hpp"

#include <regex>

#include "diadraw/base/text.hpp"
#include "diadraw/io/plantuml.hpp"

namespace dd::io {

std::optional<Format> detect_format(std::string_view file_name, std::string_view text_view) {
  std::string name(file_name);
  for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string text(text_view);
  auto test = [](const std::string& s, const char* pattern) { return std::regex_search(s, std::regex(pattern)); };
  if (test(name, "\\.(mmd|mermaid)$")) return Format::Mermaid;
  if (test(name, "\\.(puml|plantuml|pu|iuml)$")) return Format::PlantUml;
  if (test(name, "\\.dsl$")) return Format::Structurizr;
  if (test(name, "\\.bpmn(2|20)?(\\.xml)?$")) return Format::Bpmn;
  // Structurizr workspace JSON.
  if (test(name, "\\.json$") && test(text, "\"model\"\\s*:") && test(text, "\"(softwareSystems|people|views)\"\\s*:"))
    return Format::Structurizr;
  if (test(name, "\\.(drawio|dio)$") || test(name, "\\.drawio\\.(xml|svg|png)$") ||
      (name.size() >= 5 && name.compare(name.size() - 5, 5, ".vsdx") == 0))
    return Format::Drawio;
  std::string_view head = js_trim(text_view);
  head = text_view.substr(static_cast<std::size_t>(head.data() - text_view.data()));
  if (!head.empty() && head.front() == '<') {
    if (test(text, "mxGraphModel|<mxfile")) return Format::Drawio;
    if (test(text, "spec/BPMN/20100524/MODEL")) return Format::Bpmn;
    return std::nullopt;
  }
  if (head.rfind("@startuml", 0) == 0) return Format::PlantUml;
  // /^\s*workspace\b/m and /^\s*(flowchart|graph)\b/m: at the start of any line.
  std::regex::flag_type flags = std::regex::ECMAScript | std::regex::multiline;
  if (std::regex_search(text, std::regex("^\\s*workspace\\b", flags))) return Format::Structurizr;
  if (std::regex_search(text, std::regex("^\\s*(flowchart|graph)\\b", flags))) return Format::Mermaid;
  return std::nullopt;
}

Result<ImportResult> import_diagram(std::string_view data, Format format, const ImportOptions& options) {
  switch (format) {
    case Format::Mermaid: return import_mermaid(data);
    case Format::Drawio: return import_drawio(data, options.drawio);
    case Format::Bpmn: return import_bpmn(data, options.bpmn);
    case Format::PlantUml:
      try {
        std::optional<std::string> file_name = options.file_name;
        return import_plantuml(data, {file_name, options.resolve});
      } catch (const std::exception& e) {
        return fail(std::string("PlantUML import failed: ") + e.what());
      }
    case Format::Structurizr: return import_structurizr(data, {options.keep_geometry, options.file_name, options.resolve});
    default: return fail("Importing this format is not available in the C++ core yet");
  }
}

}  // namespace dd::io
