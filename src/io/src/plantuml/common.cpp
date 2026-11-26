// Port of packages/import/src/plantuml/common.ts (and unsupportedConstruct from structural.ts).

#include <algorithm>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

std::string utf16_prefix(std::string_view s, std::size_t n) { return std::string(s.substr(0, byte_offset(s, n))); }

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == sep) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string first_word(std::string_view t) {
  for (std::size_t i = 0; i < t.size();) {
    std::size_t start = i;
    if (is_js_whitespace(next_code_point(t, i))) return std::string(t.substr(0, start));
  }
  return std::string(t);
}

void Ctx::drop(int line, const std::string& construct, const std::optional<std::string>& detail) {
  b.drop(detail ? construct + ": " + utf16_prefix(*detail, 80) : "Not imported: " + construct, line,
         type + ":" + construct);
}

void Ctx::style(int, const std::string& what) {
  ++count(stats, "styling");
  ++count(unknown_styles, what);
}

bool Ctx::has_library(std::string_view name) const {
  return std::find(libraries.begin(), libraries.end(), name) != libraries.end();
}

std::string unquote(std::string_view s) {
  std::string_view t = js_trim(s);
  if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
    return std::string(t.substr(1, t.size() - 2));
  return std::string(t);
}

std::string icon_shape(Ctx& ctx, std::string_view icon) {
  std::vector<std::string> libs;
  for (const auto& l : ctx.libraries)
    if (l != "C4") libs.push_back(l);
  if (libs.size() != 1) return "image";
  static const Regex kNonId("[^A-Za-z0-9_]", "g");
  static const Regex kLeadingDollar("^\\$");
  static const Regex kLeadingDigit("^(\\d)");
  std::string alias = ctx.b.use_library("plantuml-" + libs[0], "^1", kNonId.replace(libs[0], "_", true));
  std::string id = kLeadingDollar.replace(icon, "", false);
  id = kNonId.replace(id, "_", true);
  id = kLeadingDigit.replace(id, "_$1", false);
  return alias + "." + id;
}

std::string plain_label(std::string_view text) {
  static const Regex kNewline("\\\\n", "g");
  static const Regex kTags("<\\/?(b|i|u|s|color[^>]*|size[^>]*|font[^>]*|back[^>]*|strike|w[^>]*|plain)>", "gi");
  static const Regex kOpenIcon("<&[\\w-]+>", "g");
  static const Regex kSprite("<\\$[\\w-]+(\\{[^}]*\\})?>", "g");
  static const Regex kBold("\\*\\*(.+?)\\*\\*", "g");
  static const Regex kItalic("\\/\\/(.+?)\\/\\/", "g");
  static const Regex kUnderline("__(.+?)__", "g");
  static const Regex kStrike("~~(.+?)~~", "g");
  std::string s = kNewline.replace(text, "\n", true);
  s = kTags.replace(s, "", true);
  s = kOpenIcon.replace(s, "", true);
  s = kSprite.replace(s, "", true);
  s = kBold.replace(s, "$1", true);
  s = kItalic.replace(s, "$1", true);
  s = kUnderline.replace(s, "$1", true);
  s = kStrike.replace(s, "$1", true);
  return trim(s);
}

std::string strip_comments(std::string_view source) {
  // /\/'[\s\S]*?'\//g → only the line breaks of a block comment stay.
  std::string text;
  text.reserve(source.size());
  for (std::size_t i = 0; i < source.size();) {
    if (source.compare(i, 2, "/'") == 0) {
      std::size_t end = source.find("'/", i + 2);
      if (end != std::string_view::npos) {
        for (std::size_t k = i; k < end + 2; ++k)
          if (source[k] == '\n') text.push_back('\n');
        i = end + 2;
        continue;
      }
    }
    text.push_back(source[i++]);
  }
  static const Regex kLines("\\r?\\n");
  std::vector<std::string> lines = kLines.split(text);
  static const Regex kLineComment("^\\s*'");
  for (auto& l : lines)
    if (kLineComment.test(l)) l.clear();
  return join(lines, "\n");
}

