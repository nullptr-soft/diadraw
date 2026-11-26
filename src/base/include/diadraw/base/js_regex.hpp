#pragma once

// JavaScript regular expressions over UTF-8 text, for code ported from the TypeScript packages
// whose behaviour depends on exact JavaScript semantics: `\s` is Unicode whitespace, `\p{L}` and
// `\p{N}` are Unicode classes, `.` excludes line terminators, lookahead and lookbehind work,
// captures inside a repeated group reset on each iteration, and an unmatched group is undefined
// (not empty). Supported flags: `i`, `m`, `s`, `u`, `g` (the global flag is up to the caller).
//
// A backtracking matcher on code points. Offsets are byte offsets into the UTF-8 input. The
// differences from V8 (UTF-16 code units in non-`u` patterns, full Unicode case folding) only
// matter for characters outside the Basic Multilingual Plane.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dd::js {

class Regex;

/// The result of a successful match (`RegExpExecArray`).
class Match {
 public:
  /// Byte offset of the match in the input.
  [[nodiscard]] std::size_t index() const { return static_cast<std::size_t>(groups_[0].first); }
  [[nodiscard]] std::size_t end() const { return static_cast<std::size_t>(groups_[0].second); }
  /// Whether group `n` took part in the match (`m[n] !== undefined`).
  [[nodiscard]] bool has(std::size_t n) const { return n < groups_.size() && groups_[n].first >= 0; }
  /// Group `n`, or "" when it did not take part.
  [[nodiscard]] std::string str(std::size_t n = 0) const;
  [[nodiscard]] std::string_view view(std::size_t n = 0) const;
  /// Group `n`, or nullopt when it did not take part.
  [[nodiscard]] std::optional<std::string> get(std::size_t n) const;
  [[nodiscard]] std::size_t size() const { return groups_.size(); }
  [[nodiscard]] std::string operator[](std::size_t n) const { return str(n); }

 private:
  friend class Regex;
  std::string_view input_;
  std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> groups_;
};

class Regex {
 public:
  /// Compiles a pattern in JavaScript syntax; throws std::invalid_argument when it is malformed.
  explicit Regex(std::string_view pattern, std::string_view flags = "");
  ~Regex();
  Regex(Regex&&) noexcept;
  Regex& operator=(Regex&&) noexcept;
  Regex(const Regex&) = delete;
  Regex& operator=(const Regex&) = delete;

  /// The first match at or after byte offset `from` (`exec` with `lastIndex = from`).
  [[nodiscard]] std::optional<Match> exec(std::string_view input, std::size_t from = 0) const;
  /// A match starting exactly at `at` (a sticky match).
  [[nodiscard]] std::optional<Match> match_at(std::string_view input, std::size_t at) const;
  [[nodiscard]] bool test(std::string_view input) const { return exec(input).has_value(); }
  // String literals live for the whole program.
  [[nodiscard]] std::optional<Match> exec(const char* input, std::size_t from = 0) const {
    return exec(std::string_view(input), from);
  }
  [[nodiscard]] std::vector<Match> match_all(const char* input) const { return match_all(std::string_view(input)); }
  // A match points into its input: matching a temporary string would leave it dangling.
  std::optional<Match> exec(std::string&&, std::size_t = 0) const = delete;
  std::optional<Match> match_at(std::string&&, std::size_t) const = delete;
  std::vector<Match> match_all(std::string&&) const = delete;
  /// Every match, as `matchAll` (or `exec` in a loop with the `g` flag) finds them.
  [[nodiscard]] std::vector<Match> match_all(std::string_view input) const;
  /// `input.replace(re, replacement)`: the first match, or every match when `all` (the `g` flag).
  /// The replacement understands `$&`, `$1`…, `` $` ``, `$'` and `$$`.
  [[nodiscard]] std::string replace(std::string_view input, std::string_view replacement, bool all) const;
  /// `input.replace(re, (…) => …)` with a callback.
  [[nodiscard]] std::string replace(std::string_view input, const std::function<std::string(const Match&)>& f,
                                    bool all) const;
  /// `input.split(re)`, with captured groups spliced in (undefined ones as nullopt).
  [[nodiscard]] std::vector<std::optional<std::string>> split_with_groups(std::string_view input) const;
  /// `input.split(re)` for patterns without captures.
  [[nodiscard]] std::vector<std::string> split(std::string_view input) const;
  [[nodiscard]] std::size_t group_count() const;

  struct Node;

 private:
  std::unique_ptr<struct Program> program_;
};

/// Unicode classes and case mapping, as JavaScript sees them.
bool is_letter(std::uint32_t cp);
bool is_number(std::uint32_t cp);
std::uint32_t to_lower(std::uint32_t cp);
std::uint32_t to_upper(std::uint32_t cp);
/// `s.toLowerCase()` / `s.toUpperCase()` (simple one-to-one mappings).
std::string to_lower(std::string_view s);
std::string to_upper(std::string_view s);

/// JavaScript's `GetSubstitution` for a replacement string.
std::string substitute(std::string_view replacement, const Match& m, std::string_view input);

}  // namespace dd::js
