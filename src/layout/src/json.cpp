#include "diadraw/layout/json.hpp"

#include <utility>

#include "diadraw/model/json.hpp"

namespace dd {

Json to_json(const Box& box) {
  return Json(Json::Object{{"x", box.x}, {"y", box.y}, {"width", box.width}, {"height", box.height}});
}

Json to_json(const EdgeRoute& route) {
  Json points = Json::array();
  for (const auto& p : route.points) points.push(Json(Json::Object{{"x", p.x}, {"y", p.y}}));
  Json out(Json::Object{{"points", std::move(points)}});
  if (route.label) out.set("label", to_json(*route.label));
  return out;
}

Json to_json(const Geometry& geometry) {
  Json nodes = Json::object(), edges = Json::object(), rotations = Json::object(), ignored = Json::array();
  for (const auto& id : geometry.nodes.keys()) nodes.add(id, to_json(*geometry.nodes.find(id)));
  for (const auto& id : geometry.edges.keys()) edges.add(id, to_json(*geometry.edges.find(id)));
  for (const auto& id : geometry.rotations.keys()) rotations.add(id, Json(*geometry.rotations.find(id)));
  for (const auto& hint : geometry.ignored_hints) ignored.push(to_json(hint));
  return Json(Json::Object{{"width", geometry.width},
                           {"height", geometry.height},
                           {"nodes", std::move(nodes)},
                           {"edges", std::move(edges)},
                           {"rotations", std::move(rotations)},
                           {"ignoredHints", std::move(ignored)}});
}

}  // namespace dd
