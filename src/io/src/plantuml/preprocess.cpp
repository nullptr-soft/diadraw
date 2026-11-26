// Port of packages/import/src/plantuml/preprocess.ts: includes (local files through a resolver,
// the vendored C4-PlantUML library for stdlib and known URLs, icon libraries by name),
// `!define`/`!definelong`, variables, `!function`/`!procedure` (with default and named
// arguments), `!if`/`!elseif`/`!else`/`!ifdef`/`!ifndef`, `!includesub` and the common `%`
// builtins. Nothing is ever fetched.

#include <algorithm>
#include <cmath>
#include <map>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/library/builtin.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

namespace {

/// A preprocessor value: a string or a number.
struct Value {
  bool num = false;
  double n = 0;
  std::string s;

  static Value of(double d) { return {true, d, {}}; }
  static Value of(std::string str) { return {false, 0, std::move(str)}; }
  [[nodiscard]] std::string str() const { return num ? js_number_string(n) : s; }
  [[nodiscard]] double number() const { return num ? n : js_to_number(s); }
};

bool truthy(const Value& v) {
  if (v.num) return v.n != 0;
  return !v.s.empty() && v.s != "0" && js::to_lower(v.s) != "false";
}

struct Param {
  std::string name;
  std::optional<std::string> fallback;
};

struct Callable {
  bool function = false;  // else a procedure
  std::vector<Param> params;
  std::vector<std::string> body;
  /// One-line functions: `!function $f($a) !return $a + 1`.
  std::optional<std::string> inline_return;
};

struct Define {
  std::optional<std::vector<std::string>> params;
  std::string body;
};

/// A JavaScript Map: insertion order; setting a key again keeps its place; deleting forgets it.
template <class V>
struct JsMap {
  std::vector<std::string> order;
  std::map<std::string, V, std::less<>> values;

  void set(const std::string& k, V v) {
    auto it = values.find(k);
    if (it == values.end()) {
      order.push_back(k);
      values.emplace(k, std::move(v));
    } else {
      it->second = std::move(v);
    }
  }
  [[nodiscard]] const V* get(std::string_view k) const {
    auto it = values.find(k);
    return it == values.end() ? nullptr : &it->second;
  }
  [[nodiscard]] bool has(std::string_view k) const { return values.find(k) != values.end(); }
  void erase(std::string_view k) {
    auto it = values.find(k);
    if (it == values.end()) return;
    values.erase(it);
    order.erase(std::find(order.begin(), order.end(), k));
  }
};

/// Icon libraries in the PlantUML stdlib (and their usual URLs), by include pattern, from the
/// mappings of the built-in `plantuml-*` libraries.
const std::vector<std::pair<Regex, std::string>>& library_patterns(std::string_view format) {
  static std::map<std::string, std::vector<std::pair<Regex, std::string>>, std::less<>> cache;
  auto it = cache.find(format);
  if (it != cache.end()) return it->second;
  std::vector<std::pair<Regex, std::string>> out;
  static const Regex kPrefix("^plantuml-");
  for (const auto& lib : library::builtin_bundles().as_array()) {
    const Json* mappings = lib.get("mappings");
    const Json* patterns = mappings ? mappings->get(format) : nullptr;
    if (!patterns || !patterns->is_object()) continue;
    const Json* name = lib.get("name");
    std::string lib_name = kPrefix.replace(name && name->is_string() ? name->as_string() : "", "", false);
    for (const auto& [pattern, _] : patterns->as_object()) out.emplace_back(Regex(pattern, "i"), lib_name);
  }
  return cache.emplace(std::string(format), std::move(out)).first->second;
}

bool is_c4_include(std::string_view target) {
  static const Regex kStdlib("^<C4\\/", "i");
  static const Regex kNamed(
      "C4-PlantUML|C4_(Context|Container|Component|Deployment|Dynamic|Sequence)(\\.puml)?$|^C4(\\.puml)?$", "i");
  return kStdlib.test(target) || kNamed.test(target);
}

std::vector<std::string> split_lines(std::string_view text) {
  static const Regex kLines("\\r?\\n");
  return kLines.split(text);
}

std::vector<std::string> strip_included_comments(const std::vector<std::string>& lines) {
  return split(strip_comments(join(lines, "\n")), '\n');
}

/// Finds the `(...)` that starts at `open`, returning the index of the matching `)`.
std::optional<std::size_t> closing_paren(std::string_view text, std::size_t open) {
  int depth = 0;
  char quote = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    char ch = text[i];
    if (quote) {
      if (ch == quote) quote = 0;
    } else if (ch == '"') {
      quote = ch;
    } else if (ch == '(') {
      ++depth;
    } else if (ch == ')' && --depth == 0) {
      return i;
    }
  }
  return std::nullopt;
}

