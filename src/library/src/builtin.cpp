#include "diadraw/library/builtin.hpp"

#include <map>

namespace dd::library {

namespace {

// The JSON text in pieces, joined at first use.
constexpr const char* kPieces[] = {
#include "builtin_libraries.inc"
};

}  // namespace

const Json& builtin_bundles() {
  static const Json bundles = [] {
    std::string text;
    for (const char* piece : kPieces) text += piece;
    auto parsed = Json::parse(text);
    return parsed ? *parsed : Json::array();
  }();
  return bundles;
}

const Json* builtin_bundle(std::string_view name) {
  for (const auto& b : builtin_bundles().as_array()) {
    const Json* n = b.get("name");
    if (n && n->is_string() && n->as_string() == name) return &b;
  }
  return nullptr;
}

const std::set<std::string, std::less<>>& builtin_shape_ids(std::string_view name) {
  static std::map<std::string, std::set<std::string, std::less<>>, std::less<>> cache;
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  std::set<std::string, std::less<>> ids;
  if (const Json* b = builtin_bundle(name))
    if (const Json* shapes = b->get("shapes"); shapes && shapes->is_object())
      for (const auto& [id, _] : shapes->as_object()) ids.insert(id);
  return cache.emplace(std::string(name), std::move(ids)).first->second;
}

}  // namespace dd::library
