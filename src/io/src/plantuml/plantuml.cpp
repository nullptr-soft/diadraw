// Port of packages/import/src/plantuml/index.ts.

#include <algorithm>
#include <set>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io {

using js::Regex;
using namespace plantuml;

namespace {

bool unsupported_block(std::string_view kind) {
  static const std::set<std::string, std::less<>> kBlocks{
      "gantt", "json",  "yaml",  "salt",  "ditaa",      "dot",     "math", "latex", "ebnf",
      "regex", "chen",  "files", "creole", "board",     "chronology", "nwdiag", "wire", "project"};
  return kBlocks.count(kind) > 0;
}

}  // namespace

ImportResult import_plantuml(std::string_view source, const PlantUmlOptions& options) {
  ModelBuilder b;
  Counts stats{{"blocks", 0}, {"styling", 0}};
  Counts unknown_styles;
  std::vector<Block> blocks = split_blocks(strip_comments(source));
  static const Regex kTiming(R"re(^\s*(robust|concise|clock|binary|analog)\s+["\w])re");
  static const Regex kNwdiag(R"re(^\s*nwdiag\s*\{)re");

  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const Block& block = blocks[index];
    ++count(stats, "blocks");
    std::string prefix = blocks.size() > 1 ? std::to_string(index) + ":" : "";
    if (unsupported_block(block.kind)) {
      ++count(stats, "type:" + block.kind);
      b.drop("@start" + block.kind + " diagrams are not supported", block.line, "@start" + block.kind);
      continue;
    }
    PreprocessResult pre = preprocess(block.source, options);
    for (const auto& issue : pre.issues)
      b.drop(issue.message, issue.line, js::to_lower(split(issue.message, ':')[0]));
    if (std::find(pre.libraries.begin(), pre.libraries.end(), "C4") != pre.libraries.end())
      b.note("C4-PlantUML macros were read with the vendored C4-PlantUML signatures");
    std::vector<std::string> icon_libs;
    for (const auto& l : pre.libraries)
      if (l != "C4") icon_libs.push_back(l);
    if (!icon_libs.empty())
      b.note("Icon library macros (" + join(icon_libs, ", ") +
             ") become elements with shapes from the built-in plantuml-* libraries");

    // Timing and network diagrams start with @startuml too.
    std::optional<std::string> other;
    for (const auto& l : pre.lines)
      if (kTiming.test(l.text)) {
        other = "timing";
        break;
      }
    if (!other)
      for (const auto& l : pre.lines)
        if (kNwdiag.test(l.text)) {
          other = "nwdiag";
          break;
        }
    if (other) {
      ++count(stats, "type:" + *other);
      b.drop(*other + " diagrams are not supported", block.line, "@start" + *other);
      continue;
    }
    std::string type = block.kind == "mindmap" || block.kind == "wbs" ? block.kind : detect_type(pre.lines, pre.libraries);
    ++count(stats, "type:" + type);
    Ctx ctx{b, pre.lines, pre.libraries, prefix, type, stats, unknown_styles, Direction::Down, std::nullopt, {}, {}, std::nullopt, false, {}};
    if (type == "c4") parse_c4(ctx);
    else if (type == "activity") parse_activity(ctx);
    else if (type == "activity-legacy") parse_legacy_activity(ctx);
    else if (type == "sequence") parse_sequence(ctx);
    else if (type == "mindmap" || type == "wbs") parse_tree(ctx, type);
    else parse_structural(ctx, type);

    std::set<ElementId, std::less<>> hidden_ids;
    for (const auto& n : ctx.hidden)
      if (auto id = b.id(ctx.key(n)); id && !id->empty()) hidden_ids.insert(*id);
    std::vector<std::string> include;
    for (const auto& k : ctx.include) {
      if (std::find(include.begin(), include.end(), k) != include.end()) continue;
      auto id = b.id(k);
      if (id && hidden_ids.count(*id)) continue;
      include.push_back(k);
    }
    if (include.empty()) {
      b.note("Diagram " + std::to_string(index + 1) + " has nothing to show");
      continue;
    }
    std::string name = block.name ? *block.name : ctx.title ? *ctx.title : type;
    ViewInit view;
    view.algorithm = type == "sequence" ? "sequence" : "layered";
    view.direction = ctx.direction;
    view.include = include;
    for (const auto& h : ctx.hints) {
      if (const auto* pin = std::get_if<PinHint>(&h))
        if (!b.model().elements.contains(pin->element) || !b.model().elements.contains(pin->reference)) continue;
      view.hints.push_back(h);
    }
    view.sequence = ctx.sequence;
    view.autonumber = ctx.autonumber;
    b.add_view(name, std::move(view));
  }
  if (blocks.empty()) b.drop("No PlantUML diagram found");
  ImportResult result = b.result();
  result.report.stats = stats;
  result.report.unknown_styles = std::vector<std::pair<std::string, int>>();
  for (const auto& [k, v] : unknown_styles) result.report.unknown_styles->emplace_back(k, static_cast<int>(v));
  return result;
}

}  // namespace dd::io
