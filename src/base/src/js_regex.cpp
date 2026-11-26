#include "diadraw/base/js_regex.hpp"

#include <algorithm>
#include <stdexcept>

#include "diadraw/base/text.hpp"

namespace dd::js {

namespace {

#include "unicode_tables.inc"

template <std::size_t N>
bool in_ranges(const std::uint32_t (&table)[N][2], std::uint32_t cp) {
  std::size_t lo = 0, hi = N;
  while (lo < hi) {
    std::size_t mid = (lo + hi) / 2;
    if (cp < table[mid][0]) hi = mid;
    else if (cp > table[mid][1]) lo = mid + 1;
    else return true;
  }
  return false;
}

template <std::size_t N>
std::uint32_t lookup(const std::uint32_t (&table)[N][2], std::uint32_t cp) {
  std::size_t lo = 0, hi = N;
  while (lo < hi) {
    std::size_t mid = (lo + hi) / 2;
    if (cp < table[mid][0]) hi = mid;
    else if (cp > table[mid][0]) lo = mid + 1;
    else return table[mid][1];
  }
  return cp;
}

bool is_line_terminator(std::uint32_t cp) { return cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029; }
bool is_word_char(std::uint32_t cp) {
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_';
}

/// A non-owning reference to a callable, cheaper than std::function for continuations.
template <class Sig>
class FunctionRef;
template <class R, class... A>
class FunctionRef<R(A...)> {
 public:
  template <class F>
  FunctionRef(F&& f)  // NOLINT(google-explicit-constructor)
      : obj_(const_cast<void*>(static_cast<const void*>(&f))), call_([](void* o, A... a) -> R {
          return (*static_cast<std::remove_reference_t<F>*>(o))(std::forward<A>(a)...);
        }) {}
  R operator()(A... a) const { return call_(obj_, std::forward<A>(a)...); }

 private:
  void* obj_;
  R (*call_)(void*, A...);
};

}  // namespace

bool is_letter(std::uint32_t cp) { return in_ranges(kLetterRanges, cp); }
bool is_number(std::uint32_t cp) { return in_ranges(kNumberRanges, cp); }
std::uint32_t to_lower(std::uint32_t cp) {
  if (cp < 0x80) return cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
  return lookup(kToLower, cp);
}
std::uint32_t to_upper(std::uint32_t cp) {
  if (cp < 0x80) return cp >= 'a' && cp <= 'z' ? cp - 32 : cp;
  return lookup(kToUpper, cp);
}
std::string to_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
      ++i;
      continue;
    }
    append_utf8(out, to_lower(next_code_point(s, i)));
  }
  return out;
}
std::string to_upper(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c));
      ++i;
      continue;
    }
    append_utf8(out, to_upper(next_code_point(s, i)));
  }
  return out;
}

// ── Patterns ──────────────────────────────────────────────────────────────────────────────────

enum class Kind { Empty, Char, Any, Class, Seq, Alt, Group, Repeat, Begin, End, WordB, NotWordB, Look, Backref };

struct ClassItem {
  enum Type { Range, Word, NotWord, Digit, NotDigit, Space, NotSpace, Letter, NotLetter, Number, NotNumber } type;
  std::uint32_t lo = 0, hi = 0;
};

struct Regex::Node {
  Kind kind = Kind::Empty;
  std::uint32_t cp = 0;
  bool negated = false;
  bool behind = false;
  bool greedy = true;
  int group = -1;
  int min = 0, max = -1;
  /// Capture groups inside this node (for resetting them on each repetition).
  int first_group = 0, end_group = 0;
  std::vector<ClassItem> items;
  std::vector<Node> children;
};

struct Program {
  Regex::Node root;
  int groups = 1;
  bool icase = false, multiline = false, dotall = false, unicode = false;
};

namespace {

using Node = Regex::Node;

class Parser {
 public:
  Parser(std::string_view p, Program& prog) : p_(p), prog_(prog) {
    // Count capture groups first, so `\N` knows whether it is a back reference.
    bool in_class = false;
    for (std::size_t i = 0; i < p.size(); ++i) {
      if (p[i] == '\\') ++i;
      else if (in_class) in_class = p[i] != ']';
      else if (p[i] == '[') in_class = true;
      else if (p[i] == '(' && (i + 1 >= p.size() || p[i + 1] != '?')) ++total_groups_;
    }
  }

