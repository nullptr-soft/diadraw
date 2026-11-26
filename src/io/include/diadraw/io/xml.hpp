#pragma once

// The small, forgiving XML reader of packages/import/src/drawio.ts (`parseXml`): elements,
// attributes and text, nothing else. Malformed markup is skipped, never fatal. The importers that
// share it (draw.io, BPMN) depend on its exact behaviour, so it is a port, not a general parser.

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dd::io {

struct XmlNode {
  std::string name;
  /// In document order; a repeated attribute keeps its first place and takes the last value.
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<std::unique_ptr<XmlNode>> children;
  std::string text;

  [[nodiscard]] const std::string* attr(std::string_view key) const {
    for (const auto& [k, v] : attrs)
      if (k == key) return &v;
    return nullptr;
  }
  /// First child element with this name, or nullptr.
  [[nodiscard]] const XmlNode* child(std::string_view child_name) const {
    for (const auto& c : children)
      if (c->name == child_name) return c.get();
    return nullptr;
  }
};

/// Parses XML into a `#document` root node.
std::unique_ptr<XmlNode> parse_xml(std::string_view xml);

/// Calls `f` on the node and every descendant, parents first.
template <class F>
void walk_xml(const XmlNode& node, F&& f) {
  f(node);
  for (const auto& c : node.children) walk_xml(*c, f);
}

}  // namespace dd::io
