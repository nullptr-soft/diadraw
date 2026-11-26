#pragma once

// The DiaDraw document model, a port of `@diadraw/core` (packages/core/src/model.ts).
//
// Elements, relationships and groups are defined once in the model. A view picks a subset of the
// model, styles it and carries layout hints. The model holds no geometry: positions come from the
// layout service, steered by the hints on each view.
//
// Change a model only through operations (operations.hpp). Collections share their values between
// snapshots, so copying a `Model` is cheap.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "diadraw/base/ordered_map.hpp"

namespace dd {

using ElementId = std::string;
using RelationshipId = std::string;
using GroupId = std::string;
using ViewId = std::string;

/// Ordered `key → value` pairs, as a JavaScript `Record<string, string>` keeps them.
class StringMap {
 public:
  StringMap() = default;
  StringMap(std::initializer_list<std::pair<std::string, std::string>> items) {
    for (const auto& [k, v] : items) set(k, v);
  }

  [[nodiscard]] const std::string* find(std::string_view key) const {
    for (const auto& [k, v] : items_)
      if (k == key) return &v;
    return nullptr;
  }
  void set(std::string key, std::string value) {
    for (auto& [k, v] : items_)
      if (k == key) {
        v = std::move(value);
        return;
      }
    items_.emplace_back(std::move(key), std::move(value));
  }
  [[nodiscard]] bool empty() const { return items_.empty(); }
  [[nodiscard]] std::size_t size() const { return items_.size(); }
  [[nodiscard]] auto begin() const { return items_.begin(); }
  [[nodiscard]] auto end() const { return items_.end(); }

  /// Same pairs in any order.
  friend bool operator==(const StringMap& a, const StringMap& b) {
    if (a.size() != b.size()) return false;
    for (const auto& [k, v] : a.items_) {
      const std::string* o = b.find(k);
      if (!o || *o != v) return false;
    }
    return true;
  }

 private:
  std::vector<std::pair<std::string, std::string>> items_;
};

/// Free-form properties attached to model items.
using Properties = StringMap;
/// Visual style as `key → value` (see docs/dsl.md). Unknown keys are kept.
using Style = StringMap;

struct Point2 {
  double x = 0;
  double y = 0;
  bool operator==(const Point2&) const = default;
};

enum class PortSide { North, South, East, West };

/// A named connection point on an element. Relationships may attach to a port.
struct Port {
  std::string id;
  std::optional<PortSide> side;
  bool operator==(const Port&) const = default;
};

struct Element {
  ElementId id;
  /// Open-ended: `person`, `system`, `container`, `process`, `task`, … or a plugin's own kind.
  std::string kind;
  /// Display name; the DSL defaults it to the id.
  std::string name;
  std::optional<ElementId> parent;
  std::optional<GroupId> group;
  std::optional<std::string> technology;
  std::optional<std::string> description;
  std::optional<std::string> shape;
  std::vector<std::string> tags;
  std::vector<Port> ports;
  Properties properties;
  std::optional<Style> style;

  bool operator==(const Element&) const = default;
};

enum class LineStyle { Solid, Dashed };

struct Endpoint {
  ElementId element;
  std::optional<std::string> port;
  bool operator==(const Endpoint&) const = default;
};

struct Relationship {
  RelationshipId id;
  Endpoint source;
  Endpoint target;
  std::optional<std::string> label;
  std::optional<std::string> technology;
  LineStyle line = LineStyle::Solid;
  bool bidirectional = false;
  std::vector<std::string> tags;
  Properties properties;
  std::optional<Style> style;
  /// Set on relationships a view lifted from hidden descendants; never stored in a model.
  bool implied = false;

