#include "diadraw/dsl/highlight.hpp"

#include <algorithm>
#include <string>

#include "diadraw/dsl/syntax.hpp"

namespace dd::dsl {

namespace {

constexpr std::string_view kKeywords[] = {
    "use",     "as",          "model",       "view",       "of",       "group",      "port",   "tech",
    "shape",   "tag",         "description", "include",    "exclude",  "layout",     "direction",
    "spacing", "pin",         "at",          "below",      "above",    "left",       "right",  "up",
    "down",    "align",       "horizontally", "vertically", "order",   "north",      "south",  "east",
    "west",    "style",       "prop",        "size",       "route",    "via",        "from",   "to",
    "sequence", "activate",   "deactivate",  "over",       "divider",  "delay",      "ref",
    "autonumber", "alt",      "opt",         "loop",       "par",      "critical",   "break",  "else",
    "note",    "theme",       "relationship", "extends",
    "text",    "box",         "around",      "rotate",
};

bool keyword(std::string_view w) {
  for (auto k : kKeywords)
    if (k == w) return true;
  return false;
}

bool digit(char c) { return c >= '0' && c <= '9'; }
bool word_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool word_char(char c) { return word_start(c) || digit(c); }

/// The length of a whitespace character at `i` (JS `\s` within a line), or 0.
std::size_t space_at(std::string_view s, std::size_t i) {
  unsigned char c = s[i];
  if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r') return 1;
  if (c < 0x80) return 0;
  // The non-ASCII characters of JS `\s`: U+00A0, U+1680, U+2000–200A, U+2028/9, U+202F, U+205F,
  // U+3000, U+FEFF.
  auto at = [&](std::size_t k) { return i + k < s.size() ? static_cast<unsigned char>(s[i + k]) : 0u; };
  if (c == 0xC2 && at(1) == 0xA0) return 2;
  if (c == 0xE1 && at(1) == 0x9A && at(2) == 0x80) return 3;
  if (c == 0xE2 && at(1) == 0x80 && ((at(2) >= 0x80 && at(2) <= 0x8A) || at(2) == 0xA8 || at(2) == 0xA9 || at(2) == 0xAF))
    return 3;
  if (c == 0xE2 && at(1) == 0x81 && at(2) == 0x9F) return 3;
  if (c == 0xE3 && at(1) == 0x80 && at(2) == 0x80) return 3;
  if (c == 0xEF && at(1) == 0xBB && at(2) == 0xBF) return 3;
  return 0;
}

/// The byte length of the UTF-8 sequence starting at `i`.
std::size_t char_len(std::string_view s, std::size_t i) {
  unsigned char c = s[i];
  std::size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  return std::min(n, s.size() - i);
}

}  // namespace

std::string_view token_kind_name(TokenKind kind) {
  switch (kind) {
    case TokenKind::Comment: return "comment";
    case TokenKind::String: return "string";
    case TokenKind::Number: return "number";
    case TokenKind::Operator: return "operator";
    case TokenKind::Bracket: return "bracket";
    case TokenKind::Keyword: return "keyword";
    case TokenKind::Kind: return "typeName";
    case TokenKind::Definition: return "definition";
    case TokenKind::Name: return "variableName";
  }
  return "";
}

std::vector<Token> highlight(std::string_view text) {
  std::vector<Token> out;
  auto emit = [&](std::size_t from, std::size_t to, TokenKind kind) {
    if (to <= from) return;
    // Adjacent tokens of one kind are one run, as an editor paints them.
    if (!out.empty() && out.back().kind == kind && out.back().to == from) out.back().to = static_cast<std::uint32_t>(to);
    else out.push_back({static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to), kind});
  };
  bool in_comment = false;
  bool in_block_string = false;  // inside `"""` … `"""`
  bool expect_kind = false;
  std::size_t line_start = 0;
  // Like CodeMirror's stream parsers, each line is tokenized on its own; state carries over.
  // Lines end at \n, \r\n or \r, as in CodeMirror.
  while (line_start <= text.size()) {
    std::size_t line_end = text.find_first_of("\r\n", line_start);
    if (line_end == std::string_view::npos) line_end = text.size();
    std::size_t next = line_end + (text.substr(line_end, 2) == "\r\n" ? 2 : 1);
    std::string_view line = text.substr(line_start, line_end - line_start);
    std::size_t i = 0;
    auto at = [&](std::size_t k) { return k < line.size() ? line[k] : '\0'; };
    auto starts = [&](std::size_t k, std::string_view s) { return line.substr(k).starts_with(s); };
    // The rest of a block string: up to its closing `"""`, or the whole line.
    auto block_string = [&](std::size_t from) {
      std::size_t k = block_string_close(line, from);
      if (k == std::string_view::npos) k = line.size();
      else in_block_string = false;
      emit(line_start + from, line_start + k, TokenKind::String);
      return k;
    };
    if (in_block_string) i = block_string(0);
    while (i < line.size()) {
      std::size_t start = i;
      if (in_comment) {
        std::size_t close = line.find("*/", i);
        if (close == std::string_view::npos) {
          i = line.size();
        } else {
          i = close + 2;
          in_comment = false;
        }
        emit(line_start + start, line_start + i, TokenKind::Comment);
        continue;
      }
      if (std::size_t n = space_at(line, i)) {
        while (i < line.size() && (n = space_at(line, i))) i += n;
        continue;
      }
      if (starts(i, "//")) {
        emit(line_start + i, line_start + line.size(), TokenKind::Comment);
        i = line.size();
        continue;
      }
      if (starts(i, "/*")) {
        in_comment = true;
        i += 2;
        emit(line_start + start, line_start + i, TokenKind::Comment);
        continue;
      }
      if (starts(i, kBlockQuote)) {
        in_block_string = true;
        emit(line_start + i, line_start + i + kBlockQuote.size(), TokenKind::String);
        i = block_string(i + kBlockQuote.size());
        continue;
      }
      if (at(i) == '"') {
        ++i;
        while (i < line.size()) {
          if (line[i] == '"') break;
          if (line[i] == '\\') {
            if (i + 1 >= line.size()) break;
            i += 1 + char_len(line, i + 1);
            continue;
          }
          i += char_len(line, i);
        }
        if (at(i) == '"') ++i;
        emit(line_start + start, line_start + i, TokenKind::String);
        continue;
      }
      {
        std::size_t k = i + (at(i) == '-' ? 1 : 0);
        if (digit(at(k))) {
          while (digit(at(k))) ++k;
          if (at(k) == '.' && digit(at(k + 1))) {
            ++k;
            while (digit(at(k))) ++k;
          }
          i = k;
          emit(line_start + start, line_start + i, TokenKind::Number);
          continue;
        }
      }
      if (starts(i, "-->") || starts(i, "->>") || starts(i, "<->")) {
        i += 3;
        emit(line_start + start, line_start + i, TokenKind::Operator);
        continue;
      }
      if (starts(i, "->")) {
        i += 2;
        emit(line_start + start, line_start + i, TokenKind::Operator);
        continue;
      }
      if (at(i) == '{' || at(i) == '}') {
        ++i;
        emit(line_start + start, line_start + i, TokenKind::Bracket);
        continue;
      }
      if (at(i) == '=') {
        ++i;
        expect_kind = true;
        emit(line_start + start, line_start + i, TokenKind::Operator);
        continue;
      }
      if (word_start(at(i))) {
        while (word_char(at(i))) ++i;
        TokenKind kind;
        if (expect_kind) {
          expect_kind = false;
          // In `name = a.b -> c` the word after `=` is an endpoint, not a kind.
          std::size_t k = i;
          while (at(k) == '.' && word_start(at(k + 1))) {
            ++k;
            while (word_char(at(k))) ++k;
          }
          while (k < line.size())
            if (std::size_t n = space_at(line, k)) k += n;
            else break;
          if (starts(k, "->") || starts(k, "-->") || starts(k, "<->")) {
            kind = TokenKind::Name;
          } else {
            kind = TokenKind::Kind;
            // A library's kind, `alias.kind`, is one name.
            if (at(i) == '.' && word_start(at(i + 1))) {
              ++i;
              while (word_char(at(i))) ++i;
            }
          }
        } else {
          std::size_t k = i;
          while (k < line.size())
            if (std::size_t n = space_at(line, k)) k += n;
            else break;
          if (at(k) == '=' && at(k + 1) != '=') kind = TokenKind::Definition;
          else kind = keyword(line.substr(start, i - start)) ? TokenKind::Keyword : TokenKind::Name;
        }
        emit(line_start + start, line_start + i, kind);
        continue;
      }
      i += char_len(line, i);
    }
    line_start = next;
  }
  return out;
}

