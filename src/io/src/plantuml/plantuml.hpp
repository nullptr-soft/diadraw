#pragma once

// Internals of the PlantUML importer (a port of packages/import/src/plantuml): the preprocessor,
// the context the diagram parsers share, and the shared statement and arrow parsing.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/io/builder.hpp"
#include "diadraw/io/plantuml.hpp"

namespace dd::io::plantuml {

struct SourceLine {
  std::string text;
  /// 1-based line in the source file (for included or generated text, the line that caused it).
  int line = 0;
};

struct PreprocessIssue {
  int line = 0;
  std::string message;
};

struct PreprocessResult {
  std::vector<SourceLine> lines;
  std::vector<PreprocessIssue> issues;
  /// Stdlib libraries in use, in the order first seen: `C4` and icon sets such as `aws`.
  std::vector<std::string> libraries;
  std::vector<std::string> unresolved;
};

PreprocessResult preprocess(std::string_view source, const PlantUmlOptions& options);

/// Splits call arguments on top-level commas, keeping quoted strings and nested calls whole.
std::vector<std::string> split_args(std::string_view text);

using Counts = std::vector<std::pair<std::string, double>>;
inline double& count(Counts& counts, const std::string& key) {
  for (auto& [k, v] : counts)
    if (k == key) return v;
  return counts.emplace_back(key, 0).second;
}

/// What a diagram parser works with.
struct Ctx {
  ModelBuilder& b;
  const std::vector<SourceLine>& lines;
  const std::vector<std::string>& libraries;
  std::string prefix;
  /// The diagram type, for the constructs in the report.
  std::string type;
  Counts& stats;
  Counts& unknown_styles;
  Direction direction = Direction::Down;
  std::optional<std::string> title;
  std::vector<LayoutHint> hints;
  /// Elements to show, in order (sequence participants), when the diagram lists them.
  std::vector<std::string> include;
  std::optional<std::vector<SequenceItem>> sequence;
  bool autonumber = false;
  /// Elements hidden with `hide`/`remove`.
  std::set<std::string, std::less<>> hidden;

  /// Builder key for a diagram-local name (diagrams in one file do not share elements).
  [[nodiscard]] std::string key(std::string_view name) const { return prefix + std::string(name); }
  /// Reports a construct that was not imported.
  void drop(int line, const std::string& construct, const std::optional<std::string>& detail = std::nullopt);
  /// Counts an element created implicitly (named by a relationship but never declared).
  void implicit() { ++count(stats, "implicit"); }
  /// Counts a styling statement kept only as passthrough.
  void style(int line, const std::string& what);
  [[nodiscard]] bool has_library(std::string_view name) const;
};

// ── common.ts ──────────────────────────────────────────────────────────────────────────────────

std::string unquote(std::string_view s);
/// PlantUML text escapes and creole markup to plain text.
std::string plain_label(std::string_view text);
/// The shape for an icon-library macro (`Lambda(…)`).
std::string icon_shape(Ctx& ctx, std::string_view icon);
/// Strips `'` line comments and `/' … '/` block comments.
std::string strip_comments(std::string_view source);

struct Block {
  /// `uml`, `mindmap`, `wbs`, `gantt`, `json`, …
  std::string kind;
  std::optional<std::string> name;
  /// Lines between the markers, with blank lines in place of the markers (line numbers hold).
  std::string source;
  int line = 1;
};
std::vector<Block> split_blocks(std::string_view source);

/// Guesses the diagram type of a `@startuml` block, as PlantUML does from its content.
std::string detect_type(const std::vector<SourceLine>& lines, const std::vector<std::string>& libraries);

struct KeywordSpec {
  std::string kind;
  std::optional<std::string> shape;
};
/// Element kinds for PlantUML element keywords (component, deployment and use case diagrams).
const std::vector<std::pair<std::string, KeywordSpec>>& element_keywords();
const KeywordSpec* element_keyword(std::string_view keyword);

/// Statements every diagram type shares: how many lines were consumed (0 when not one).
std::size_t common(Ctx& ctx, const std::vector<SourceLine>& lines, std::size_t i);

struct Arrow {
  bool dashed = false;
  /// Head at the left end (`<`, `<|`, `*`, `o`, …) and at the right end.
  std::string left;
  std::string right;
  /// Direction word inside the arrow (`-up->`, `-l->`): `up`, `down`, `left` or `right`.
  std::optional<std::string> direction;
  bool hidden = false;
};
/// An arrow like `<|--`, `*-->`, `..>`, `-up->`, `-[#red,dashed]->`, `--(`.
const js::Regex& arrow_regex();
Arrow parse_arrow(const js::Match& m);

struct HeadStyle {
  std::string arrow;
  std::optional<std::string> fill;
};
std::optional<HeadStyle> head_style(std::string_view head);

/// The pin hint an arrow direction asks for (`a -down-> b` puts b below a).
std::optional<LayoutHint> direction_hint(const ElementId& from, const ElementId& to,
                                         const std::optional<std::string>& direction);

/// A short name for an unsupported line, for the report's counts.
std::string unsupported_construct(std::string_view t);

/// `s.trim()` as a string.
inline std::string trim(std::string_view s) { return std::string(js_trim(s)); }
/// `t.split(/\s/)[0]`.
std::string first_word(std::string_view t);
/// `s.slice(0, n)` in UTF-16 code units.
std::string utf16_prefix(std::string_view s, std::size_t n);
/// `s.split(',')`, untrimmed.
std::vector<std::string> split(std::string_view s, char sep);
std::string join(const std::vector<std::string>& parts, std::string_view sep);

// ── The diagram parsers ────────────────────────────────────────────────────────────────────────

void parse_structural(Ctx& ctx, const std::string& type);
void parse_sequence(Ctx& ctx);
void parse_activity(Ctx& ctx);
void parse_legacy_activity(Ctx& ctx);
void parse_c4(Ctx& ctx);
void parse_tree(Ctx& ctx, const std::string& kind);

}  // namespace dd::io::plantuml