std::vector<Block> split_blocks(std::string_view source) {
  static const Regex kLines("\\r?\\n");
  static const Regex kStart("^\\s*@start(\\w+)(?:\\s+(.+))?\\s*$");
  static const Regex kEnd("^\\s*@end(\\w+)");
  std::vector<std::string> lines = kLines.split(source);
  std::vector<Block> blocks;
  struct Current {
    std::string kind;
    std::optional<std::string> name;
    std::size_t start;
  };
  std::optional<Current> current;
  auto body = [&](std::size_t start, std::size_t end) {
    std::string out;
    for (std::size_t j = 0; j < lines.size(); ++j) {
      if (j) out += "\n";
      if (j > start && j < end) out += lines[j];
    }
    return out;
  };
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    auto start = kStart.exec(l);
    if (start && !current) {
      current = Current{js::to_lower(start->str(1)), std::nullopt, i};
      if (start->has(2) && !start->view(2).empty()) current->name = trim(start->view(2));
      continue;
    }
    auto end = kEnd.exec(l);
    if (end && current) {
      blocks.push_back({current->kind, current->name, body(current->start, i), static_cast<int>(current->start) + 1});
      current.reset();
    }
  }
  if (current)
    blocks.push_back(
        {current->kind, current->name, body(current->start, lines.size()), static_cast<int>(current->start) + 1});
  if (blocks.empty() && !js_trim(source).empty()) blocks.push_back({"uml", std::nullopt, std::string(source), 1});
  return blocks;
}

namespace {

int score(const std::vector<SourceLine>& lines, const Regex& re) {
  int n = 0;
  for (const auto& l : lines)
    if (re.test(l.text)) ++n;
  return n;
}

}  // namespace

std::string detect_type(const std::vector<SourceLine>& lines, const std::vector<std::string>& libraries) {
  static const Regex kC4Call(
      R"re(^\s*(Person|System|Container|Component|Rel|BiRel|Lay|Boundary|System_Boundary|Container_Boundary|Enterprise_Boundary|Deployment_Node|Node|SystemDb|SystemQueue|ContainerDb|ContainerQueue|ComponentDb|ComponentQueue)(_\w+)?\s*\()re");
  static const Regex kActivity(
      R"re(^\s*(start|stop|kill|detach|endif|endwhile|end fork|end split|endswitch)\s*$|^\s*:[^:]*[;|<>\]}/]\s*$|^\s*(if\s*\(.*\)\s*(then|is)|while\s*\(.*\)|repeat\b|fork\s*$|partition\b|split\s*$|switch\s*\()|^\s*\|[^|]+\|\s*$)re");
  static const Regex kLegacy(R"re(\(\*(top)?\))re");
  static const Regex kSequenceOnly(
      R"re(^\s*(participant|activate|deactivate|autonumber|alt\b|opt\b|loop\b|ref\s+over|note\s+over|return\b)|^\s*[\w"]+\s*-+>>?\s*[\w"]+\s*:)re");
  static const Regex kState(
      R"re(^\s*state\s+|\[\*\]|\[H\*?\]|<<(fork|join|choice|end|history|history\*|entryPoint|exitPoint|inputPin|outputPin|expansionInput|expansionOutput)>>)re");
  static const Regex kClass(
      R"re(^\s*(abstract(\s+class)?|class|interface|enum|annotation|entity|struct|protocol|exception|metaclass|stereotype|record|dataclass|circle|diamond)\s+["\w]|<\|--|--\|>|<\|\.\.|\.\.\|>|\*--|--\*|o--|--o|\}\|--|\|\{)re");
  static const Regex kObject(R"re(^\s*(object|map)\s+["\w])re");
  static const Regex kUsecase(R"re(^\s*usecase\s+|\(\s*[^)*]+\s*\)\s*(as\s+\w+)?\s*$|^\s*\([^)*]+\)\s*(-|\.))re");
  static const Regex kDeployment(R"re(^\s*(node|cloud|artifact|storage|stack|frame|card)\s+["\w])re");
  static const Regex kComponent(
      R"re(^\s*(component|database|folder|package|rectangle|queue|file|agent|collections|hexagon|interface|port|portin|portout|label|person)\s+["\w[]|^\s*\[[^\]]+\]|^\s*\(\)\s*)re");
  static const Regex kSequence(
      R"re(^\s*(participant|actor|boundary|control|entity|database|collections|queue)\s+["\w].*?(as\s+\w+)?(\s+order\s+\d+)?(\s*#\w+)?\s*$|^\s*(activate|deactivate|destroy|create|return|autonumber|alt|else|opt|loop|par|par2|break|critical|group|ref\s+over|note\s+over|end\s*$|==.*==|\.\.\.|\|\|\|))re");

  if (std::find(libraries.begin(), libraries.end(), "C4") != libraries.end() || score(lines, kC4Call) > 0)
    return "c4";
  int activity = score(lines, kActivity);
  int legacy = score(lines, kLegacy);
  if (legacy > 0 && activity <= legacy) return "activity-legacy";
  int sequence_only = score(lines, kSequenceOnly);
  if (activity > 0 && activity >= sequence_only) return "activity";
  std::vector<std::pair<std::string, int>> scores{{"state", score(lines, kState)},
                                                  {"class", score(lines, kClass)},
                                                  {"object", score(lines, kObject)},
                                                  {"usecase", score(lines, kUsecase)},
                                                  {"deployment", score(lines, kDeployment)},
                                                  {"component", score(lines, kComponent)}};
  int seq = score(lines, kSequence);
  // Sequence keywords like `actor` and `database` also appear elsewhere: they only win alone.
  std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return b.second < a.second; });
  const auto& best = scores.front();
  if (best.second > 0 && best.second >= seq / 2.0) return best.first;
  return "sequence";
}

