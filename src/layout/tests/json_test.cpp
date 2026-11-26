#include "diadraw/layout/json.hpp"

#include <gtest/gtest.h>

namespace dd {

TEST(LayoutJson, WritesGeometryInTheTypeScriptShape) {
  Geometry g;
  g.width = 200;
  g.height = 100.5;
  g.nodes.set("a", Box{0, 0, 40, 20});
  g.edges.set("r", EdgeRoute{{{40, 10}, {80, 10}}, Box{50, 0, 20, 8}});
  g.edges.set("s", EdgeRoute{{{0, 0}, {1, 1}}, std::nullopt});
  g.rotations.set("a", 30);
  EXPECT_EQ(to_json(g).dump(),
            R"({"width":200,"height":100.5,"nodes":{"a":{"x":0,"y":0,"width":40,"height":20}},)"
            R"("edges":{"r":{"points":[{"x":40,"y":10},{"x":80,"y":10}],"label":{"x":50,"y":0,"width":20,"height":8}},)"
            R"("s":{"points":[{"x":0,"y":0},{"x":1,"y":1}]}},"rotations":{"a":30},"ignoredHints":[]})");
}

}  // namespace dd
