#pragma once

// Shared import machinery, a port of packages/import/src/builder.ts: the report of what an
// importer could not carry over, identifiers from arbitrary text, and ModelBuilder, which builds
// models through core operations so imported models obey the same rules as edited ones.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/model/operations.hpp"

namespace dd::io {

struct ImportIssue {
  /// 1-based line in the source, when known.
  std::optional<int> line;
  std::string message;
  /// A short name of the construct that was dropped, for counting across files.
  std::optional<std::string> construct;
};

/// What an importer could not carry over. Nothing is dropped silently.
struct ImportReport {
  std::vector<ImportIssue> dropped;
  std::vector<std::string> notes;
  /// Shape names the importer did not recognise, with how often each appeared.
  std::optional<std::vector<std::pair<std::string, int>>> unknown_shapes;
  /// Style keys the importer did not recognise (kept in the passthrough copy).
  std::optional<std::vector<std::pair<std::string, int>>> unknown_styles;
  /// Counts of source items and what they became.
  std::optional<std::vector<std::pair<std::string, double>>> stats;
};

/// A file an include resolves to.
struct IncludedFile {
  std::string path;
  std::string text;
};

/// Reads local includes (Structurizr `!include` and `workspace extends`, PlantUML `!include`): a
/// file's text, or for a directory every file in it; nullopt when missing. URLs are never passed
/// here; importers report them as unresolved.
using IncludeResolver = std::function<std::optional<std::vector<IncludedFile>>(
    const std::string& path, const std::optional<std::string>& from)>;

struct ImportResult {
  Model model;
  ImportReport report;
};

Json to_json(const ImportReport& report);
Json to_json(const ImportResult& result);

/// A DSL identifier from arbitrary text: letters, digits and `_`, not a keyword, not starting with
/// a digit. Accents are dropped (`é` → `e`), as Unicode compatibility decomposition does.
std::string to_identifier(std::string_view text, std::string_view fallback = "node");

/// Strips HTML tags and decodes the common entities; collapses whitespace.
std::string plain_text(std::string_view html);
std::string decode_entities(std::string_view text);

struct ElementInit {
  std::string kind;
  std::string name;
  /// Source id of the parent element.
  std::optional<std::string> parent;
  std::optional<GroupId> group;
  /// Identifier to declare the element with; derived from the source id when absent.
  std::optional<std::string> local;
  std::optional<std::string> technology;
  std::optional<std::string> description;
  std::optional<std::string> shape;
  std::optional<std::vector<std::string>> tags;
  std::optional<Style> style;
  std::optional<Properties> properties;
};

struct RelationshipInit {
  std::optional<std::string> label;
  std::optional<std::string> technology;
  std::optional<LineStyle> line;
  bool bidirectional = false;
  std::optional<Style> style;
  std::optional<Properties> properties;
};

struct ViewInit {
  std::optional<Direction> direction;
  std::optional<std::string> algorithm;
  /// Source id of the scope element.
  std::optional<std::string> scope;
  /// Source ids, or `*`.
  std::optional<std::vector<std::string>> include;
  std::vector<LayoutHint> hints;
  std::optional<std::vector<SequenceItem>> sequence;
  bool autonumber = false;
};

class ModelBuilder {
 public:
  [[nodiscard]] const Model& model() const { return model_; }

  void drop(std::string message, std::optional<int> line = {}, std::optional<std::string> construct = {});
  void note(const std::string& message);

  /// Adds `use "name@range" as alias` once; returns the alias.
  std::string use_library(const std::string& name, const std::string& range, const std::string& alias);

  /// The DiaDraw id for a source id, if that element was added.
  [[nodiscard]] std::optional<ElementId> id(std::string_view source_id) const;
  [[nodiscard]] bool has(std::string_view source_id) const;
  /// The source id an element was added under.
  [[nodiscard]] std::optional<std::string> key_of(std::string_view id) const;

  ElementId add_element(const std::string& source_id, ElementInit init);

  /// Replaces fields of an element added earlier; `change` edits the element in place.
  template <class F>
  void patch(std::string_view source_id, F&& change) {
    auto id = this->id(source_id);
    const Element* el = id ? model_.elements.find(*id) : nullptr;
    if (!el) return;
    Element copy = *el;
    change(copy);
    std::string key = copy.id;  // before the move: argument order is unspecified
    model_.elements.set(std::move(key), std::move(copy));
  }

  void set_property(std::string_view source_id, const std::string& key, const std::string& value);
  void add_port(std::string_view source_id, const std::string& port);
  void set_view_spacing(std::string_view view_id, double spacing);

  /// Adds a group named `name` (unique by name within its parent group) and returns its id.
  GroupId add_group(const std::string& name, const std::optional<GroupId>& parent = {});

  std::optional<RelationshipId> add_relationship(const std::string& from, const std::string& to,
                                                 RelationshipInit init = {}, std::optional<int> line = {});

  ViewId add_view(const std::string& name, ViewInit init = {});

  [[nodiscard]] ImportResult result() const { return {model_, {dropped_, notes_, {}, {}, {}}}; }
  [[nodiscard]] const std::vector<ImportIssue>& dropped() const { return dropped_; }
  [[nodiscard]] const std::vector<std::string>& notes() const { return notes_; }

 private:
  void apply(const Operation& op);

  Model model_;
  std::map<std::string, ElementId, std::less<>> ids_;
  std::map<ElementId, std::string, std::less<>> keys_;
  std::vector<ImportIssue> dropped_;
  std::vector<std::string> notes_;
};

}  // namespace dd::io