  Node parse() {
    Node n = alternation();
    if (pos_ < p_.size()) fail("unmatched )");
    return n;
  }

 private:
  [[noreturn]] void fail(const char* what) const {
    throw std::invalid_argument(std::string("Invalid regular expression: ") + what + ": " + std::string(p_));
  }
  bool at_end() const { return pos_ >= p_.size(); }
  char peek(std::size_t k = 0) const { return pos_ + k < p_.size() ? p_[pos_ + k] : '\0'; }
  std::uint32_t next_cp() { return next_code_point(p_, pos_); }

  Node alternation() {
    int first = prog_.groups;
    std::vector<Node> alts;
    alts.push_back(sequence());
    while (peek() == '|' && !at_end()) {
      ++pos_;
      alts.push_back(sequence());
    }
    if (alts.size() == 1) return std::move(alts[0]);
    Node n;
    n.kind = Kind::Alt;
    n.children = std::move(alts);
    n.first_group = first;
    n.end_group = prog_.groups;
    return n;
  }

  Node sequence() {
    int first = prog_.groups;
    Node n;
    n.kind = Kind::Seq;
    while (!at_end() && peek() != '|' && peek() != ')') n.children.push_back(term());
    n.first_group = first;
    n.end_group = prog_.groups;
    if (n.children.size() == 1) return std::move(n.children[0]);
    return n;
  }

  /// `{n}`, `{n,}` or `{n,m}` at the current position; leaves the position alone when not one.
  bool braces(int& min, int& max) {
    std::size_t i = pos_ + 1;
    auto digits = [&](int& out) {
      std::size_t start = i;
      long v = 0;
      while (i < p_.size() && p_[i] >= '0' && p_[i] <= '9') v = std::min(v * 10 + (p_[i++] - '0'), 1L << 30);
      out = static_cast<int>(v);
      return i > start;
    };
    if (!digits(min)) return false;
    if (i < p_.size() && p_[i] == '}') {
      max = min;
      pos_ = i + 1;
      return true;
    }
    if (i >= p_.size() || p_[i] != ',') return false;
    ++i;
    if (i < p_.size() && p_[i] == '}') {
      max = -1;
      pos_ = i + 1;
      return true;
    }
    if (!digits(max) || i >= p_.size() || p_[i] != '}') return false;
    pos_ = i + 1;
    return true;
  }

  Node term() {
    int first = prog_.groups;
    Node atom_node = atom();
    for (;;) {
      int min = 0, max = -1;
      char c = peek();
      if (c == '*') {
        ++pos_;
      } else if (c == '+') {
        min = 1;
        ++pos_;
      } else if (c == '?') {
        max = 1;
        ++pos_;
      } else if (c == '{' && braces(min, max)) {
      } else {
        break;
      }
      Node r;
      r.kind = Kind::Repeat;
      r.min = min;
      r.max = max;
      if (peek() == '?') {
        r.greedy = false;
        ++pos_;
      }
      r.first_group = first;
      r.end_group = prog_.groups;
      r.children.push_back(std::move(atom_node));
      atom_node = std::move(r);
    }
    return atom_node;
  }

  static Node char_node(std::uint32_t cp) {
    Node n;
    n.kind = Kind::Char;
    n.cp = cp;
    return n;
  }
  static Node class_node(std::vector<ClassItem> items, bool negated) {
    Node n;
    n.kind = Kind::Class;
    n.items = std::move(items);
    n.negated = negated;
    return n;
  }

  std::uint32_t hex(std::size_t count) {
    std::uint32_t v = 0;
    for (std::size_t k = 0; k < count; ++k) {
      char c = peek();
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return 0xFFFFFFFF;
      v = v * 16 + static_cast<std::uint32_t>(d);
      ++pos_;
    }
    return v;
  }