std::string unquote_both(std::string_view s) {
  std::string_view t = js_trim(s);
  if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
    return std::string(t.substr(1, t.size() - 2));
  // `"` alone starts and ends with a quote too: slice(1, -1) of one character is "".
  if (t.size() == 1 && (t[0] == '"' || t[0] == '\'')) return "";
  return std::string(t);
}

/// `s.substr(start, length)` in UTF-16 code units.
std::string js_substr(std::string_view s, double start, std::optional<double> length) {
  auto size = static_cast<double>(utf16_length(s));
  auto integer = [](double d) { return std::isnan(d) ? 0.0 : std::trunc(d); };
  double from = integer(start);
  if (from < 0) from = std::max(size + from, 0.0);
  from = std::min(from, size);
  double count = length ? std::min(std::max(integer(*length), 0.0), size - from) : size - from;
  if (count <= 0) return "";
  std::size_t a = byte_offset(s, static_cast<std::size_t>(from));
  std::size_t b = byte_offset(s, static_cast<std::size_t>(from + count));
  return std::string(s.substr(a, b - a));
}

std::string to_hex(double d) {
  if (std::isnan(d)) return "NaN";
  if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
  bool negative = d < 0;
  double v = std::trunc(std::fabs(d));
  std::string out;
  if (v == 0) out = "0";
  while (v >= 1) {
    double digit = std::fmod(v, 16);
    out.insert(out.begin(), "0123456789abcdef"[static_cast<int>(digit)]);
    v = std::floor(v / 16);
  }
  return negative && out != "0" ? "-" + out : out;
}

double parse_int_hex(std::string_view s) {
  std::string_view t = js_trim(s);
  bool negative = false;
  if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
    negative = t[0] == '-';
    t.remove_prefix(1);
  }
  if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
  double v = 0;
  std::size_t n = 0;
  for (char c : t) {
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) break;
    v = v * 16 + d;
    ++n;
  }
  if (n == 0) return std::nan("");
  return negative ? -v : v;
}

class Preprocessor {
 public:
  explicit Preprocessor(const PlantUmlOptions& options) : options_(options) {}

  PreprocessResult result;

  void run(std::string_view source) {
    std::vector<SourceLine> lines;
    int n = 0;
    for (auto& text : split_lines(source)) lines.push_back({std::move(text), ++n});
    run_lines(lines, false);
  }

 private:
  // ---------------------------------------------------------------- values

  const Value* get(std::string_view name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
      if (const Value* v = it->get(name)) return v;
    return globals_.get(name);
  }

  void set(const std::string& name, Value value, const std::string& where = "auto") {
    if (where == "global" || scopes_.empty()) {
      globals_.set(name, std::move(value));
      return;
    }
    auto& scope = scopes_.back();
    if (where == "local" || scope.has(name)) scope.set(name, std::move(value));
    else if (globals_.has(name)) globals_.set(name, std::move(value));
    else scope.set(name, std::move(value));
  }

