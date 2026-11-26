#pragma once

// Layout geometry as JSON, in the shapes of the TypeScript `Geometry` types: what the WASM module
// and the C ABI hand their callers.

#include "diadraw/base/json.hpp"
#include "diadraw/layout/types.hpp"

namespace dd {

/// `{ x, y, width, height }`.
Json to_json(const Box& box);
/// `{ points: [{ x, y }], label?: box }`.
Json to_json(const EdgeRoute& route);
/// `{ width, height, nodes: { id: box }, edges: { id: route }, rotations: { id: degrees },
/// ignoredHints: [hint] }`. Sequence and decoration geometry are left out.
Json to_json(const Geometry& geometry);

}  // namespace dd