const std::vector<std::pair<std::string, KeywordSpec>>& element_keywords() {
  static const std::vector<std::pair<std::string, KeywordSpec>> kKeywords{
      {"actor", {"person", "actor"}},       {"person", {"person", std::nullopt}},
      {"agent", {"component", std::nullopt}}, {"artifact", {"component", "note"}},
      {"boundary", {"component", "circle"}}, {"card", {"component", "card"}},
      {"circle", {"component", "circle"}},  {"cloud", {"system", "cloud"}},
      {"collections", {"component", std::nullopt}}, {"component", {"component", std::nullopt}},
      {"control", {"component", "circle"}}, {"database", {"database", std::nullopt}},
      {"entity", {"component", "circle"}},  {"file", {"component", "note"}},
      {"folder", {"system", "folder"}},     {"frame", {"system", std::nullopt}},
      {"hexagon", {"component", "hexagon"}}, {"interface", {"component", "interface"}},
      {"label", {"note", "text"}},          {"node", {"system", "cube"}},
      {"package", {"system", "folder"}},    {"queue", {"container", "queue"}},
      {"rectangle", {"system", std::nullopt}}, {"stack", {"component", std::nullopt}},
      {"storage", {"database", std::nullopt}}, {"usecase", {"process", "ellipse"}}};
  return kKeywords;
}

const KeywordSpec* element_keyword(std::string_view keyword) {
  for (const auto& [k, spec] : element_keywords())
    if (k == keyword) return &spec;
  return nullptr;
}

std::size_t common(Ctx& ctx, const std::vector<SourceLine>& lines, std::size_t i) {
  const std::string& text = lines[i].text;
  const int line = lines[i].line;
  std::string t = trim(text);
  if (t.empty()) return 1;
  auto until = [&](const Regex& end) -> std::size_t {
    std::size_t j = i;
    while (j < lines.size() && !end.test(trim(lines[j].text))) ++j;
    return j - i + 1;
  };
  static const Regex kLeftRight("^LAYOUT_(LEFT_RIGHT|LANDSCAPE)(\\(\\))?$", "i");
  static const Regex kTopDown("^LAYOUT_TOP_DOWN(\\(\\))?$", "i");
  static const Regex kTitle("^title\\s+(.+)$", "i");
  static const Regex kTitleBlock("^title\\s*$", "i");
  static const Regex kEndTitle("^end\\s*title$", "i");
  static const Regex kHeaderEtc("^(header|footer|legend|caption)\\b", "i");
  static const Regex kHeaderBlock("^(legend|header|footer)\\b\\s*(left|right|center|top|bottom)*\\s*$", "i");
  static const Regex kSprite("^sprite\\s+\\$?[\\w-]+", "i");
  static const Regex kBraceEnd("\\}\\s*$");
  static const Regex kSkinparam("^skinparam\\b", "i");
  static const Regex kCloseBrace("^\\}$");
  static const Regex kStyle("^<style>", "i");
  static const Regex kStyleEnd("<\\/style>", "i");
  static const Regex kLeftToRight("^left\\s+to\\s+right\\s+direction$", "i");
  static const Regex kTopToBottom("^top\\s+to\\s+bottom\\s+direction$", "i");
  static const Regex kHide("^(hide|remove)\\s+(.+)$", "i");
  static const Regex kHideTarget("^[\\w.\"]+$");
  static const Regex kHideStyling(
      "^(empty|circle|stereotype|members|methods|fields|attributes|footbox|unlinked|@unlinked|<<.*>>)$", "i");
  static const Regex kMisc(
      "^(show|scale|allowmixing|allow_mixing|newpage|mainframe|set\\s+namespaceSeparator|set\\s+separator|hide\\s*$|"
      "page\\s|footbox|!pragma)",
      "i");

  if (kLeftRight.test(t)) {
    ctx.direction = Direction::Right;
    return 1;
  }
  if (kTopDown.test(t)) {
    ctx.direction = Direction::Down;
    return 1;
  }
  if (auto m = kTitle.exec(t)) {
    ctx.title = plain_label(unquote(m->view(1)));
    return 1;
  }
  if (kTitleBlock.test(t)) {
    std::size_t n = until(kEndTitle);
    std::vector<std::string> parts;
    for (std::size_t k = i + 1; k < i + n - 1 && k < lines.size(); ++k) parts.push_back(trim(lines[k].text));
    ctx.title = plain_label(join(parts, " "));
    return n;
  }
  if (kHeaderEtc.test(t)) {
    std::string word = js::to_lower(first_word(t));
    if (kHeaderBlock.test(t)) {
      Regex end("^end\\s*" + word + "$", "i");
      std::size_t n = until(end);
      ctx.drop(line, word);
      return n;
    }
    ctx.drop(line, word);
    return 1;
  }
  if (kSprite.test(t)) {
    std::size_t n = t.find('{') != std::string::npos && t.find('}') == std::string::npos ? until(kBraceEnd) : 1;
    ctx.style(line, "sprite");
    return n;
  }
  if (kSkinparam.test(t)) {
    if (!t.empty() && t.back() == '{') {
      std::size_t n = until(kCloseBrace);
      ctx.style(line, "skinparam");
      return n;
    }
    ctx.style(line, "skinparam");
    return 1;
  }
  if (kStyle.test(t)) {
    std::size_t n = t.find("</style>") != std::string::npos ? 1 : until(kStyleEnd);
    ctx.style(line, "style block");
    return n;
  }
  if (kLeftToRight.test(t)) {
    ctx.direction = Direction::Right;
    return 1;
  }
  if (kTopToBottom.test(t)) {
    ctx.direction = Direction::Down;
    return 1;
  }
  if (auto m = kHide.exec(t)) {
    std::string target = trim(m->view(2));
    // `hide Foo` hides an element; `hide empty members`, `hide circle`, … are styling.
    if (kHideTarget.test(target) && !kHideStyling.test(target)) ctx.hidden.insert(unquote(target));
    else ctx.style(line, js::to_lower(m->view(1)) + " " + first_word(target));
    return 1;
  }
  if (kMisc.test(t)) {
    ctx.style(line, js::to_lower(first_word(t)));
    return 1;
  }
  return 0;
}