  bool operator==(const Relationship&) const = default;
};

/// A named boundary around elements that is not itself part of the model's hierarchy.
struct Group {
  GroupId id;
  std::string name;
  std::optional<GroupId> parent;
  bool operator==(const Group&) const = default;
};

enum class Direction { Right, Left, Down, Up };

/// Layout algorithms with an engine of their own, in the order docs and completion list them.
/// Any other name runs `layered`; the DSL warns about it.
inline constexpr std::string_view kLayoutAlgorithms[] = {"layered", "tree",  "grid",    "radial",
                                                         "force",   "fixed", "sequence"};

/// Whether `name` is one of `kLayoutAlgorithms`.
inline bool is_layout_algorithm(std::string_view name) {
  for (std::string_view a : kLayoutAlgorithms)
    if (a == name) return true;
  return false;
}

struct LayoutOptions {
  /// Layout algorithm: one of `kLayoutAlgorithms`.
  std::string algorithm = "layered";
  Direction direction = Direction::Down;
  /// Minimum spacing between nodes, in px.
  std::optional<double> spacing;
  bool operator==(const LayoutOptions&) const = default;
};

// ── Layout hints ──────────────────────────────────────────────────────────────────────────────

enum class Placement { Below, Above, LeftOf, RightOf };
enum class Axis { Horizontal, Vertical };

/// `pin a below b`
struct PinHint {
  ElementId element;
  Placement placement = Placement::Below;
  ElementId reference;
  bool operator==(const PinHint&) const = default;
};

/// `pin a at x, y [size w, h]`
struct PinAtHint {
  ElementId element;
  double x = 0;
  double y = 0;
  std::optional<double> width;
  std::optional<double> height;
  bool operator==(const PinAtHint&) const = default;
};

/// A fixed route for a relationship: bend points, and where it leaves and enters (0..1 of the box).
struct RouteHint {
  RelationshipId relationship;
  std::vector<Point2> points;
  std::optional<Point2> exit;
  std::optional<Point2> entry;
  bool operator==(const RouteHint&) const = default;
};

struct AlignHint {
  std::vector<ElementId> elements;
  Axis axis = Axis::Horizontal;
  bool operator==(const AlignHint&) const = default;
};

struct OrderHint {
  std::vector<ElementId> elements;
  bool operator==(const OrderHint&) const = default;
};

/// `size a 160, 80`: the element's box in this view. Every layout honours it: auto layouts arrange
/// the view around it, and in a fixed view it is the size of a `pin … at` without one.
struct SizeHint {
  ElementId element;
  double width = 0;
  double height = 0;
  bool operator==(const SizeHint&) const = default;
};

/// `rotate a 30`: the element is drawn turned by this many degrees, clockwise, about its centre.
/// Auto layouts make room for the turned shape; lines meet its turned outline.
struct RotateHint {
  ElementId element;
  double degrees = 0;
  bool operator==(const RotateHint&) const = default;
};

using LayoutHint = std::variant<PinHint, PinAtHint, RouteHint, AlignHint, OrderHint, SizeHint, RotateHint>;

// ── Sequence views ────────────────────────────────────────────────────────────────────────────

enum class MessageStyle { Sync, Async, Reply };
enum class FrameKind { Alt, Opt, Loop, Par, Critical, Break, Group };
enum class NotePosition { Over, Left, Right };

struct SequenceMessage {
  ElementId from;
  ElementId to;
  std::optional<std::string> label;
  MessageStyle style = MessageStyle::Sync;
  bool operator==(const SequenceMessage&) const = default;
};

/// `activate x` (activate = true) or `deactivate x`.
struct SequenceActivation {
  bool activate = true;
  ElementId element;
  bool operator==(const SequenceActivation&) const = default;
};

struct SequenceNote {
  NotePosition position = NotePosition::Over;
  std::vector<ElementId> over;
  std::string text;
  bool operator==(const SequenceNote&) const = default;
};

struct SequenceDivider {
  std::optional<std::string> label;
  bool operator==(const SequenceDivider&) const = default;
};

struct SequenceDelay {
  std::optional<std::string> label;
  bool operator==(const SequenceDelay&) const = default;
};

struct SequenceRef {
  std::vector<ElementId> over;
  std::string label;
  bool operator==(const SequenceRef&) const = default;
};

struct SequenceItem;

struct FrameSection {
  std::optional<std::string> label;
  std::vector<SequenceItem> items;
  bool operator==(const FrameSection&) const;
};

struct SequenceFrame {
  FrameKind frame = FrameKind::Group;
  std::vector<FrameSection> sections;
  bool operator==(const SequenceFrame&) const = default;
};

/// One step of a sequence view, in order from top to bottom.
struct SequenceItem
    : std::variant<SequenceMessage, SequenceActivation, SequenceNote, SequenceDivider,
                   SequenceDelay, SequenceRef, SequenceFrame> {
  using variant::variant;
  bool operator==(const SequenceItem& o) const {
    return static_cast<const variant&>(*this) == static_cast<const variant&>(o);
  }
};

inline bool FrameSection::operator==(const FrameSection& o) const {
  return label == o.label && items == o.items;
}

// ── Decorations ───────────────────────────────────────────────────────────────────────────────

enum class DecorationKind { Note, Text, Box };

/// A view-level item that is not part of the model: a sticky note, a free text label or a box.
/// It lives in one view only and is placed after the layout, next to what it annotates.
///
/// `note "text" right of web`, `text "Checkout" at 0, -40`, `box "PCI zone" around web db`.
struct Decoration {
  DecorationKind kind = DecorationKind::Note;
  /// `note memo "…"`: an optional name, unique in the view, so edits and hosts can address it.
  std::optional<std::string> name;
  std::optional<std::string> text;
  /// `below|above|left of|right of <reference>`: placed beside an element of the view.
  std::optional<Placement> placement;
  std::optional<ElementId> reference;
  /// `box around a b …`: drawn around the boxes of these elements.
  std::vector<ElementId> around;
  /// `at x, y [size w, h]`: an absolute position (and size), in diagram coordinates.
  std::optional<Point2> at;
  std::optional<double> width;
  std::optional<double> height;
  std::optional<Style> style;

