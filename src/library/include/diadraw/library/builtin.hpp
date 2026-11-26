#pragma once

// The libraries that ship with DiaDraw (c4, bpmn, the PlantUML standard-library sets), compiled
// in from assets/builtin/ (tools/gen/builtin_libraries.mjs). The full library module (manifests, resolver, lockfile)
// builds on these.

#include <set>
#include <string>
#include <string_view>

#include "diadraw/base/json.hpp"

namespace dd::library {

/// The built-in bundles, as JSON (an array of `LibraryBundle`).
const Json& builtin_bundles();

/// The bundle named `name`, or nullptr.
const Json* builtin_bundle(std::string_view name);

/// Shape ids of a built-in library (empty when there is no such library).
const std::set<std::string, std::less<>>& builtin_shape_ids(std::string_view name);

}  // namespace dd::library