  /// A character escape after `\` (shared by atoms and classes); nullopt for a class escape.
  std::optional<std::uint32_t> char_escape(char c) {
    switch (c) {
      case 'n': ++pos_; return '\n';
      case 'r': ++pos_; return '\r';
      case 't': ++pos_; return '\t';
      case 'v': ++pos_; return '\v';
      case 'f': ++pos_; return '\f';
      case '0':
        if (!(peek(1) >= '0' && peek(1) <= '9')) {
          ++pos_;
          return 0;
        }
        break;
      case 'x': {
        std::size_t save = pos_++;
        std::uint32_t v = hex(2);
        if (v != 0xFFFFFFFF) return v;
        pos_ = save + 1;
        return 'x';
      }
      case 'u': {
        std::size_t save = pos_++;
        if (peek() == '{' && prog_.unicode) {
          ++pos_;
          std::uint32_t v = 0;
          while (!at_end() && peek() != '}') {
            std::uint32_t d = hex(1);
            if (d == 0xFFFFFFFF) fail("bad \\u{…}");
            v = v * 16 + d;
          }
          ++pos_;
          return v;
        }
        std::uint32_t v = hex(4);
        if (v != 0xFFFFFFFF) {
          // A surrogate pair written as two escapes.
          if (v >= 0xD800 && v <= 0xDBFF && peek() == '\\' && peek(1) == 'u') {
            std::size_t back = pos_;
            pos_ += 2;
            std::uint32_t lo = hex(4);
            if (lo >= 0xDC00 && lo <= 0xDFFF) return 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            pos_ = back;
          }
          return v;
        }
        pos_ = save + 1;
        return 'u';
      }
      case 'c': {
        char l = peek(1);
        if ((l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z')) {
          pos_ += 2;
          return static_cast<std::uint32_t>(l) % 32;
        }
        return std::nullopt;
      }
      default: break;
    }
    return std::nullopt;
  }

  /// `\d`, `\w`, `\s`, `\p{…}` and their negations; nullopt for other escapes.
  std::optional<ClassItem> class_escape(char c) {
    switch (c) {
      case 'd': ++pos_; return ClassItem{ClassItem::Digit};
      case 'D': ++pos_; return ClassItem{ClassItem::NotDigit};
      case 'w': ++pos_; return ClassItem{ClassItem::Word};
      case 'W': ++pos_; return ClassItem{ClassItem::NotWord};
      case 's': ++pos_; return ClassItem{ClassItem::Space};
      case 'S': ++pos_; return ClassItem{ClassItem::NotSpace};
      case 'p':
      case 'P': {
        if (!prog_.unicode || peek(1) != '{') return std::nullopt;
        std::size_t close = p_.find('}', pos_);
        if (close == std::string_view::npos) fail("bad \\p");
        std::string_view name = p_.substr(pos_ + 2, close - pos_ - 2);
        pos_ = close + 1;
        bool neg = c == 'P';
        if (name == "L" || name == "Letter") return ClassItem{neg ? ClassItem::NotLetter : ClassItem::Letter};
        if (name == "N" || name == "Number") return ClassItem{neg ? ClassItem::NotNumber : ClassItem::Number};
        fail("unsupported Unicode property");
      }
      default: return std::nullopt;
    }
  }

  Node atom() {
    char c = peek();
    switch (c) {
      case '^': {
        ++pos_;
        Node n;
        n.kind = Kind::Begin;
        return n;
      }
      case '$': {
        ++pos_;
        Node n;
        n.kind = Kind::End;
        return n;
      }
      case '.': {
        ++pos_;
        Node n;
        n.kind = Kind::Any;
        return n;
      }
      case '[': return char_class();
      case '(': return group();
      case '*':
      case '+':
      case '?': fail("nothing to repeat");
      case '\\': {
        ++pos_;
        if (at_end()) fail("\\ at end of pattern");
        char e = peek();
        if (e == 'b' || e == 'B') {
          ++pos_;
          Node n;
          n.kind = e == 'b' ? Kind::WordB : Kind::NotWordB;
          return n;
        }
        if (auto item = class_escape(e)) return class_node({*item}, false);
        if (e >= '1' && e <= '9') {
          std::size_t i = pos_;
          int v = 0;
          while (i < p_.size() && p_[i] >= '0' && p_[i] <= '9') v = v * 10 + (p_[i++] - '0');
          if (v <= total_groups_) {
            pos_ = i;
            Node n;
            n.kind = Kind::Backref;
            n.group = v;
            return n;
          }
        }
        if (auto cp = char_escape(e)) return char_node(*cp);
        return char_node(next_cp());  // identity escape
      }
      default: return char_node(next_cp());
    }
  }

  Node group() {
    ++pos_;  // (
    Node n;
    if (peek() == '?') {
      char k = peek(1);
      if (k == ':') {
        pos_ += 2;
        n.kind = Kind::Group;
        n.group = -1;
      } else if (k == '=' || k == '!') {
        pos_ += 2;
        n.kind = Kind::Look;
        n.negated = k == '!';
      } else if (k == '<' && (peek(2) == '=' || peek(2) == '!')) {
        n.kind = Kind::Look;
        n.behind = true;
        n.negated = peek(2) == '!';
        pos_ += 3;
      } else {
        fail("unsupported group");
      }
    } else {
      n.kind = Kind::Group;
      n.group = prog_.groups++;
    }
    n.children.push_back(alternation());
    if (peek() != ')') fail("missing )");
    ++pos_;
    return n;
  }

  Node char_class() {
    ++pos_;  // [
    bool negated = false;
    if (peek() == '^') {
      negated = true;
      ++pos_;
    }
    std::vector<ClassItem> items;
    // One class atom: a character, or a class escape (nullopt char).
    auto class_atom = [&](std::optional<ClassItem>& escape) -> std::optional<std::uint32_t> {
      if (peek() == '\\') {
        ++pos_;
        char e = peek();
        if (e == 'b') {
          ++pos_;
          return 0x08;
        }
        if (e == '-') {
          ++pos_;
          return '-';
        }
        if (auto item = class_escape(e)) {
          escape = item;
          return std::nullopt;
        }
        if (auto cp = char_escape(e)) return cp;
        return next_cp();
      }
      return next_cp();
    };
    while (!at_end() && peek() != ']') {
      std::optional<ClassItem> esc;
      auto lo = class_atom(esc);
      if (!lo) {
        items.push_back(*esc);
        continue;
      }
      if (peek() == '-' && peek(1) != ']' && pos_ + 1 < p_.size()) {
        std::size_t save = pos_;
        ++pos_;
        std::optional<ClassItem> esc2;
        auto hi = class_atom(esc2);
        if (!hi) {
          // `[a-\d]`: the dash is literal (Annex B).
          items.push_back({ClassItem::Range, *lo, *lo});
          items.push_back({ClassItem::Range, '-', '-'});
          items.push_back(*esc2);
          continue;
        }
        if (*hi < *lo) {
          pos_ = save;
          fail("range out of order in character class");
        }
        items.push_back({ClassItem::Range, *lo, *hi});
        continue;
      }
      items.push_back({ClassItem::Range, *lo, *lo});
    }
    if (at_end()) fail("missing ]");
    ++pos_;  // ]
    return class_node(std::move(items), negated);
  }

  std::string_view p_;
  std::size_t pos_ = 0;
  Program& prog_;
  int total_groups_ = 0;
};

bool item_matches(const ClassItem& item, std::uint32_t cp) {
  switch (item.type) {
    case ClassItem::Range: return cp >= item.lo && cp <= item.hi;
    case ClassItem::Word: return is_word_char(cp);
    case ClassItem::NotWord: return !is_word_char(cp);
    case ClassItem::Digit: return cp >= '0' && cp <= '9';
    case ClassItem::NotDigit: return !(cp >= '0' && cp <= '9');
    case ClassItem::Space: return is_js_whitespace(cp);
    case ClassItem::NotSpace: return !is_js_whitespace(cp);
    case ClassItem::Letter: return is_letter(cp);
    case ClassItem::NotLetter: return !is_letter(cp);
    case ClassItem::Number: return is_number(cp);
    case ClassItem::NotNumber: return !is_number(cp);
  }
  return false;
}

/// Case-insensitive canonical form: upper case for non-unicode patterns (never mapping a non-ASCII
/// character to an ASCII one), simple case folding for unicode ones.
std::uint32_t canonical(std::uint32_t cp, bool unicode) {
  if (unicode) return to_lower(to_upper(cp));
  std::uint32_t u = to_upper(cp);
  return cp >= 0x80 && u < 0x80 ? cp : u;
}

class Matcher {
 public:
  Matcher(const Program& p, std::string_view s) : p_(p), s_(s), caps_(static_cast<std::size_t>(p.groups), {-1, -1}) {}

