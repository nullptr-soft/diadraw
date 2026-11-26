#pragma once

// DiaDraw source → model, a port of packages/dsl/src/lower.ts.
//
// Offsets in spans are UTF-8 byte offsets. Diagnostic columns count UTF-16 code units, as the
// TypeScript implementation and editors such as CodeMirror and VS Code do.

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/dsl/syntax.hpp"
#include "diadraw/model/operations.hpp"

namespace dd::dsl {

struct Span {
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  bool operator==(const Span&) const = default;
};

enum class Severity { Error, Warning };

struct Diagnostic {
  Span span;
  Severity severity = Severity::Error;
  std::string message;
  /// 1-based line and column of `span.from`; the column counts UTF-16 code units.
  int line = 1;
  int column = 1;
};

/// One `tech`, `shape`, `tag`, `description`, `style` or `prop` clause of a declaration.
/// (Relationships ignore `shape` and `description`, with a warning, but their clauses are kept.)
struct ClauseSource {
  enum class Kind { Technology, Shape, Tag, Description, Style, Prop };
  Kind kind = Kind::Technology;
  /// The whole clause, keyword included.
  Span span;
  /// The tag (`tag "x"`) or the property key (`prop "k" "v"`); empty for the others.
  std::string key;
};

struct ElementSource {
  Span decl;
  Span name;
  Span kind;
  std::optional<Span> label;
  /// The `{ … }` body, when the declaration has one.
  std::optional<Span> body;
  /// The clauses after the label, in source order (complete ones only).
  std::vector<ClauseSource> clauses;
};

struct RelationshipSource {
  Span decl;
  /// The target reference; a new label goes after it.
  Span target;
  std::optional<Span> label;
  /// The explicit id in `name = a -> b`, when there is one.
  std::optional<Span> id;
  /// The source reference; a new name goes before it.
  Span source;
  /// `->`, `-->` or `<->`.
  Span arrow;
  /// The clauses after the label, in source order (complete ones only).
  std::vector<ClauseSource> clauses;
  /// The element whose body declares the relationship (its id split at the dots; empty in a
  /// `model` block): references in the declaration resolve from here outwards.
  std::vector<std::string> scope;
};

struct GroupSource {
  Span decl;
  Span body;
};

struct SelectorSource {
  bool include = true;  ///< `include` or `exclude`
  /// The element id, or `*`.
  std::string selector;
  Span span;
  /// The whole `include …` / `exclude …` statement.
  Span statement;
};

struct ViewSource {
  Span decl;
  /// The view's id after `view`.
  Span name;
  Span body;
  std::optional<Span> layout;
  /// One span per hint, in the order of `view.hints`.
  std::vector<Span> hints;
  /// One span per decoration, in the order of `view.decorations`.
  std::vector<Span> decorations;
  std::vector<SelectorSource> selectors;
  /// The view's `theme …` statement.
  std::optional<Span> theme;
};

/// One `key value` line of a `style` block, a theme or a theme's `node` block.
struct EntrySource {
  std::string key;
  /// The whole entry, key and value.
  Span span;
};

/// A `style` block, in the order of `model.styles`.
struct StyleSource {
  Span decl;
  /// `container tag "legacy"`, between `style` and the body.
  Span selector;
  Span body;
  /// Its entries in source order (complete ones only).
  std::vector<EntrySource> entries;
};

/// A `node kind { … }` block of a theme declaration.
struct ThemeNodeSource {
  std::string kind;
  Span decl;
  Span body;
  std::vector<EntrySource> entries;
};

/// A `theme name { … }` declaration, in the order of `model.themes`.
struct ThemeSource {
  Span decl;
  Span name;
  /// `extends base`, when written.
  std::optional<Span> base;
  Span body;
  /// The theme-wide `key value` entries.
  std::vector<EntrySource> entries;
  /// Its `node kind { … }` blocks in source order (several may name one kind).
  std::vector<ThemeNodeSource> nodes;
};

/// A reference to an element (or one of its ports) as the text writes it and the lowering resolved
/// it: a relationship's end, a view's `of`, a selector, a hint, a sequence step or a decoration.
struct ReferenceSource {
  /// Its parts, one span per dotted part in order (`shop` and `web` in `shop.web`), are
  /// SourceMap::reference_parts[first, first + count).
  std::uint32_t first = 0;
  std::uint32_t count = 0;
  /// What it names.
  Endpoint target;

