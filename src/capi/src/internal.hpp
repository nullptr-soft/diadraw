#pragma once

// What the two halves of the C ABI share: the `dd_document` handle and the helpers that keep C++
// (exceptions, allocation) from crossing into C. Private to src/capi.

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string_view>

#include "diadraw/diadraw.h"
#include "diadraw/model/history.hpp"

struct dd_document {
  dd::History history;
};

namespace dd::capi {

/// A malloc'd copy of `s` with a NUL after it, for the caller to free with `dd_string_free`;
/// NULL when out of memory.
inline char* copy_bytes(std::string_view s) noexcept {
  auto* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (!out) return nullptr;
  if (!s.empty()) std::memcpy(out, s.data(), s.size());
  out[s.size()] = '\0';
  return out;
}

inline void set_error(char** error, std::string_view message) noexcept {
  if (error) *error = copy_bytes(message);
}

/// Runs `body`, turning an exception from the core (malformed input it reports by throwing, or
/// running out of memory) into `on_failure` and an error message, so nothing unwinds into C.
template <typename T, typename Body>
T guarded(char** error, T on_failure, Body&& body) noexcept {
  try {
    return body();
  } catch (const std::exception& e) {
    set_error(error, e.what());
  } catch (...) {
    set_error(error, "unknown error");
  }
  return on_failure;
}

}  // namespace dd::capi
