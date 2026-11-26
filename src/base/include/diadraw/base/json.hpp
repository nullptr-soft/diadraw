#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "diadraw/base/result.hpp"

namespace dd {

/// A small JSON value, used at the edges: the C ABI, the WASM bindings and parity tests.
///
/// Objects keep their members in insertion order, as JavaScript objects do, because the order of
/// model maps is meaningful (it is the order elements print in). `dump_sorted()` prints with keys
/// sorted, for canonical comparisons. Equality ignores member order. Numbers are doubles, printed
/// the way JavaScript prints them.
class Json {
 public:
  using Array = std::vector<Json>;
  using Member = std::pair<std::string, Json>;
  using Object = std::vector<Member>;

  Json() = default;  // null
  Json(std::nullptr_t) {}
  Json(bool b) : v_(b) {}
  Json(double d) : v_(d) {}
  Json(int i) : v_(static_cast<double>(i)) {}
  Json(std::int64_t i) : v_(static_cast<double>(i)) {}
  Json(std::size_t i) : v_(static_cast<double>(i)) {}
  Json(const char* s) : v_(std::string(s)) {}
  Json(std::string s) : v_(std::move(s)) {}
  Json(std::string_view s) : v_(std::string(s)) {}
  Json(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
  Json(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

  static Json array() { return Json(Array{}); }
  static Json object() { return Json(Object{}); }

  [[nodiscard]] bool is_null() const { return v_.index() == 0; }
  [[nodiscard]] bool is_bool() const { return v_.index() == 1; }
  [[nodiscard]] bool is_number() const { return v_.index() == 2; }
  [[nodiscard]] bool is_string() const { return v_.index() == 3; }
  [[nodiscard]] bool is_array() const { return v_.index() == 4; }
  [[nodiscard]] bool is_object() const { return v_.index() == 5; }

  [[nodiscard]] bool as_bool() const { return std::get<1>(v_); }
  [[nodiscard]] double as_number() const { return std::get<2>(v_); }
  [[nodiscard]] const std::string& as_string() const { return std::get<3>(v_); }
  [[nodiscard]] const Array& as_array() const { return *std::get<4>(v_); }
  [[nodiscard]] const Object& as_object() const { return *std::get<5>(v_); }
  Array& as_array() { return mutable_array(); }
  Object& as_object() { return mutable_object(); }

  /// Object member, or nullptr when this is not an object or the key is absent.
  [[nodiscard]] const Json* get(std::string_view key) const;
  /// Sets an object member (this must be an object).
  Json& set(std::string key, Json value);
  /// Appends an object member without looking for an existing one: for keys known to be unique,
  /// such as the ids of a model map, where `set` would make building the object quadratic.
  Json& add(std::string key, Json value);
  /// Appends to an array (this must be an array).
  Json& push(Json value);

  [[nodiscard]] std::string dump() const;
  [[nodiscard]] std::string dump_sorted() const;
  /// `JSON.stringify(value, null, indent)`.
  [[nodiscard]] std::string dump_pretty(int indent) const;
  static Result<Json> parse(std::string_view text);

  friend bool operator==(const Json& a, const Json& b);

 private:
  Array& mutable_array();
  Object& mutable_object();
  void dump_to(std::string& out, bool sorted) const;
  void dump_pretty_to(std::string& out, int indent, int depth) const;

  std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>,
               std::shared_ptr<Object>>
      v_;
};

/// Writes a JSON string literal (with quotes) for `s`.
void append_json_string(std::string& out, std::string_view s);
/// Formats a number for JSON: as JavaScript's `String(n)`, with non-finite values as `null`.
std::string format_number(double d);
/// JavaScript's `String(n)`, including `NaN` and `Infinity`.
std::string js_number_string(double d);
/// JavaScript's `Math.round`: halves round up, towards +∞.
double js_round(double x);

}  // namespace dd