  /// How many parts the target's whole path has: the element's dotted parts, and its port.
  [[nodiscard]] std::size_t path_length() const {
    return static_cast<std::size_t>(std::count(target.element.begin(), target.element.end(), '.')) + 1 +
           (target.port ? 1 : 0);
  }
  /// Where written part `j` sits in the target's whole path: a reference writes the path's tail,
  /// so part j is path part j + (path_length - count). Callers check count <= path_length().
  [[nodiscard]] std::size_t path_index(std::size_t j) const { return j + path_length() - count; }
};

/// Where each model item came from in the text. Canvas edits use it to patch the source.
struct SourceMap {
  /// Body of the first `model { … }` block.
  std::optional<Span> model;
  std::map<ElementId, ElementSource, std::less<>> elements;
  std::map<RelationshipId, RelationshipSource, std::less<>> relationships;
  std::map<GroupId, GroupSource, std::less<>> groups;
  std::map<ViewId, ViewSource, std::less<>> views;
  /// `use` statements by alias.
  std::map<std::string, Span, std::less<>> libraries;
  /// The `theme …` statement that sets `model.theme` (the last one).
  std::optional<Span> theme;
  /// Every top-level `use`, `theme`, `style`, `model` and `view` statement, in source order: where
  /// new top-level statements go.
  std::vector<Span> statements;
  std::vector<StyleSource> styles;
  std::vector<ThemeSource> themes;
  /// Every reference that resolved, in the order the lowering resolved them (not source order).
  std::vector<ReferenceSource> references;
  /// The parts of every reference, one after another (kept flat: one allocation for them all).
  std::vector<Span> reference_parts;

  /// A reference's parts.
  [[nodiscard]] std::span<const Span> parts(const ReferenceSource& ref) const {
    return std::span<const Span>(reference_parts).subspan(ref.first, ref.count);
  }
};

struct ParseResult {
  Model model;
  /// Sorted by position.
  std::vector<Diagnostic> diagnostics;
  SourceMap source_map;
  /// The operations that build `model` from an empty model, in order.
  std::vector<Operation> operations;
  Tree tree;
};

/// Parses DiaDraw source into a model. Never fails: problems become diagnostics.
ParseResult parse(std::string_view source);

// ── Text helpers shared with print and patch ─────────────────────────────────────────────────

/// The contents of a string literal: quotes removed, `\n` a newline, `\x` → `x`. A block string
/// (`"""` … `"""`, see docs/dsl.md) also loses a blank first and last line and the indentation
/// its lines share.
std::string unquote(std::string_view literal);
/// A string literal for `text`, escaping `\`, `"` and newlines.
std::string quote(std::string_view text);
/// A block string for `text` when it spans lines: `"""`, its lines one step deeper than `indent`,
/// then `"""` on a line of its own at `indent`. Text a block cannot hold as it is gets
/// `quote(text)` instead: text in which every line starts with blanks (they would read back as
/// indentation), a line of only blanks, `"""` or a carriage return.
std::string quote_lines(std::string_view text, std::string_view indent);
/// Parses `key=value;key=value` (draw.io style); empty values are kept.
Style parse_style_text(std::string_view text);
std::string print_style_text(const Style& style);
/// A `style` block value as written: bare when it is a number or a word, quoted otherwise.
std::string print_style_value(std::string_view value);
/// The id a relationship gets from its endpoints; `index` disambiguates parallel edges.
std::string relationship_id(const Endpoint& source, const Endpoint& target, int index = 1);

}  // namespace dd::dsl
