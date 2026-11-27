#pragma once

#include <gtest/gtest.h>

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include "diadraw/layout/layout.hpp"
#include "diadraw/model/operations.hpp"
#include "diadraw/model/views.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::test {

/// Fails the current test and stops it. For helpers that return a value, where googletest's
/// ASSERT_* macros cannot be used; the test runner reports the exception as well.
[[noreturn]] inline void stop(const std::string& message) {
  ADD_FAILURE() << message;
  throw std::runtime_error(message);
}

/// The value of a result that must succeed.
template <class T>
T ok(Result<T> r) {
  if (!r.ok()) stop(r.error().message);
  return std::move(*r);
}

/// Applies operations to an empty model; fails the test if any is refused.
inline Model build(std::initializer_list<Operation> ops) {
  Model m;
  for (const auto& op : ops) m = ok(apply_operation(m, op)).model;
  return m;
}

/// Applies an operation that must succeed.
inline ApplyResult apply_ok(const Model& m, const Operation& op) { return ok(apply_operation(m, op)); }

/// The error message of an operation that must fail.
inline std::string apply_error(const Model& m, const Operation& op) {
  auto r = apply_operation(m, op);
  if (r.ok()) stop("the operation was expected to fail");
  return r.error().message;
}

/// One view of a model, laid out with the renderer's measure and rendered as SVG.
inline std::string render(const Model& model, std::string_view view_id, const RenderOptions& options = {}) {
  auto view = ok(resolve_view(model, view_id));
  auto geometry = ok(layout(view, create_measure()));
  return render_svg(view, geometry, options);
}

/// The `<g class="cls" data-id="id" …>…` group of one rendered node or edge (turned or not), up
/// to its first `</g>`; empty when the SVG has none.
inline std::string item_svg(const std::string& svg, const std::string& cls, const std::string& id) {
  auto at = svg.find("<g class=\"" + cls + "\" data-id=\"" + id + "\"");
  if (at == std::string::npos) return "";
  return svg.substr(at, svg.find("</g>", at) - at);
}

inline bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace dd::test