  /// Evaluates a preprocessor expression (strings, numbers, variables, calls, operators).
  Value evaluate(const std::string& expr, int line) {
    static const Regex kToken(
        R"re("(?:[^"\\]|\\.)*"|'[^']*'|\d+(?:\.\d+)?|[$%]?[A-Za-z_][\w]*\s*\(|[$%]?[A-Za-z_][\w]*|==|!=|<=|>=|&&|\|\||[-+*/<>!(),?:])re",
        "g");
    std::vector<std::string> tokens;
    for (const auto& m : kToken.match_all(expr)) tokens.push_back(m.str());
    std::size_t pos = 0;
    auto peek = [&]() -> const std::string* { return pos < tokens.size() ? &tokens[pos] : nullptr; };
    auto is = [&](const char* t) { return pos < tokens.size() && tokens[pos] == t; };
    std::function<Value()> or_expr;
    std::function<Value()> primary = [&]() -> Value {
      if (pos >= tokens.size()) {
        ++pos;
        return Value::of(std::string());
      }
      std::string t = tokens[pos++];
      if (t == "(") {
        Value v = or_expr();
        if (is(")")) ++pos;
        return v;
      }
      if (t == "!") return Value::of(truthy(primary()) ? 0.0 : 1.0);
      if (t == "-") return Value::of(-primary().number());
      if (t[0] == '"' || t[0] == '\'') {
        static const Regex kEscape("\\\\(.)", "g");
        return Value::of(kEscape.replace(std::string_view(t).substr(1, t.size() >= 2 ? t.size() - 2 : 0), "$1", true));
      }
      if (t[0] >= '0' && t[0] <= '9') return Value::of(js_to_number(t));
      if (t.back() == '(') {
        std::string name = trim(std::string_view(t).substr(0, t.size() - 1));
        std::vector<std::string> args;
        while (peek() && !is(")")) {
          args.push_back(or_expr().str());
          if (is(",")) ++pos;
        }
        ++pos;
        return call(name, args, line);
      }
      if (t[0] == '$') {
        const Value* v = get(t);
        return v ? *v : Value::of(std::string());
      }
      // A bare word: a variable without `$`, a defined macro, or plain text.
      if (const Value* v = get(t)) return *v;
      if (const Define* d = defines_.get(t)) return Value::of(d->body);
      return Value::of(t);
    };
    auto product = [&]() -> Value {
      Value v = primary();
      while (is("*") || is("/")) {
        std::string op = tokens[pos++];
        double r = primary().number();
        v = Value::of(op == "*" ? v.number() * r : v.number() / r);
      }
      return v;
    };
    auto sum = [&]() -> Value {
      Value v = product();
      while (is("+") || is("-")) {
        std::string op = tokens[pos++];
        Value r = product();
        if (op == "+") v = v.num && r.num ? Value::of(v.n + r.n) : Value::of(v.str() + r.str());
        else v = Value::of(v.number() - r.number());
      }
      return v;
    };
    auto compare = [&]() -> Value {
      Value v = sum();
      const std::string* op = peek();
      if (op && (*op == "==" || *op == "!=" || *op == "<" || *op == ">" || *op == "<=" || *op == ">=")) {
        std::string o = *op;
        ++pos;
        Value r = sum();
        bool res;
        if (v.num || r.num) {
          double a = v.number(), b = r.number();
          res = o == "==" ? a == b : o == "!=" ? a != b : o == "<" ? a < b : o == ">" ? a > b : o == "<=" ? a <= b : a >= b;
        } else {
          const std::string &a = v.s, &b = r.s;
          res = o == "==" ? a == b : o == "!=" ? a != b : o == "<" ? a < b : o == ">" ? a > b : o == "<=" ? a <= b : a >= b;
        }
        return Value::of(res ? 1.0 : 0.0);
      }
      return v;
    };
    auto and_expr = [&]() -> Value {
      Value v = compare();
      while (is("&&")) {
        ++pos;
        Value r = compare();
        v = Value::of(truthy(v) && truthy(r) ? 1.0 : 0.0);
      }
      return v;
    };
    or_expr = [&]() -> Value {
      Value v = and_expr();
      while (is("||")) {
        ++pos;
        Value r = and_expr();
        v = Value::of(truthy(v) || truthy(r) ? 1.0 : 0.0);
      }
      if (is("?")) {
        ++pos;
        Value a = or_expr();
        if (is(":")) ++pos;
        Value b = or_expr();
        return truthy(v) ? a : b;
      }
      return v;
    };
    return or_expr();
  }

  Value call(const std::string& name, const std::vector<std::string>& args, int line) {
    auto s = [&](std::size_t i) { return i < args.size() ? args[i] : std::string(); };
    if (name == "%strlen" || name == "%size") return Value::of(static_cast<double>(utf16_length(s(0))));
    if (name == "%substr")
      return Value::of(js_substr(s(0), args.size() > 1 ? js_to_number(args[1]) : 0,
                                 args.size() > 2 ? std::optional<double>(js_to_number(args[2])) : std::nullopt));
    if (name == "%strpos") {
      std::string hay = s(0);
      auto at = hay.find(s(1));
      return Value::of(at == std::string::npos ? -1.0 : static_cast<double>(utf16_offset(hay, at)));
    }
    if (name == "%upper") return Value::of(js::to_upper(s(0)));
    if (name == "%lower") return Value::of(js::to_lower(s(0)));
    if (name == "%true") return Value::of(1.0);
    if (name == "%false") return Value::of(0.0);
    if (name == "%not") return Value::of(truthy(Value::of(s(0))) ? 0.0 : 1.0);
    if (name == "%boolval") return Value::of(truthy(Value::of(s(0))) ? 1.0 : 0.0);
    if (name == "%newline") return Value::of(std::string("\n"));
    if (name == "%intval") {
      double d = std::trunc(js_to_number(s(0)));
      return Value::of(std::isnan(d) || d == 0 ? 0.0 : d);
    }
    if (name == "%string") return Value::of(s(0));
    if (name == "%variable_exists") {
      std::string v = s(0);
      return Value::of(get(v[0] == '$' ? v : "$" + v) || get(v) ? 1.0 : 0.0);
    }
    if (name == "%function_exists") return Value::of(callables_.has(s(0)) ? 1.0 : 0.0);
    if (name == "%get_variable_value") {
      const Value* v = get(s(0));
      return v ? *v : Value::of(std::string());
    }
    if (name == "%set_variable_value") {
      set(s(0), Value::of(s(1)), "global");
      return Value::of(std::string());
    }
    if (name == "%chr") {
      double d = js_to_number(s(0));
      auto unit = static_cast<std::uint32_t>(std::isnan(d) || std::isinf(d) ? 0 : static_cast<std::int64_t>(std::trunc(d)) & 0xFFFF);
      std::string out;
      append_utf8(out, unit);
      return Value::of(out);
    }
    if (name == "%dec2hex") return Value::of(to_hex(js_to_number(s(0))));
    if (name == "%hex2dec") return Value::of(parse_int_hex(s(0)));
    if (name == "%date" || name == "%filename" || name == "%dirpath" || name == "%version" || name == "%feature")
      return Value::of(std::string());
    const Callable* fn = callables_.get(name);
    if (fn && fn->function) {
      Callable copy = *fn;
      auto v = invoke(copy, args, line, true);
      return v ? *v : Value::of(std::string());
    }
    return Value::of(name + "(" + join(args, ", ") + ")");
  }

  /// Runs a function or procedure; procedures emit their body lines.
  std::optional<Value> invoke(const Callable& fn, const std::vector<std::string>& raw_args, int line, bool is_function) {
    if (depth_ > 50) {
      result.issues.push_back({line, "Preprocessor recursion is too deep"});
      return std::nullopt;
    }
    static const Regex kNamed(R"re(^(\$[\w]+)\s*=\s*([\s\S]*)$)re");
    JsMap<Value> scope;
    std::vector<std::string> positional;
    for (const auto& a : raw_args) {
      auto named = kNamed.exec(a);
      bool is_param = named && std::any_of(fn.params.begin(), fn.params.end(),
                                           [&](const Param& p) { return p.name == named->view(1); });
      if (is_param) scope.set(named->str(1), arg(named->str(2), line));
      else positional.push_back(a);
    }
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
      const Param& p = fn.params[i];
      if (scope.has(p.name)) continue;
      if (i < positional.size() && !positional[i].empty()) scope.set(p.name, arg(positional[i], line));
      else scope.set(p.name, p.fallback ? arg(*p.fallback, line) : Value::of(std::string()));
    }
    scopes_.push_back(std::move(scope));
    ++depth_;
    std::optional<Value> out;
    if (fn.inline_return) {
      out = evaluate(*fn.inline_return, line);
    } else {
      std::vector<SourceLine> body;
      for (const auto& text : fn.body) body.push_back({text, line});
      out = run_lines(body, is_function);
    }
    --depth_;
    scopes_.pop_back();
    return out;
  }