  bool operator==(const Decoration&) const = default;
};

// ── Views and the model ───────────────────────────────────────────────────────────────────────

/// `*` selects every element in the view's scope; anything else is an element id.
using Selector = std::string;

struct View {
  ViewId id;
  /// Diagram type, e.g. `flowchart` or `component`.
  std::optional<std::string> type;
  /// Element whose children the view shows; none for a view of the whole model.
  std::optional<ElementId> scope;
  std::vector<Selector> include{"*"};
  std::vector<Selector> exclude;
  LayoutOptions layout;
  std::vector<LayoutHint> hints;
  /// Steps of a sequence view; participants are the included elements.
  std::optional<std::vector<SequenceItem>> sequence;
  /// Number the messages of a sequence view.
  std::optional<bool> autonumber;
  /// `theme dark` in the view: overrides the document's theme for this view.
  std::optional<std::string> theme;
  /// Notes, free text and boxes drawn on this view only, in source order.
  std::vector<Decoration> decorations;

  bool operator==(const View&) const = default;
};

/// `use "aws-icons@^1" as aws`: a shape library the document uses.
struct LibraryUse {
  std::string name;
  /// A semver range, `*` when the spec has none.
  std::string range = "*";
  std::string alias;
  bool operator==(const LibraryUse&) const = default;
};

/// What a `style` block applies to.
enum class StyleTarget { Elements, Relationships };

/// A `style` block: a look shared by every element of a kind, every item with a tag, or every
/// relationship. Inline `style "…"` on an item overrides it.
struct StyleRule {
  StyleTarget target = StyleTarget::Elements;
  /// Element kind (`style container { … }`); none for `*` and for relationships.
  std::optional<std::string> kind;
  /// Only items with this tag (`style tag "external" { … }`).
  std::optional<std::string> tag;
  /// Same keys as inline `style "…"`.
  Style style;

  bool operator==(const StyleRule&) const = default;
};

/// `theme brand extends dark { … }`: a named set of colours a document declares (or a library
/// ships), drawn by picking it with `theme brand`.
struct ThemeDef {
  std::string name;
  /// The theme it starts from: a built-in, another declared theme or `alias.name`; `default`
  /// when absent.
  std::optional<std::string> base;
  /// Theme-wide keys: `background`, `edge`, `fontFamily`, … (see docs/dsl.md).
  Style values;
  /// `node person { fill … stroke … text … }`, in order; `default` and `external` are special.
  std::vector<std::pair<std::string, Style>> nodes;

  bool operator==(const ThemeDef&) const = default;
};

struct Model {
  OrderedMap<Element> elements;
  OrderedMap<Relationship> relationships;
  OrderedMap<Group> groups;
  OrderedMap<View> views;
  /// Libraries in `use` order; absent when the document uses none.
  std::optional<std::vector<LibraryUse>> libraries;
  /// `theme dark`: the built-in theme every view is drawn with; the default theme when absent.
  std::optional<std::string> theme;
  /// `style` blocks in document order.
  std::vector<StyleRule> styles;
  /// Themes the document declares, in order.
  std::vector<ThemeDef> themes;

  bool operator==(const Model&) const = default;
};

// ── Helpers ───────────────────────────────────────────────────────────────────────────────────

struct LibrarySpec {
  std::string name;
  std::string range;
};

/// `aws-icons@^1` → name and range; `@scope/name@1.2` keeps the scope.
LibrarySpec parse_library_spec(std::string_view spec);
/// The alias a `use` gets without `as`: the last name segment, as an identifier.
std::string default_library_alias(std::string_view name);
/// `name@range`, or just `name` for `*`.
std::string library_spec(const LibraryUse& use);

/// Ids of the element and all its descendants, parents first.
std::vector<ElementId> descendants_of(const Model& model, std::string_view id);
/// Whether `id` is `ancestor` or, by its dotted path, inside it (`shop.web` is inside `shop`).
bool is_within(std::string_view id, std::string_view ancestor);
/// The new id of the element `id` and of each of its descendants when `id` becomes `to`: the
/// renamed part of a dotted id is swapped, an id that does not start with the element's stays.
std::map<ElementId, ElementId, std::less<>> renamed_ids(const Model& model, std::string_view id,
                                                        std::string_view to);

/// The element with its defaults filled in (name = id).
Element make_element(ElementId id, std::string kind, std::optional<std::string> name = {},
                     std::optional<ElementId> parent = {});
Relationship make_relationship(RelationshipId id, Endpoint source, Endpoint target,
                               std::optional<std::string> label = {});
View make_view(ViewId id, std::optional<ElementId> scope = {});

/// Whether a style rule applies to an element or a relationship.
bool style_applies(const StyleRule& rule, const Element& element);
bool style_applies(const StyleRule& rule, const Relationship& relationship);
/// The style an item gets from the rules: `*` rules first, then kind rules, then tag rules
/// (later rules win within each step). Inline styles are not included. Empty when no rule
/// applies.
Style rule_style(const std::vector<StyleRule>& rules, const Element& element);
Style rule_style(const std::vector<StyleRule>& rules, const Relationship& relationship);

}  // namespace dd
