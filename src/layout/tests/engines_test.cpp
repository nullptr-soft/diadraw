// The tree, grid, radial and force engines, and the warning for a layout name without an engine.

#include <gtest/gtest.h>

#include <cmath>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;

namespace {

Geometry lay_out(const std::string& source, const std::string& view_id) {
  auto parsed = dsl::parse(source);
  EXPECT_TRUE(parsed.diagnostics.empty());
  auto view = dd::test::ok(resolve_view(parsed.model, view_id));
  return dd::test::ok(layout(view, create_measure()));
}

bool apart(const Box& a, const Box& b) {
  return a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y;
}

bool inside(const Box& inner, const Box& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

Point mid(const Box& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

/// Every pair of the given nodes is apart, every edge has a route, and everything is on the canvas.
void expect_tidy(const Geometry& g, const std::vector<std::string>& ids) {
  for (std::size_t i = 0; i < ids.size(); ++i)
    for (std::size_t j = i + 1; j < ids.size(); ++j)
      EXPECT_TRUE(apart(*g.nodes.find(ids[i]), *g.nodes.find(ids[j]))) << ids[i] << " overlaps " << ids[j];
  for (const auto& b : g.nodes) {
    EXPECT_GE(b.x, 0);
    EXPECT_GE(b.y, 0);
    EXPECT_LE(b.x + b.width, g.width);
    EXPECT_LE(b.y + b.height, g.height);
  }
  for (const auto& r : g.edges) EXPECT_GE(r.points.size(), 2u);
}

const char* kOrg = R"(model {
  ceo = process "CEO"
  cto = process "CTO"
  cfo = process "CFO"
  dev = process "Developers"
  ops = process "Operations"
  acc = process "Accounting"
  ceo -> cto "runs"
  ceo -> cfo
  cto -> dev
  cto -> ops
  cfo -> acc
  ops -> acc
}
)";

std::string org(const std::string& layout) { return std::string(kOrg) + "view v { include * layout " + layout + " }"; }

const std::vector<std::string> kOrgIds{"ceo", "cto", "cfo", "dev", "ops", "acc"};

}  // namespace

TEST(LayoutAlgorithms, AnUnknownNameWarnsAndRunsLayered) {
  auto parsed = dsl::parse(org("circular"));
  ASSERT_EQ(parsed.diagnostics.size(), 1u);
  EXPECT_EQ(parsed.diagnostics[0].severity, dsl::Severity::Warning);
  EXPECT_TRUE(dd::test::contains(parsed.diagnostics[0].message, "Unknown layout \"circular\""));
  auto view = dd::test::ok(resolve_view(parsed.model, "v"));
  auto unknown = dd::test::ok(layout(view, create_measure()));
  EXPECT_EQ(unknown.nodes.find("acc")->y, lay_out(org("layered"), "v").nodes.find("acc")->y);
  for (std::string_view name : kLayoutAlgorithms) EXPECT_TRUE(is_layout_algorithm(name));
}

TEST(TreeLayout, PutsChildrenOneRankBelowTheirParentCentredUnderIt) {
  auto g = lay_out(org("tree"), "v");
  expect_tidy(g, kOrgIds);
  const Box& ceo = *g.nodes.find("ceo");
  const Box& cto = *g.nodes.find("cto");
  const Box& cfo = *g.nodes.find("cfo");
  EXPECT_GT(cto.y, ceo.y + ceo.height);
  EXPECT_EQ(cto.y, cfo.y);
  EXPECT_LT(cto.x, cfo.x);  // edge order
  EXPECT_DOUBLE_EQ(mid(ceo).x, (mid(cto).x + mid(cfo).x) / 2);
  // acc is reached through cfo first; ops -> acc is not a tree edge.
  EXPECT_GT(g.nodes.find("acc")->y, cfo.y + cfo.height);
  // Tree edges go down, across and down; the label sits on the last stretch.
  const auto& runs = *g.edges.find("ceo->cto");
  ASSERT_EQ(runs.points.size(), 4u);
  EXPECT_EQ(runs.points.front(), (Point{mid(ceo).x, ceo.y + ceo.height}));
  EXPECT_EQ(runs.points.back(), (Point{mid(cto).x, cto.y}));
  ASSERT_TRUE(runs.label.has_value());
  EXPECT_DOUBLE_EQ(mid(*runs.label).x, mid(cto).x);
  EXPECT_TRUE(g.ignored_hints.empty());
}

