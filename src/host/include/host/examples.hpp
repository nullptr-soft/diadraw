#pragma once

// File › Examples: the sample documents every app offers (corpus/*.draw, compiled into the
// host library at configure time; the core does not carry them). The web app reads the same
// files (src/apps/web/src/examples.ts), so every app lists the same examples under the same
// names. Choosing one opens it as a new, untitled document.

#include <span>
#include <string_view>

namespace dd::host {

struct Example {
  std::string_view name;    ///< the file name without `.draw`, e.g. "uml-01-class-library"; the menu label
  std::string_view source;  ///< the document's text
};

/// Every example, sorted by name.
[[nodiscard]] std::span<const Example> examples();

/// The example called `name`, or nothing.
[[nodiscard]] const Example* find_example(std::string_view name);

}  // namespace dd::host