const Regex& arrow_regex() {
  static const std::string kHeads =
      R"re(<\|?|\*|o|#|x|\}|\^|\+|0\)?|\(0?|\|\||\|\{|\}\||\}o|o\{|\|o|o\||>|\|>|\\\\|//|<<|>>)re";
  static const Regex kArrow("((?:" + kHeads +
                            R"re()?)([-.=~]+)(?:\[([^\]]*)\])?(up|down|left|right|u|d|l|r|le|ri|do)?(?:\[([^\]]*)\])?([-.=~]*)((?:)re" +
                            kHeads + ")?)");
  return kArrow;
}

Arrow parse_arrow(const js::Match& m) {
  static const Regex kDashed("dashed|dotted");
  static const Regex kHidden("hidden");
  Arrow a;
  std::string body = m.str(2) + m.str(6);
  std::string options = m.str(3) + "," + m.str(5);
  if (m.has(4)) {
    std::string dir = js::to_lower(m.view(4));
    a.direction = dir[0] == 'u' ? "up" : dir[0] == 'd' ? "down" : dir[0] == 'l' ? "left" : "right";
  }
  a.dashed = body.find('.') != std::string::npos || kDashed.test(options);
  a.left = m.str(1);
  a.right = m.str(7);
  a.hidden = kHidden.test(options);
  return a;
}

std::optional<HeadStyle> head_style(std::string_view head) {
  if (head.empty()) return std::nullopt;
  if (head == ">" || head == "<" || head == ">>" || head == "<<") return HeadStyle{"open", std::nullopt};
  if (head == "|>" || head == "<|") return HeadStyle{"block", "0"};
  if (head == "*") return HeadStyle{"diamond", std::nullopt};
  if (head == "o") return HeadStyle{"diamond", "0"};
  if (head == "#" || head == "x" || head == "+" || head == "^") return HeadStyle{"oval", "0"};
  return HeadStyle{"open", std::nullopt};
}

std::optional<LayoutHint> direction_hint(const ElementId& from, const ElementId& to,
                                         const std::optional<std::string>& direction) {
  if (!direction) return std::nullopt;
  Placement placement = *direction == "down"   ? Placement::Below
                        : *direction == "up"   ? Placement::Above
                        : *direction == "left" ? Placement::LeftOf
                                               : Placement::RightOf;
  return LayoutHint{PinHint{to, placement, from}};
}

std::string unsupported_construct(std::string_view t) {
  static const Regex kWord("^[!@]?[A-Za-z_][\\w]*");
  static const Regex kArrowish("->|-->|\\.\\.>");
  if (auto m = kWord.exec(t)) return js::to_lower(m->view(0));
  if (kArrowish.test(t)) return "arrow";
  return utf16_prefix(t, 12);
}

}  // namespace dd::io::plantuml