TEST(TreeLayout, FollowsTheDirection) {
  auto g = lay_out(std::string(kOrg) + "view v { include * layout tree direction right }", "v");
  expect_tidy(g, kOrgIds);
  const Box& ceo = *g.nodes.find("ceo");
  EXPECT_GT(g.nodes.find("cto")->x, ceo.x + ceo.width);
  auto up = lay_out(std::string(kOrg) + "view v { include * layout tree direction up }", "v");
  EXPECT_LT(up.nodes.find("cto")->y, up.nodes.find("ceo")->y);
}

TEST(GridLayout, FillsRowsOfTheSquareRootInViewOrder) {
  auto g = lay_out(org("grid"), "v");
  expect_tidy(g, kOrgIds);
  // Six members: rows of three.
  EXPECT_EQ(g.nodes.find("ceo")->y, g.nodes.find("cfo")->y);
  EXPECT_LT(g.nodes.find("ceo")->x, g.nodes.find("cto")->x);
  EXPECT_GT(g.nodes.find("dev")->y, g.nodes.find("ceo")->y);
  EXPECT_EQ(g.nodes.find("dev")->x, g.nodes.find("ceo")->x);
  auto columns = lay_out(std::string(kOrg) + "view v { include * layout grid direction right spacing 10 }", "v");
  EXPECT_EQ(columns.nodes.find("ceo")->x, columns.nodes.find("cfo")->x);
  EXPECT_EQ(columns.nodes.find("cto")->y - columns.nodes.find("ceo")->y, columns.nodes.find("ceo")->height + 10);
}

TEST(RadialLayout, PutsTheBestConnectedNodeInTheMiddle) {
  auto g = lay_out(R"(model {
  hub = process "Hub"
  a = process "A"
  b = process "B"
  c = process "C"
  d = process "D"
  e = process "E"
  hub -> a
  hub -> b
  c -> hub
  hub -> d
  d -> e
}
view v { include * layout radial })",
                   "v");
  expect_tidy(g, {"hub", "a", "b", "c", "d", "e"});
  Point hub = mid(*g.nodes.find("hub"));
  auto dist = [&](const char* id) {
    Point p = mid(*g.nodes.find(id));
    return std::hypot(p.x - hub.x, p.y - hub.y);
  };
  EXPECT_NEAR(dist("a"), dist("b"), 0.02);
  EXPECT_NEAR(dist("a"), dist("d"), 0.02);
  EXPECT_GT(dist("e"), dist("d") + 60);
  // Straight edges.
  EXPECT_EQ(g.edges.find("hub->a")->points.size(), 2u);
}

TEST(ForceLayout, KeepsNodesApartAndIsRepeatable) {
  auto g = lay_out(org("force"), "v");
  expect_tidy(g, kOrgIds);
  auto again = lay_out(org("force"), "v");
  for (const auto& id : kOrgIds) EXPECT_EQ(*g.nodes.find(id), *again.nodes.find(id));
  // Connected nodes end up closer than the far ends of the graph.
  Point ceo = mid(*g.nodes.find("ceo")), cto = mid(*g.nodes.find("cto")), acc = mid(*g.nodes.find("acc"));
  Point dev = mid(*g.nodes.find("dev"));
  EXPECT_LT(std::hypot(ceo.x - cto.x, ceo.y - cto.y), std::hypot(dev.x - acc.x, dev.y - acc.y) + 1);
}

TEST(LayoutAlgorithms, EveryEngineWrapsContainersAroundTheirChildren) {
  for (const char* algorithm : {"tree", "grid", "radial", "force"}) {
    SCOPED_TRACE(algorithm);
    auto g = lay_out(std::string(R"(model {
  web = person "Visitor"
  shop = system "Shop" {
    ui = container "UI"
    api = container "API"
    db = container "DB"
  }
  mail = system "Mail"
  web -> shop.ui
  shop.ui -> shop.api
  shop.api -> shop.db
  shop.api -> mail
}
view v { include web shop shop.ui shop.api shop.db mail layout )") +
                         algorithm + " }",
                     "v");
    expect_tidy(g, {"web", "shop", "mail"});
    expect_tidy(g, {"shop.ui", "shop.api", "shop.db"});
    for (const char* child : {"shop.ui", "shop.api", "shop.db"})
      EXPECT_TRUE(inside(*g.nodes.find(child), *g.nodes.find("shop"))) << child;
    EXPECT_TRUE(g.edges.contains("shop.api->mail"));
  }
}

TEST(LayoutAlgorithms, HintsTheEngineMissesAreAppliedAfterwards) {
  auto g = lay_out(std::string(kOrg) + "view v { include * layout grid\n pin acc above ceo }", "v");
  EXPECT_LE(g.nodes.find("acc")->y + g.nodes.find("acc")->height, g.nodes.find("ceo")->y + 1);
}
