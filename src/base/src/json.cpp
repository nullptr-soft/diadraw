#include "diadraw/base/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "diadraw/base/text.hpp"

namespace dd {

const Json* Json::get(std::string_view key) const {
  if (!is_object()) return nullptr;
  for (const auto& [k, v] : as_object())
    if (k == key) return &v;
  return nullptr;
}

Json::Array& Json::mutable_array() {
  auto& p = std::get<4>(v_);
  if (p.use_count() > 1) p = std::make_shared<Array>(*p);  // copy on write
  return *p;
}

Json::Object& Json::mutable_object() {
  auto& p = std::get<5>(v_);
  if (p.use_count() > 1) p = std::make_shared<Object>(*p);
  return *p;
}

Json& Json::set(std::string key, Json value) {
  auto& obj = mutable_object();
  for (auto& [k, v] : obj)
    if (k == key) {
      v = std::move(value);
      return *this;
    }
  obj.emplace_back(std::move(key), std::move(value));
  return *this;
}

Json& Json::add(std::string key, Json value) {
  mutable_object().emplace_back(std::move(key), std::move(value));
  return *this;
}

Json& Json::push(Json value) {
  mutable_array().push_back(std::move(value));
  return *this;
}

bool operator==(const Json& a, const Json& b) {
  if (a.v_.index() != b.v_.index()) return false;
  switch (a.v_.index()) {
    case 0: return true;
    case 1: return a.as_bool() == b.as_bool();
    case 2: return a.as_number() == b.as_number();
    case 3: return a.as_string() == b.as_string();
    case 4: return a.as_array() == b.as_array();
    default: {
      const auto& x = a.as_object();
      const auto& y = b.as_object();
      if (x.size() != y.size()) return false;
      for (const auto& [k, v] : x) {
        const Json* other = b.get(k);
        if (!other || !(v == *other)) return false;
      }
      return true;
    }
  }
}

// ── Printing ─────────────────────────────────────────────────────────────────────────────────

void append_json_string(std::string& out, std::string_view s) {
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

std::string format_number(double d) {
  if (!std::isfinite(d)) return "null";
  return js_number_string(d);
}

std::string js_number_string(double d) {
  if (std::isnan(d)) return "NaN";
  if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
  if (d == 0) return "0";  // also -0
  std::string sign = d < 0 ? "-" : "";
  double a = std::fabs(d);
  // Shortest round-trip digits in scientific form: "d.ddde±x".
  char buf[64];
  auto [end, ec] = std::to_chars(buf, buf + sizeof buf, a, std::chars_format::scientific);
  std::string sci(buf, end);
  auto e = sci.find('e');
  std::string digits;
  for (std::size_t i = 0; i < e; ++i)
    if (sci[i] != '.') digits.push_back(sci[i]);
  int k = static_cast<int>(digits.size());
  int n = std::atoi(sci.c_str() + e + 1) + 1;  // value = 0.digits × 10^n
  // ECMAScript Number::toString, steps for radix 10.
  if (k <= n && n <= 21) return sign + digits + std::string(static_cast<std::size_t>(n - k), '0');
  if (0 < n && n <= 21) return sign + digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
  if (-6 < n && n <= 0) return sign + "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
  int exp = n - 1;
  std::string mant = k == 1 ? digits : digits.substr(0, 1) + "." + digits.substr(1);
  return sign + mant + "e" + (exp < 0 ? "-" : "+") + std::to_string(std::abs(exp));
}

double js_round(double x) {
  if (!std::isfinite(x)) return x;
  double r = std::floor(x);
  if (x - r >= 0.5) r += 1;
  return r == 0 && x < 0 ? -0.0 : r;
}

void Json::dump_to(std::string& out, bool sorted) const {
  switch (v_.index()) {
    case 0: out += "null"; break;
    case 1: out += as_bool() ? "true" : "false"; break;
    case 2: out += format_number(as_number()); break;
    case 3: append_json_string(out, as_string()); break;
    case 4: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : as_array()) {
        if (!first) out.push_back(',');
        first = false;
        item.dump_to(out, sorted);
      }
      out.push_back(']');
      break;
    }
    default: {
      out.push_back('{');
      bool first = true;
      std::vector<const Member*> members;
      members.reserve(as_object().size());
      for (const auto& m : as_object()) members.push_back(&m);
      if (sorted)
        std::sort(members.begin(), members.end(),
                  [](const Member* x, const Member* y) { return x->first < y->first; });
      for (const Member* m : members) {
        if (!first) out.push_back(',');
        first = false;
        append_json_string(out, m->first);
        out.push_back(':');
        m->second.dump_to(out, sorted);
      }
      out.push_back('}');
    }
  }
}

