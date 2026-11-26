// Port of packages/import/src/plantuml/tree.ts: mindmaps and work breakdown structures.

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

void parse_tree(Ctx& ctx, const std::string& kind) {
  ModelBuilder& b = ctx.b;
  std::vector<std::optional<std::string>> parents;
  int count = 0;
  static const Regex kNode(R"re(^([*+-]+|#+)(?:\[#?[\w#]+\])?(_)?\s*(:)?(.*)$)re");
  static const Regex kArrowPrefix("^[<>]\\s*");
  static const Regex kSemicolonEnd(";\\s*$");
  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }
    const int line = lines[i].line;
    std::string t = trim(lines[i].text);
    auto m = kNode.exec(t);
    if (!m) {
      std::string word = js::to_lower(first_word(t));
      ctx.drop(line, word.empty() ? "line" : word, t);
      continue;
    }
    std::size_t depth = utf16_length(m->view(1));
    std::string text = kArrowPrefix.replace(m->view(4), "", false);
    if (m->has(3)) {
      auto ends_with_semicolon = [](std::string_view s) {
        std::string_view trimmed = js_trim(s);
        // trimEnd: only the end matters, and js_trim also trims the start, which keeps the last char.
        return !trimmed.empty() && trimmed.back() == ';';
      };
      while (!ends_with_semicolon(text) && i + 1 < lines.size()) text += "\n" + trim(lines[++i].text);
      text = kSemicolonEnd.replace(text, "", false);
    }
    std::string key = ctx.key("n" + std::to_string(++count));
    std::string label = plain_label(text);
    ElementInit init;
    init.kind = "process";
    init.name = label;
    std::string first = split(label, '\n')[0];
    init.local = first.empty() ? "node" : first;
    init.shape = m->has(2) ? "text" : "rounded";
    b.add_element(key, std::move(init));
    ctx.include.push_back(key);
    parents.resize(depth - 1);
    std::optional<std::string> parent = depth >= 2 && depth - 2 < parents.size() ? parents[depth - 2] : std::nullopt;
    if (parent) {
      RelationshipInit rel;
      rel.style = Style{{"endArrow", "none"}};
      b.add_relationship(*parent, key, std::move(rel));
    }
    parents.resize(depth);
    parents[depth - 1] = key;
  }
  ctx.direction = kind == "mindmap" ? Direction::Right : Direction::Down;
}

}  // namespace dd::io::plantuml