  Value arg(const std::string& text, int line) {
    static const Regex kDouble(R"re(^"(?:[^"\\]|\\.)*"$)re");
    static const Regex kSingle("^'[^']*'$");
    static const Regex kNumber("^-?\\d+(\\.\\d+)?$");
    static const Regex kVariable("^[$%][\\w]+");
    std::string t = trim(text);
    if (kDouble.test(t) || kSingle.test(t)) return Value::of(t.substr(1, t.size() - 2));
    if (kNumber.test(t)) return Value::of(js_to_number(t));
    if (kVariable.test(t)) return evaluate(t, line);
    return Value::of(t);
  }

  /// Replaces variables, function calls and macros in a text line.
  std::string substitute(const std::string& text, int line) {
    std::string out = text;
    // User functions and builtins: $f(...) / %f(...) / f(...) when f is a known function.
    static const Regex kCall(R"re(([$%][A-Za-z_]\w*|[A-Za-z_]\w*)\s*\()re", "g");
    for (int guard = 0; guard < 50; ++guard) {
      bool found = false;
      std::size_t from = 0;
      while (auto match = kCall.exec(out, from)) {
        from = match->end();
        std::string name = match->str(1);
        const Callable* fn = callables_.get(name);
        bool known = name[0] == '%' || (fn && fn->function);
        if (!known) continue;
        std::size_t open = match->end() - 1;
        auto close = closing_paren(out, open);
        if (!close) continue;
        std::vector<std::string> args;
        for (const auto& a : split_args(std::string_view(out).substr(open + 1, *close - open - 1)))
          args.push_back(arg(a, line).str());
        std::string value = call(name, args, line).str();
        out = out.substr(0, match->index()) + value + out.substr(*close + 1);
        found = true;
        break;
      }
      if (!found) break;
    }
    // Variables.
    static const Regex kVariable("\\$[A-Za-z_]\\w*", "g");
    out = kVariable.replace(
        out,
        [&](const js::Match& m) {
          const Value* v = get(m.view());
          return v ? v->str() : m.str();
        },
        true);
    // !define macros.
    for (const auto& name : std::vector<std::string>(defines_.order)) {
      const Define* def = defines_.get(name);
      if (!def || out.find(name) == std::string::npos) continue;
      if (def->params) {
        Regex re("\\b" + name + "\\s*\\(", "g");
        std::size_t from = 0;
        while (auto match = re.exec(out, from)) {
          std::size_t open = match->end() - 1;
          auto close = closing_paren(out, open);
          if (!close) break;
          auto args = split_args(std::string_view(out).substr(open + 1, *close - open - 1));
          std::string body = def->body;
          for (std::size_t i = 0; i < def->params->size(); ++i) {
            std::string p = (*def->params)[i];
            std::string escaped;
            for (char c : p) {
              if (c == '$') escaped += "\\$";
              else escaped.push_back(c);
            }
            Regex param("\\b" + escaped + "\\b", "g");
            body = param.replace(body, unquote_both(i < args.size() ? args[i] : ""), true);
          }
          out = out.substr(0, match->index()) + body + out.substr(*close + 1);
          from = match->index() + body.size();
        }
      } else {
        std::string escaped;
        for (char c : name) {
          if (c == '$') escaped += "\\$";
          else escaped.push_back(c);
        }
        Regex re("(?<![\\w$])" + escaped + "(?![\\w])", "g");
        out = re.replace(out, def->body, true);
      }
    }
    return out;
  }

  // ---------------------------------------------------------------- includes

  std::vector<SourceLine> include(const std::string& target, int line, const std::optional<std::string>& sub = {}) {
    std::string t = trim(target);
    auto add_library = [&](const std::string& lib) {
      if (std::find(result.libraries.begin(), result.libraries.end(), lib) == result.libraries.end())
        result.libraries.push_back(lib);
    };
    if (is_c4_include(t)) {
      add_library("C4");
      return {};
    }
    for (const auto& [re, lib] : library_patterns("plantuml-include"))
      if (re.test(t)) {
        add_library(lib);
        return {};
      }
    static const Regex kStdlib("^<.*>$");
    static const Regex kRemote("^https?:\\/\\/", "i");
    if (kStdlib.test(t)) {
      result.unresolved.push_back(t);
      result.issues.push_back({line, "Unknown stdlib include " + t});
      return {};
    }
    if (kRemote.test(t)) {
      result.unresolved.push_back(t);
      result.issues.push_back({line, "Remote include not fetched: " + t});
      return {};
    }
    std::optional<std::string> text;
    if (options_.resolve)
      if (auto files = options_.resolve(t, options_.file_name); files && !files->empty()) text = (*files)[0].text;
    // A relative path into a known icon library that is not on disk: the library, by name.
    if (!text)
      for (const auto& [re, lib] : library_patterns("plantuml-local"))
        if (re.test(t)) {
          add_library(lib);
          return {};
        }
    if (!text) {
      result.unresolved.push_back(t);
      result.issues.push_back({line, "Include not found: " + t});
      return {};
    }
    std::string key = t + "!" + sub.value_or("");
    if (included_.count(key)) return {};
    included_.insert(key);
    std::vector<std::string> lines = split_lines(*text);
    if (sub) {
      Regex start_re("^\\s*!startsub\\s+" + *sub + "\\b");
      static const Regex kEnd("^\\s*!endsub\\b");
      std::optional<std::size_t> start, end;
      for (std::size_t i = 0; i < lines.size(); ++i)
        if (start_re.test(lines[i])) {
          start = i;
          break;
        }
      for (std::size_t i = 0; i < lines.size(); ++i)
        if ((!start || i > *start) && kEnd.test(lines[i])) {
          end = i;
          break;
        }
      if (!start) lines.clear();
      else
        lines = std::vector<std::string>(lines.begin() + static_cast<std::ptrdiff_t>(*start + 1),
                                         end ? lines.begin() + static_cast<std::ptrdiff_t>(std::max(*end, *start + 1))
                                             : lines.end());
    }
    // Included files carry their own comments and markers.
    static const Regex kMarker("^\\s*@(start|end)\\w+");
    std::vector<SourceLine> out;
    for (auto& l : strip_included_comments(lines))
      if (!kMarker.test(l)) out.push_back({std::move(l), line});
    return out;
  }

  // ---------------------------------------------------------------- the main loop

  /// Processes lines; returns the value of `!return` when running a function body.
  std::optional<Value> run_lines(const std::vector<SourceLine>& lines, bool is_function) {
    struct Cond {
      bool parent, taken, active;
    };
    std::vector<Cond> conds;
    auto active = [&] { return conds.empty() || conds.back().active; };
    static const Regex kIf(R"re(^!(if|ifdef|ifndef)\b\s*(.*)$)re");
    static const Regex kElseIf(R"re(^!elseif\b\s*(.*)$)re");
    static const Regex kElse(R"re(^!else\b)re");
    static const Regex kEndIf(R"re(^!end\s*if\b)re");
    static const Regex kCallable(
        R"re(^!(?:unquoted\s+)?(?:final\s+)?(function|procedure)\s+([$%]?\w+)\s*\(([^)]*)\)\s*(.*)$)re");
    static const Regex kEndFunction(R"re(^!end\s*function\b)re");
    static const Regex kEndProcedure(R"re(^!end\s*procedure\b)re");
    static const Regex kDefineLong(R"re(^!definelong\s+(\w+)(?:\(([^)]*)\))?\s*$)re");
    static const Regex kEndDefineLong(R"re(^!enddefinelong\b)re");
    static const Regex kDefine(R"re(^!define\s+(\w+)(?:\(([^)]*)\))?\s*(.*)$)re");
    static const Regex kUndef(R"re(^!undef\s+(\w+))re");
    static const Regex kAssign(R"re(^!(global\s+|local\s+)?(\$?\w+)\s*(\?)?=\s*(.*)$)re");
    static const Regex kReturn(R"re(^!return\b\s*(.*)$)re");
    static const Regex kInclude(R"re(^!(include|include_many|include_once|includeurl|import)\s+(.+)$)re");
    static const Regex kIncludeSub(R"re(^!includesub\s+(.+)!(\w+)\s*$)re");
    static const Regex kIgnored(R"re(^!(startsub|endsub|pragma|assert|log|dump_memory|option|exit)\b)re");
    static const Regex kTheme(R"re(^!theme\s+(.+)$)re");
    static const Regex kProcedureCall(R"re(^\s*([$]?\w+)\s*\(([\s\S]*)\)\s*$)re");

    for (std::size_t i = 0; i < lines.size(); ++i) {
      const std::string& raw = lines[i].text;
      const int line = lines[i].line;
      std::string text = trim(raw);

      if (auto m = kIf.exec(text)) {
        bool parent = active();
        bool value = false;
        if (parent) {
          if (m->view(1) == "if") {
            value = truthy(evaluate(m->str(2), line));
          } else {
            std::string name = trim(m->view(2));
            bool defined = defines_.has(name) || get(name) || get("$" + name);
            value = m->view(1) == "ifdef" ? defined : !defined;
          }
        }
        conds.push_back({parent, value, parent && value});
        continue;
      }
      if (auto m = kElseIf.exec(text)) {
        if (!conds.empty()) {
          Cond& c = conds.back();
          bool v = c.parent && !c.taken && truthy(evaluate(m->str(1), line));
          c.active = v;
          c.taken = c.taken || v;
        }
        continue;
      }
      if (kElse.test(text)) {
        if (!conds.empty()) {
          Cond& c = conds.back();
          c.active = c.parent && !c.taken;
          c.taken = true;
        }
        continue;
      }
      if (kEndIf.test(text)) {
        if (!conds.empty()) conds.pop_back();
        continue;
      }
      if (!active()) continue;

      // Definitions with bodies.
      if (auto m = kCallable.exec(text)) {
        std::vector<Param> params;
        for (const auto& p : split_args(m->view(3))) {
          if (p.empty()) continue;
          auto eq = p.find('=');
          if (eq == std::string::npos) params.push_back({trim(p), std::nullopt});
          else params.push_back({trim(p.substr(0, eq)), trim(p.substr(eq + 1))});
        }
        std::string tail = trim(m->view(4));
        if (tail.rfind("!return", 0) == 0) {
          callables_.set(m->str(2), Callable{true, params, {}, trim(std::string_view(tail).substr(7))});
          continue;
        }
        bool function = m->view(1) == "function";
        const Regex& end = function ? kEndFunction : kEndProcedure;
        std::vector<std::string> body;
        while (++i < lines.size() && !end.test(trim(lines[i].text))) body.push_back(lines[i].text);
        callables_.set(m->str(2), Callable{function, params, body, std::nullopt});
        continue;
      }
      if (auto m = kDefineLong.exec(text)) {
        std::vector<std::string> body;
        while (++i < lines.size() && !kEndDefineLong.test(trim(lines[i].text))) body.push_back(lines[i].text);
        if (m->has(2)) {
          std::vector<Param> params;
          for (const auto& p : split(m->view(2), ','))
            if (!js_trim(p).empty()) params.push_back({trim(p), std::nullopt});
          callables_.set(m->str(1), Callable{false, params, body, std::nullopt});
        } else {
          defines_.set(m->str(1), Define{std::nullopt, join(body, "\n")});
        }
        continue;
      }
      if (auto m = kDefine.exec(text)) {
        Define d;
        if (m->has(2)) {
          d.params.emplace();
          for (const auto& p : split(m->view(2), ','))
            if (!js_trim(p).empty()) d.params->push_back(trim(p));
        }
        d.body = m->str(3);
        defines_.set(m->str(1), std::move(d));
        continue;
      }
      if (auto m = kUndef.exec(text)) {
        defines_.erase(m->view(1));
        continue;
      }
      if (auto m = kAssign.exec(text)) {
        std::string name = m->view(2).front() == '$' ? m->str(2) : "$" + m->str(2);
        if (m->has(3) && get(name)) continue;
        std::string scope = m->has(1) ? trim(m->view(1)) : "";
        std::string where = scope == "global" ? "global" : scope == "local" ? "local" : "auto";
        set(name, evaluate(m->str(4), line), where);
        continue;
      }
      if (auto m = kReturn.exec(text)) {
        if (is_function) return evaluate(m->str(1), line);
        continue;
      }
      if (auto m = kInclude.exec(text)) {
        std::string target = substitute(trim(m->view(2)), line);
        auto included = include(target, line);
        if (!included.empty()) run_lines(included, false);
        continue;
      }
      if (auto m = kIncludeSub.exec(text)) {
        auto included = include(substitute(trim(m->view(1)), line), line, m->str(2));
        if (!included.empty()) run_lines(included, false);
        continue;
      }
      if (kIgnored.test(text)) continue;
      if (auto m = kTheme.exec(text)) {
        result.issues.push_back({line, "Theme not applied: " + trim(m->view(1))});
        continue;
      }
      if (!text.empty() && text[0] == '!') {
        result.issues.push_back({line, "Unknown preprocessor directive: " + first_word(text)});
        continue;
      }

      if (!text.empty() && text[0] == '\'') continue;
      // A text line: substitute, then expand a procedure call.
      std::string out = substitute(raw, line);
      if (auto c = kProcedureCall.exec(out)) {
        const Callable* proc = callables_.get(c->view(1));
        if (proc && !proc->function) {
          Callable copy = *proc;
          invoke(copy, split_args(c->view(2)), line, false);
          continue;
        }
      }
      for (auto& l : split(out, '\n')) result.lines.push_back({std::move(l), line});
    }
    return std::nullopt;
  }

  const PlantUmlOptions& options_;
  JsMap<Value> globals_;
  JsMap<Callable> callables_;
  JsMap<Define> defines_;
  std::set<std::string, std::less<>> included_;
  std::vector<JsMap<Value>> scopes_;
  int depth_ = 0;
};

}  // namespace

std::vector<std::string> split_args(std::string_view text) {
  std::vector<std::string> out;
  int depth = 0;
  char quote = 0;
  std::string current;
  for (char ch : text) {
    if (quote) {
      current.push_back(ch);
      if (ch == quote) quote = 0;
    } else if (ch == '"' || ch == '\'') {
      quote = ch;
      current.push_back(ch);
    } else if (ch == '(' || ch == '[' || ch == '{') {
      ++depth;
      current.push_back(ch);
    } else if (ch == ')' || ch == ']' || ch == '}') {
      --depth;
      current.push_back(ch);
    } else if (ch == ',' && depth == 0) {
      out.push_back(trim(current));
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!js_trim(current).empty() || !out.empty()) out.push_back(trim(current));
  return out;
}

PreprocessResult preprocess(std::string_view source, const PlantUmlOptions& options) {
  Preprocessor p(options);
  p.run(source);
  return std::move(p.result);
}

}  // namespace dd::io::plantuml