  using K = FunctionRef<bool(std::size_t)>;

  bool run(std::size_t start, std::size_t& end) {
    std::fill(caps_.begin(), caps_.end(), std::make_pair(std::ptrdiff_t{-1}, std::ptrdiff_t{-1}));
    return match(p_.root, start, [&](std::size_t q) {
      end = q;
      return true;
    });
  }
  const std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>>& caps() const { return caps_; }

 private:
  std::uint32_t cp_at(std::size_t pos, std::size_t& len) const {
    std::size_t i = pos;
    std::uint32_t cp = next_code_point(s_, i);
    len = i - pos;
    return cp;
  }
  std::uint32_t cp_before(std::size_t pos) const {
    std::size_t i = pos;
    do --i;
    while (i > 0 && (static_cast<unsigned char>(s_[i]) & 0xC0) == 0x80 && pos - i < 4);
    std::size_t j = i;
    return next_code_point(s_, j);
  }

  bool single(const Node& n, std::uint32_t cp) const {
    switch (n.kind) {
      case Kind::Char:
        return p_.icase ? canonical(cp, p_.unicode) == canonical(n.cp, p_.unicode) : cp == n.cp;
      case Kind::Any: return p_.dotall || !is_line_terminator(cp);
      case Kind::Class: {
        auto in = [&](std::uint32_t c) {
          for (const auto& item : n.items)
            if (item_matches(item, c)) return true;
          return false;
        };
        bool hit = in(cp);
        if (!hit && p_.icase) {
          std::uint32_t l = to_lower(cp), u = to_upper(cp);
          hit = (l != cp && in(l)) || (u != cp && in(u));
        }
        return hit != n.negated;
      }
      default: return false;
    }
  }
  static bool is_single(const Node& n) { return n.kind == Kind::Char || n.kind == Kind::Any || n.kind == Kind::Class; }

