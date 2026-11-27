#include "diadraw/dsl/format.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "diadraw/dsl/syntax.hpp"

namespace dd::dsl {

namespace {

enum class Kind : std::uint8_t {
  Word,          ///< an identifier or a number
  Punct,         ///< `{ } = . , * -> --> <-> ->>`
  String,        ///< a closed string, one-line or block
  LineComment,   ///< `// …`, without the line break
  BlockComment,  ///< `/* … */`
  Other,         ///< a character that is no token, or a one-line string that is not closed
  Rest,          ///< an unclosed block string or block comment: everything to the end
};

struct Piece {
  Kind kind = Kind::Other;
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  /// Line breaks between the piece before and this one (for the first piece, before it).
  std::uint32_t breaks = 0;
  /// `{` count minus `}` count before this piece (never below 0 along the way).
  std::uint32_t depth = 0;
};

constexpr std::string_view kBom = "\xEF\xBB\xBF";

/// The lexer's tokens and comments (syntax.hpp) as one list in source order, with the line
/// breaks and brace depth before each. The pieces are exactly what the parser reads, so the
/// formatter changes only what lies between them.
std::vector<Piece> scan(std::string_view s) {
  Lexed lexed = lex_source(s);
  std::vector<Piece> out;
  out.reserve(lexed.tokens.size() + lexed.comments.size());
  auto token = lexed.tokens.begin();
  auto comment = lexed.comments.begin();
  while (token != lexed.tokens.end() || comment != lexed.comments.end()) {
    Piece p;
    if (comment == lexed.comments.end() || (token != lexed.tokens.end() && token->from < comment->from)) {
      p.from = token->from;
      p.to = token->to;
      switch (token->kind) {
        case Lexeme::Kind::Identifier:
        case Lexeme::Kind::Number: p.kind = Kind::Word; break;
        case Lexeme::Kind::Punct: p.kind = Kind::Punct; break;
        case Lexeme::Kind::String: p.kind = Kind::String; break;
        case Lexeme::Kind::Invalid:
          p.kind = lexed.unclosed_block_string == token->from ? Kind::Rest : Kind::Other;
          break;
      }
      ++token;
    } else {
      p.from = comment->from;
      p.to = comment->to;
      std::string_view text = s.substr(p.from, p.to - p.from);
      if (!comment->block) {
        p.kind = Kind::LineComment;
        // Spaces and a `\r` before the line break end the line, not the comment.
        while (p.to > p.from + 2 && (s[p.to - 1] == ' ' || s[p.to - 1] == '\t' || s[p.to - 1] == '\r')) --p.to;
      } else p.kind = text.size() >= 4 && text.ends_with("*/") ? Kind::BlockComment : Kind::Rest;
      ++comment;
    }
    const std::uint32_t after = out.empty() ? 0 : out.back().to;
    p.breaks = static_cast<std::uint32_t>(std::count(s.begin() + after, s.begin() + p.from, '\n'));
    p.depth = out.empty() ? 0 : out.back().depth;
    if (!out.empty() && out.back().kind == Kind::Punct && out.back().to - out.back().from == 1) {
      char c = s[out.back().from];
      if (c == '{') ++p.depth;
      else if (c == '}' && p.depth > 0) --p.depth;
    }
    out.push_back(p);
    if (p.kind == Kind::Rest) break;  // runs to the end
  }
  return out;
}

bool spans_lines(std::string_view s, const Piece& p) {
  return s.substr(p.from, p.to - p.from).find('\n') != std::string_view::npos;
}

/// The pieces of the syntax tree's statements that continue a statement begun on an earlier line:
/// a node (other than the document and skipped input) starts before the piece, on an earlier
/// line, at the same brace depth, and runs past it.
std::vector<bool> continuations(std::string_view s, const std::vector<Piece>& pieces,
                                const std::vector<std::size_t>& line_starts) {
  std::vector<bool> out(line_starts.size(), false);
  if (line_starts.empty()) return out;
  std::vector<std::uint32_t> froms;
  froms.reserve(pieces.size());
  for (const auto& p : pieces) froms.push_back(p.from);
  auto piece_at = [&](std::uint32_t offset) -> const Piece* {
    auto it = std::lower_bound(froms.begin(), froms.end(), offset);
    return it != froms.end() && *it == offset ? &pieces[static_cast<std::size_t>(it - froms.begin())] : nullptr;
  };
  // Line starts in source order, for a range lookup.
  std::vector<std::uint32_t> firsts;
  firsts.reserve(line_starts.size());
  for (std::size_t l : line_starts) firsts.push_back(pieces[l].from);

  Tree tree = parse_syntax(s);
  walk(tree.root, [&](const Node& node) {
    if (node.type == NodeType::Error) return false;
    if (node.type == NodeType::Document || node.children.empty()) return true;
    const Piece* head = piece_at(node.from);
    if (!head) return true;
    auto lo = std::upper_bound(firsts.begin(), firsts.end(), node.from);
    auto hi = std::lower_bound(lo, firsts.end(), node.to);
    for (auto it = lo; it != hi; ++it) {
      auto line = static_cast<std::size_t>(it - firsts.begin());
      const Piece& first = pieces[line_starts[line]];
      // A line that opens or closes a block sits at the block's depth, not as a continuation.
      bool brace = first.kind == Kind::Punct && first.to - first.from == 1 && (s[first.from] == '{' || s[first.from] == '}');
      if (!brace && first.depth == head->depth) out[line] = true;
    }
    return true;
  });
  return out;
}

bool is_punct(std::string_view s, const Piece& p, std::string_view text) {
  return p.kind == Kind::Punct && s.substr(p.from, p.to - p.from) == text;
}

bool is_operator(std::string_view s, const Piece& p) {
  if (p.kind != Kind::Punct) return false;
  std::string_view t = s.substr(p.from, p.to - p.from);
  return t == "=" || t == "->" || t == "-->" || t == "<->" || t == "->>";
}

/// The space between two pieces on one line.
std::string_view gap(std::string_view s, const Piece& a, const Piece& b) {
  constexpr std::string_view kOne = " ", kNone = "";
  const std::string_view kept = a.to < b.from ? kOne : kNone;  // any whitespace, as one space
  if (b.kind == Kind::LineComment) return kOne;
  if (is_punct(s, b, ",")) return kNone;
  if (is_punct(s, a, ",")) return kOne;
  if (is_operator(s, a) || is_operator(s, b)) return kOne;
  // `{}`, `}}` and `{{` stay as they are written.
  if ((is_punct(s, a, "{") || is_punct(s, a, "}")) && (is_punct(s, b, "{") || is_punct(s, b, "}")) &&
      !(is_punct(s, a, "}") && is_punct(s, b, "{")))
    return kept;
  if (is_punct(s, a, "{") || is_punct(s, a, "}") || is_punct(s, b, "{") || is_punct(s, b, "}")) return kOne;
  if (a.kind == Kind::Punct || b.kind == Kind::Punct) return kept;  // `.` and `*`
  if (a.kind == Kind::Other || b.kind == Kind::Other || a.kind == Kind::Rest || b.kind == Kind::Rest) return kept;
  return kOne;  // words, strings and block comments
}

/// A blank line inside a block string, as lowering reads it (lower.cpp): one `\r` at its end is
/// part of the line break, and the rest is spaces and tabs.
bool blank(std::string_view line) {
  if (line.ends_with('\r')) line.remove_suffix(1);
  return line.find_first_not_of(" \t") == std::string_view::npos;
}

/// How many characters of whitespace a line starts with.
std::size_t leading_blanks(std::string_view line) {
  std::size_t k = line.find_first_not_of(" \t");
  return k == std::string_view::npos ? line.size() : k;
}

/// Appends a string or block comment that spans lines, its later lines moved `shift` columns.
/// A block string moves only when its first line is blank after the `"""`, since its text keeps
/// the indentation its lines do not share, and a one-line string continued with `\` never does;
/// when one line has too little indentation to move left, nothing moves.
void append_moved(std::string& out, std::string_view text, long shift, Kind kind) {
  std::vector<std::string_view> lines;
  for (std::size_t start = 0;;) {
    std::size_t nl = text.find('\n', start);
    lines.push_back(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  if (kind == Kind::String &&
      (!text.starts_with(kBlockQuote) || !blank(lines.front().substr(kBlockQuote.size()))))
    shift = 0;
  if (shift < 0)
    for (std::size_t i = 1; i < lines.size(); ++i)
      if (!blank(lines[i]) && leading_blanks(lines[i]) < static_cast<std::size_t>(-shift)) shift = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::string_view line = lines[i];
    if (i) {
      out.push_back('\n');
      if (!blank(line)) {
        if (shift > 0) out.append(static_cast<std::size_t>(shift), ' ');
        else line.remove_prefix(static_cast<std::size_t>(-shift));
      }
    }
    out.append(line);
  }
}

}  // namespace

std::string format_source(std::string_view source, const FormatOptions& options) {
  const auto indent_size =
      static_cast<std::size_t>(std::clamp(options.indent_size, kMinIndentSize, kMaxIndentSize));
  std::string out;
  std::string_view s = source;
  if (s.starts_with(kBom)) {
    out.append(kBom);
    s.remove_prefix(kBom.size());
  }
  std::vector<Piece> pieces = scan(s);
  if (pieces.empty()) return out;
  // Line endings follow the output's first line break: blank lines before the first piece go.
  const std::size_t first_break = s.find('\n', pieces.front().from);
  const std::string_view eol = first_break != std::string_view::npos && first_break > 0 && s[first_break - 1] == '\r'
                                   ? std::string_view("\r\n")
                                   : std::string_view("\n");

  // The piece each line starts with.
  std::vector<std::size_t> line_starts;
  for (std::size_t i = 0; i < pieces.size(); ++i)
    if (i == 0 || pieces[i].breaks > 0) line_starts.push_back(i);
  const std::vector<bool> continued = continuations(s, pieces, line_starts);

  out.reserve(out.size() + s.size() + s.size() / 8);
  auto is_open = [&](const Piece& p) { return is_punct(s, p, "{"); };
  auto is_close = [&](const Piece& p) { return is_punct(s, p, "}"); };
  for (std::size_t line = 0; line < line_starts.size(); ++line) {
    const std::size_t begin = line_starts[line];
    const std::size_t end = line + 1 < line_starts.size() ? line_starts[line + 1] : pieces.size();
    const Piece& first = pieces[begin];
    if (line > 0) {
      out.append(eol);
      // One blank line at most, and none after `{` (and a comment after it) or before `}`.
      const std::size_t prev_begin = line_starts[line - 1];
      std::size_t last = begin - 1;
      while (last > prev_begin && (pieces[last].kind == Kind::LineComment || pieces[last].kind == Kind::BlockComment))
        --last;
      if (first.breaks > 1 && !is_open(pieces[last]) && !is_close(first)) out.append(eol);
    }
    std::size_t level = first.depth;
    if (is_close(first) && level > 0) --level;
    if (continued[line]) ++level;
    const std::size_t indent = level * indent_size;
    // Where the line started in the source, to move a block string's lines as far as the line.
    std::size_t source_line = s.rfind('\n', first.from == 0 ? 0 : first.from - 1);
    source_line = first.from == 0 || source_line == std::string_view::npos ? 0 : source_line + 1;
    // Characters of whitespace (the lexer's, so a no-break space is one), not bytes.
    std::size_t was_indent = 0;
    for (std::size_t k = source_line; k < first.from; ++k)
      if ((static_cast<unsigned char>(s[k]) & 0xC0) != 0x80) ++was_indent;
    const long shift = static_cast<long>(indent) - static_cast<long>(was_indent);

    out.append(indent, ' ');
    for (std::size_t i = begin; i < end; ++i) {
      const Piece& p = pieces[i];
      if (i > begin) out.append(gap(s, pieces[i - 1], p));
      std::string_view text = s.substr(p.from, p.to - p.from);
      // An unclosed string runs to the line break, `\r` included; a `\r\n` eol puts the same byte
      // back. (Under a `\n` eol it stays, since dropping it could unescape a `\` before it.)
      if (p.kind == Kind::Other && eol.size() == 2 && text.ends_with('\r') && p.to < s.size() && s[p.to] == '\n')
        text.remove_suffix(1);
      if (p.kind == Kind::Rest || (p.kind == Kind::Other && i + 1 == pieces.size())) {
        // An unclosed block runs to the end; text that is no token at the end could take in a
        // line break added after it (a `\` in an unclosed string, a cut UTF-8 sequence). Either
        // way the rest of the source stays as it is.
        out.append(s.substr(p.from));
        return out;
      }
      if ((p.kind == Kind::String || p.kind == Kind::BlockComment) && spans_lines(s, p))
        append_moved(out, text, shift, p.kind);
      else
        out.append(text);
    }
  }
  out.append(eol);
  return out;
}

}  // namespace dd::dsl
