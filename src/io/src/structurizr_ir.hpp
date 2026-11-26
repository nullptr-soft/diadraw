#pragma once

// The workspace both Structurizr readers produce (structurizr/ir.ts), and the model built from it.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "diadraw/io/builder.hpp"
#include "diadraw/io/structurizr.hpp"

namespace dd::io::structurizr {

/// A string-keyed record in insertion order, where setting a key again keeps its place.
struct Record {
  std::vector<std::pair<std::string, std::string>> items;

  void set(const std::string& key, std::string value) {
    for (auto& [k, v] : items)
      if (k == key) {
        v = std::move(value);
        return;
      }
    items.emplace_back(key, std::move(value));
  }
  [[nodiscard]] const std::string* get(std::string_view key) const {
    for (const auto& [k, v] : items)
      if (k == key) return &v;
    return nullptr;
  }
  [[nodiscard]] bool empty() const { return items.empty(); }
};

enum class ElementType {
  Person,
  SoftwareSystem,
  Container,
  Component,
  Element,
  DeploymentNode,
  InfrastructureNode,
  ContainerInstance,
  SoftwareSystemInstance
};

/// `softwareSystem`, … (the TypeScript string of the type).
const char* type_name(ElementType type);

struct IrElement {
  /// Unique key (the hierarchical identifier for the DSL, the id for JSON).
  std::string key;
  /// The identifier to declare it with in DiaDraw.
  std::string local;
  /// JSON only: the DSL identifier the workspace was written with.
  std::optional<std::string> identifier;
  ElementType type = ElementType::Element;
  std::string name;
  std::optional<std::string> description;
  std::optional<std::string> technology;
  std::vector<std::string> tags;
  Record properties;
  std::optional<std::string> parent;
  /// Group path, `/`-separated.
  std::optional<std::string> group;
  std::optional<std::string> environment;
  /// For instances: the key of the container or software system.
  std::optional<std::string> instance_of;
  std::optional<int> line;
};

struct IrRelationship {
  std::optional<std::string> key;
  std::string source;
  std::string destination;
  std::optional<std::string> description;
  std::optional<std::string> technology;
  std::vector<std::string> tags;
  Record properties;
  std::optional<int> line;
};

enum class ViewType { SystemLandscape, SystemContext, Container, Component, Filtered, Dynamic, Deployment, Custom };

struct IrStep {
  std::string source;
  std::string destination;
  std::optional<std::string> description;
  std::optional<std::string> technology;
  /// Steps in the same parallel group share a number.
  std::optional<double> parallel;
};

struct AutoLayout {
  Direction direction = Direction::Down;
  std::optional<double> rank_separation;
  std::optional<double> node_separation;
};

struct IrView {
  ViewType type = ViewType::Custom;
  std::string key;
  std::optional<std::string> subject;
  std::optional<std::string> environment;
  /// Filtered views: the base view, and whether listed tags are kept or dropped.
  std::optional<std::string> base_key;
  bool filter_exclude = false;
  std::vector<std::string> filter_tags;
  std::vector<std::string> include;
  std::vector<std::string> exclude;
  std::optional<AutoLayout> auto_layout;
  /// Element positions (JSON workspaces), by element key.
  std::map<std::string, Point2, std::less<>> positions;
  /// Relationship bend points (JSON workspaces), by relationship key, in JavaScript object order.
  std::vector<std::pair<std::string, std::vector<Point2>>> vertices;
  std::optional<std::vector<IrStep>> steps;
  std::optional<int> line;
};

struct TagStyle {
  std::string tag;
  Record props;
};

struct Workspace {
  std::optional<std::string> name;
  /// Held by pointer so the DSL reader can keep references while the list grows.
  std::vector<std::shared_ptr<IrElement>> elements;
  std::vector<std::shared_ptr<IrRelationship>> relationships;
  std::vector<std::shared_ptr<IrView>> views;
  std::vector<std::shared_ptr<TagStyle>> element_styles;
  std::vector<std::shared_ptr<TagStyle>> relationship_styles;
  /// Whether relationships between children imply ones between parents (DSL default: yes).
  bool implied_relationships = true;
  /// Whether instances inherit the relationships of what they instantiate (DSL: yes; JSON lists them).
  std::optional<bool> replicate_instances;
};

Workspace parse_json(std::string_view text, ModelBuilder& b);
Workspace parse_dsl(std::string_view source, ModelBuilder& b, const StructurizrOptions& options);
ImportResult build_model(const Workspace& ws, ModelBuilder& b, const StructurizrOptions& options);

/// `s.split(sep).map(trim).filter(Boolean)`.
std::vector<std::string> split_list(std::string_view s, char sep = ',');
/// JavaScript `s.toLowerCase()` for the ASCII letters.
std::string ascii_lower(std::string_view s);
/// `s.slice(0, n)` in UTF-16 code units.
std::string utf16_prefix(std::string_view s, std::size_t n);

}  // namespace dd::io::structurizr