  bool seq(const std::vector<Node>& nodes, std::size_t i, std::size_t pos, K k) {
    if (i == nodes.size()) return k(pos);
    return match(nodes[i], pos, [&](std::size_t q) { return seq(nodes, i + 1, q, k); });
  }

  bool repeat(const Node& n, int count, std::size_t pos, K k) {
    const Node& child = n.children[0];
    auto iterate = [&]() -> bool {
      if (n.max >= 0 && count >= n.max) return false;
      // Captures inside the repeated atom start undefined on every iteration.
      std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> saved(caps_.begin() + n.first_group,
                                                                    caps_.begin() + n.end_group);
      for (int g = n.first_group; g < n.end_group; ++g) caps_[static_cast<std::size_t>(g)] = {-1, -1};
      bool r = match(child, pos, [&](std::size_t q) {
        if (q == pos && count >= n.min) return false;  // an empty iteration ends the loop
        return repeat(n, count + 1, q, k);
      });
      if (!r) std::copy(saved.begin(), saved.end(), caps_.begin() + n.first_group);
      return r;
    };
    if (count < n.min) return iterate();
    if (n.greedy) return iterate() || k(pos);
    return k(pos) || iterate();
  }

  bool match(const Node& n, std::size_t pos, K k) {
    switch (n.kind) {
      case Kind::Empty: return k(pos);
      case Kind::Char:
      case Kind::Any:
      case Kind::Class: {
        if (pos >= s_.size()) return false;
        std::size_t len = 0;
        std::uint32_t cp = cp_at(pos, len);
        return single(n, cp) && k(pos + len);
      }
      case Kind::Seq: return seq(n.children, 0, pos, k);
      case Kind::Alt:
        for (const auto& c : n.children)
          if (match(c, pos, k)) return true;
        return false;
      case Kind::Group: {
        if (n.group < 0) return match(n.children[0], pos, k);
        auto g = static_cast<std::size_t>(n.group);
        return match(n.children[0], pos, [&](std::size_t q) {
          auto old = caps_[g];
          caps_[g] = {static_cast<std::ptrdiff_t>(pos), static_cast<std::ptrdiff_t>(q)};
          if (k(q)) return true;
          caps_[g] = old;
          return false;
        });
      }
      case Kind::Repeat: {
        const Node& child = n.children[0];
        if (is_single(child)) {
          // One character at a time: iterate instead of recursing.
          if (n.greedy) {
            std::vector<std::size_t> at{pos};
            std::size_t cur = pos;
            while ((n.max < 0 || static_cast<int>(at.size()) - 1 < n.max) && cur < s_.size()) {
              std::size_t len = 0;
              if (!single(child, cp_at(cur, len))) break;
              cur += len;
              at.push_back(cur);
            }
            for (auto c = static_cast<int>(at.size()) - 1; c >= n.min; --c)
              if (k(at[static_cast<std::size_t>(c)])) return true;
            return false;
          }
          std::size_t cur = pos;
          for (int c = 0;; ++c) {
            if (c >= n.min && k(cur)) return true;
            if ((n.max >= 0 && c >= n.max) || cur >= s_.size()) return false;
            std::size_t len = 0;
            if (!single(child, cp_at(cur, len))) return false;
            cur += len;
          }
        }
        return repeat(n, 0, pos, k);
      }
      case Kind::Begin:
        return (pos == 0 || (p_.multiline && is_line_terminator(cp_before(pos)))) && k(pos);
      case Kind::End: {
        std::size_t len = 0;
        return (pos == s_.size() || (p_.multiline && is_line_terminator(cp_at(pos, len)))) && k(pos);
      }
      case Kind::WordB:
      case Kind::NotWordB: {
        std::size_t len = 0;
        bool a = pos > 0 && is_word_char(cp_before(pos));
        bool b = pos < s_.size() && is_word_char(cp_at(pos, len));
        return ((a != b) == (n.kind == Kind::WordB)) && k(pos);
      }
      case Kind::Look: {
        auto saved = caps_;
        bool found = false;
        if (n.behind) {
          for (std::size_t j = pos + 1; j-- > 0;) {
            if (j < s_.size() && (static_cast<unsigned char>(s_[j]) & 0xC0) == 0x80) continue;
            if (match(n.children[0], j, [&](std::size_t q) { return q == pos; })) {
              found = true;
              break;
            }
          }
        } else {
          found = match(n.children[0], pos, [](std::size_t) { return true; });
        }
        if (n.negated) {
          caps_ = saved;
          return !found && k(pos);
        }
        if (!found) {
          caps_ = saved;
          return false;
        }
        if (k(pos)) return true;
        caps_ = saved;
        return false;
      }
      case Kind::Backref: {
        const auto& g = caps_[static_cast<std::size_t>(n.group)];
        if (g.first < 0) return k(pos);
        std::string_view ref = s_.substr(static_cast<std::size_t>(g.first), static_cast<std::size_t>(g.second - g.first));
        if (!p_.icase) {
          if (s_.compare(pos, ref.size(), ref) != 0) return false;
          return k(pos + ref.size());
        }
        std::size_t i = 0, j = pos;
        while (i < ref.size()) {
          if (j >= s_.size()) return false;
          std::uint32_t a = next_code_point(ref, i);
          std::uint32_t b = next_code_point(s_, j);
          if (canonical(a, p_.unicode) != canonical(b, p_.unicode)) return false;
        }
        return k(j);
      }
    }
    return false;
  }