std::vector<FoldRange> fold_ranges(std::string_view text) {
  // Where each line starts and where its text ends (at its line break), so a brace's line is a
  // binary search away. Lines end at \n, \r\n or \r, as in `highlight`.
  std::vector<std::uint32_t> starts{0}, ends;
  for (std::size_t i = text.find_first_of("\r\n"); i != std::string_view::npos; i = text.find_first_of("\r\n", i)) {
    ends.push_back(static_cast<std::uint32_t>(i));
    i += text.substr(i, 2) == "\r\n" ? 2 : 1;
    starts.push_back(static_cast<std::uint32_t>(i));
  }
  ends.push_back(static_cast<std::uint32_t>(text.size()));
  auto line_of = [&](std::uint32_t offset) {
    return static_cast<std::uint32_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin() - 1);
  };

  std::vector<FoldRange> out;
  std::vector<std::uint32_t> open;  // offsets of the `{`s not yet closed
  for (const auto& t : highlight(text)) {
    if (t.kind != TokenKind::Bracket) continue;
    // One token is a run of adjacent braces, like `{}` or `}}`.
    for (std::uint32_t i = t.from; i < t.to; ++i) {
      if (text[i] == '{') {
        open.push_back(i);
        continue;
      }
      if (open.empty()) continue;  // a stray `}`
      std::uint32_t o = open.back();
      open.pop_back();
      std::uint32_t start = line_of(o), close_line = line_of(i);
      if (close_line < start + 2) continue;  // nothing between the `{` line and the `}` line
      std::uint32_t end = close_line - 1;
      out.push_back({.open = o,
                     .close = i,
                     .start_line = start,
                     .end_line = end,
                     .line = starts[start],
                     .from = ends[start],
                     .to = ends[end],
                     .first = starts[start + 1],
                     .last = starts[close_line]});
    }
  }
  // Outer blocks close after inner ones; keep the outermost per start line.
  std::sort(out.begin(), out.end(), [](const FoldRange& a, const FoldRange& b) {
    return a.start_line != b.start_line ? a.start_line < b.start_line : a.close > b.close;
  });
  out.erase(std::unique(out.begin(), out.end(),
                        [](const FoldRange& a, const FoldRange& b) { return a.start_line == b.start_line; }),
            out.end());
  return out;
}

}  // namespace dd::dsl