void Json::dump_pretty_to(std::string& out, int indent, int depth) const {
  auto newline = [&](int d) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent * d), ' ');
  };
  if (is_array()) {
    if (as_array().empty()) {
      out += "[]";
      return;
    }
    out.push_back('[');
    bool first = true;
    for (const auto& item : as_array()) {
      if (!first) out.push_back(',');
      first = false;
      newline(depth + 1);
      item.dump_pretty_to(out, indent, depth + 1);
    }
    newline(depth);
    out.push_back(']');
    return;
  }
  if (is_object()) {
    if (as_object().empty()) {
      out += "{}";
      return;
    }
    out.push_back('{');
    bool first = true;
    for (const auto& [k, v] : as_object()) {
      if (!first) out.push_back(',');
      first = false;
      newline(depth + 1);
      append_json_string(out, k);
      out += ": ";
      v.dump_pretty_to(out, indent, depth + 1);
    }
    newline(depth);
    out.push_back('}');
    return;
  }
  dump_to(out, false);
}

std::string Json::dump_pretty(int indent) const {
  std::string out;
  dump_pretty_to(out, indent, 0);
  return out;
}

std::string Json::dump() const {
  std::string out;
  dump_to(out, false);
  return out;
}

std::string Json::dump_sorted() const {
  std::string out;
  dump_to(out, true);
  return out;
}

// ── Parsing ──────────────────────────────────────────────────────────────────────────────────

namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : s_(text) {}

  Result<Json> document() {
    auto v = value(0);
    if (!v) return v;
    skip_ws();
    if (pos_ != s_.size()) return error("unexpected trailing characters");
    return v;
  }

 private:
  static constexpr int kMaxDepth = 512;

  Error error(std::string_view what) const {
    return fail("JSON: ", what, " at offset ", std::to_string(pos_));
  }

  void skip_ws() {
    while (pos_ < s_.size() &&
           (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t'))
      ++pos_;
  }

  bool consume(std::string_view word) {
    if (s_.substr(pos_, word.size()) != word) return false;
    pos_ += word.size();
    return true;
  }

  Result<Json> value(int depth) {
    if (depth > kMaxDepth) return error("nesting too deep");
    skip_ws();
    if (pos_ >= s_.size()) return error("unexpected end");
    char c = s_[pos_];
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') {
      auto str = string();
      if (!str) return str.error();
      return Json(std::move(*str));
    }
    if (consume("true")) return Json(true);
    if (consume("false")) return Json(false);
    if (consume("null")) return Json(nullptr);
    return number();
  }

  Result<Json> number() {
    std::size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' ||
                                s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' ||
                                s_[pos_] == '-'))
      ++pos_;
    if (start == pos_) return error("unexpected character");
    double d = 0;
    auto [end, ec] = from_chars_double(s_.data() + start, s_.data() + pos_, d);
    if (ec != std::errc() || end != s_.data() + pos_) return error("bad number");
    return Json(d);
  }

  static void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  Result<unsigned> hex4() {
    if (pos_ + 4 > s_.size()) return error("short \\u escape");
    unsigned v = 0;
    auto [end, ec] = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, v, 16);
    if (ec != std::errc() || end != s_.data() + pos_ + 4) return error("bad \\u escape");
    pos_ += 4;
    return v;
  }

  Result<std::string> string() {
    ++pos_;  // opening quote
    std::string out;
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= s_.size()) break;
      char e = s_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          auto hi = hex4();
          if (!hi) return hi.error();
          unsigned cp = *hi;
          if (cp >= 0xD800 && cp <= 0xDBFF && consume("\\u")) {
            auto lo = hex4();
            if (!lo) return lo.error();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (*lo - 0xDC00);
          }
          append_utf8(out, cp);
          break;
        }
        default: return error("bad escape");
      }
    }
    return error("unterminated string");
  }

  Result<Json> array(int depth) {
    ++pos_;
    Json::Array items;
    skip_ws();
    if (consume("]")) return Json(std::move(items));
    while (true) {
      auto item = value(depth + 1);
      if (!item) return item;
      items.push_back(std::move(*item));
      skip_ws();
      if (consume("]")) return Json(std::move(items));
      if (!consume(",")) return error("expected , or ]");
    }
  }

  Result<Json> object(int depth) {
    ++pos_;
    Json::Object members;
    std::unordered_map<std::string, std::size_t> positions;
    skip_ws();
    if (consume("}")) return Json(std::move(members));
    while (true) {
      skip_ws();
      if (pos_ >= s_.size() || s_[pos_] != '"') return error("expected a key");
      auto key = string();
      if (!key) return key.error();
      skip_ws();
      if (!consume(":")) return error("expected :");
      auto v = value(depth + 1);
      if (!v) return v;
      // Like JSON.parse, a repeated key keeps its first position and takes the last value.
      auto [slot, fresh] = positions.try_emplace(*key, members.size());
      if (fresh)
        members.emplace_back(std::move(*key), std::move(*v));
      else
        members[slot->second].second = std::move(*v);
      skip_ws();
      if (consume("}")) return Json(std::move(members));
      if (!consume(",")) return error("expected , or }");
    }
  }

  std::string_view s_;
  std::size_t pos_ = 0;
};

}  // namespace

Result<Json> Json::parse(std::string_view text) { return Parser(text).document(); }

}  // namespace dd