  const Program& p_;
  std::string_view s_;
  std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> caps_;
};

std::size_t advance(std::string_view s, std::size_t i) {
  if (i >= s.size()) return i + 1;
  next_code_point(s, i);
  return i;
}

}  // namespace

std::string Match::str(std::size_t n) const { return std::string(view(n)); }
std::string_view Match::view(std::size_t n) const {
  if (!has(n)) return {};
  return input_.substr(static_cast<std::size_t>(groups_[n].first),
                       static_cast<std::size_t>(groups_[n].second - groups_[n].first));
}
std::optional<std::string> Match::get(std::size_t n) const {
  return has(n) ? std::optional<std::string>(str(n)) : std::nullopt;
}

Regex::Regex(std::string_view pattern, std::string_view flags) : program_(std::make_unique<Program>()) {
  for (char f : flags) {
    if (f == 'i') program_->icase = true;
    else if (f == 'm') program_->multiline = true;
    else if (f == 's') program_->dotall = true;
    else if (f == 'u') program_->unicode = true;
  }
  Parser parser(pattern, *program_);
  program_->root = parser.parse();
}
Regex::~Regex() = default;
Regex::Regex(Regex&&) noexcept = default;
Regex& Regex::operator=(Regex&&) noexcept = default;

std::size_t Regex::group_count() const { return static_cast<std::size_t>(program_->groups) - 1; }

std::optional<Match> Regex::match_at(std::string_view input, std::size_t at) const {
  if (at > input.size()) return std::nullopt;
  Matcher m(*program_, input);
  std::size_t end = 0;
  if (!m.run(at, end)) return std::nullopt;
  Match out;
  out.input_ = input;
  out.groups_ = m.caps();
  out.groups_[0] = {static_cast<std::ptrdiff_t>(at), static_cast<std::ptrdiff_t>(end)};
  return out;
}

std::optional<Match> Regex::exec(std::string_view input, std::size_t from) const {
  Matcher m(*program_, input);
  const bool anchored = program_->root.kind == Kind::Begin ||
                        (program_->root.kind == Kind::Seq && !program_->root.children.empty() &&
                         program_->root.children[0].kind == Kind::Begin);
  for (std::size_t start = from; start <= input.size(); start = advance(input, start)) {
    std::size_t end = 0;
    if (m.run(start, end)) {
      Match out;
      out.input_ = input;
      out.groups_ = m.caps();
      out.groups_[0] = {static_cast<std::ptrdiff_t>(start), static_cast<std::ptrdiff_t>(end)};
      return out;
    }
    if (anchored && !program_->multiline) break;
  }
  return std::nullopt;
}

std::vector<Match> Regex::match_all(std::string_view input) const {
  std::vector<Match> out;
  std::size_t from = 0;
  while (from <= input.size()) {
    auto m = exec(input, from);
    if (!m) break;
    from = m->end() == m->index() ? advance(input, m->end()) : m->end();
    out.push_back(std::move(*m));
  }
  return out;
}

std::string substitute(std::string_view replacement, const Match& m, std::string_view input) {
  std::string out;
  const std::size_t groups = m.size() - 1;
  for (std::size_t i = 0; i < replacement.size(); ++i) {
    char c = replacement[i];
    if (c != '$' || i + 1 >= replacement.size()) {
      out.push_back(c);
      continue;
    }
    char n = replacement[i + 1];
    if (n == '$') {
      out.push_back('$');
      ++i;
    } else if (n == '&') {
      out += m.view(0);
      ++i;
    } else if (n == '`') {
      out += input.substr(0, m.index());
      ++i;
    } else if (n == '\'') {
      out += input.substr(m.end());
      ++i;
    } else if (n >= '0' && n <= '9') {
      std::size_t one = static_cast<std::size_t>(n - '0');
      char n2 = i + 2 < replacement.size() ? replacement[i + 2] : '\0';
      if (n2 >= '0' && n2 <= '9') {
        std::size_t two = one * 10 + static_cast<std::size_t>(n2 - '0');
        if (two >= 1 && two <= groups) {
          out += m.view(two);
          i += 2;
          continue;
        }
      }
      if (one >= 1 && one <= groups) {
        out += m.view(one);
        ++i;
      } else {
        out.push_back('$');
      }
    } else {
      out.push_back('$');
    }
  }
  return out;
}

std::string Regex::replace(std::string_view input, const std::function<std::string(const Match&)>& f,
                           bool all) const {
  std::string out;
  std::size_t last = 0;
  std::size_t from = 0;
  while (from <= input.size()) {
    auto m = exec(input, from);
    if (!m) break;
    out.append(input, last, m->index() - last);
    out += f(*m);
    last = m->end();
    from = m->end() == m->index() ? advance(input, m->end()) : m->end();
    if (!all) break;
  }
  if (last < input.size()) out.append(input, last, std::string_view::npos);
  return out;
}

std::string Regex::replace(std::string_view input, std::string_view replacement, bool all) const {
  return replace(input, [&](const Match& m) { return substitute(replacement, m, input); }, all);
}

std::vector<std::optional<std::string>> Regex::split_with_groups(std::string_view input) const {
  std::vector<std::optional<std::string>> out;
  if (input.empty()) {
    if (!match_at(input, 0)) out.emplace_back("");
    return out;
  }
  std::size_t p = 0, q = 0;
  while (q < input.size()) {
    auto z = match_at(input, q);
    if (!z || z->end() == p) {
      q = advance(input, q);
      continue;
    }
    std::size_t e = std::min(z->end(), input.size());
    out.emplace_back(std::string(input.substr(p, q - p)));
    for (std::size_t g = 1; g < z->size(); ++g) out.push_back(z->get(g));
    p = e;
    q = p;
  }
  out.emplace_back(std::string(input.substr(p)));
  return out;
}

std::vector<std::string> Regex::split(std::string_view input) const {
  std::vector<std::string> out;
  for (auto& part : split_with_groups(input)) out.push_back(part ? std::move(*part) : std::string());
  return out;
}

}  // namespace dd::js
