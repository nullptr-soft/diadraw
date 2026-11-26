// Port of packages/layout/src/route.test.ts, plus fixed and sequence layout and rendering checks.
// Byte-for-byte agreement with the TypeScript renderer was checked over the corpus (see src/README.md).

#include <chrono>
#include <gtest/gtest.h>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;

namespace {

Box box(double x, double y, double width = 100, double height = 50) { return {x, y, width, height}; }

bool hits(const std::vector<Point>& points, const Box& b) {
  for (std::size_t i = 1; i < points.size(); ++i) {
    const Point& a = points[i - 1];
    const Point& p = points[i];
    if (std::max(a.x, p.x) > b.x && std::min(a.x, p.x) < b.x + b.width && std::max(a.y, p.y) > b.y &&
        std::min(a.y, p.y) < b.y + b.height)
      return true;
  }
  return false;
}

bool orthogonal(const std::vector<Point>& points) {
  for (std::size_t i = 1; i < points.size(); ++i)
    if (points[i].x != points[i - 1].x && points[i].y != points[i - 1].y) return false;
  return true;
}

bool on(const Point& p, const Box& b) { return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height; }

Geometry lay_out(const std::string& source, const std::string& view_id) {
  auto parsed = dsl::parse(source);
  EXPECT_TRUE(parsed.diagnostics.empty());
  auto view = dd::test::ok(resolve_view(parsed.model, view_id));
  return dd::test::ok(layout(view, create_measure()));
}

}  // namespace

TEST(OrthogonalRouter, SimplifiesCollinearPoints) {
  EXPECT_EQ(simplify({{0, 0}, {5, 0}, {10, 0}, {10, 5}}), (std::vector<Point>{{0, 0}, {10, 0}, {10, 5}}));
}

TEST(OrthogonalRouter, GoesStraightWhenNothingIsInTheWay) {
  EXPECT_EQ(shortest_orthogonal({0, 0}, {100, 0}, {}), (std::vector<Point>{{0, 0}, {100, 0}}));
}

TEST(OrthogonalRouter, RoutesAroundAnObstacleWithFewBends) {
  Box wall = box(40, -50, 20, 100);
  auto path = shortest_orthogonal({0, 0}, {100, 0}, {wall});
  ASSERT_TRUE(path.has_value());
  EXPECT_FALSE(hits(*path, wall));
  EXPECT_TRUE(orthogonal(*path));
  EXPECT_EQ(path->size(), 4u);  // up, across, down: two bends
}

TEST(OrthogonalRouter, ConnectsFacingSidesAndAvoidsOtherBoxes) {
  Box a = box(0, 0), b = box(300, 0), blocker = box(150, -20, 60, 90);
  auto route = route_edge(a, b, {blocker});
  EXPECT_TRUE(on(route.front(), a));
  EXPECT_TRUE(on(route.back(), b));
  EXPECT_EQ(route_edge(a, b, {}), (std::vector<Point>{{100, 25}, {300, 25}}));
  EXPECT_FALSE(hits(route, blocker));
  EXPECT_TRUE(orthogonal(route));
}

TEST(OrthogonalRouter, PutsLabelsOnTheLongestSegment) {
  EXPECT_EQ(label_on_route({{0, 0}, {0, 10}, {200, 10}}, {40, 10}), (Box{80, 5, 40, 10}));
}

TEST(LabelAnchor, ReadsDrawiosLabelKeysFromAStyle) {
  EXPECT_FALSE(label_anchor_of(std::nullopt));
  EXPECT_FALSE(label_anchor_of(Style{{"endArrow", "open"}}));
  EXPECT_EQ(label_anchor_of(Style{{"labelAt", "0.25"}}), (LabelAnchor{0.25, 0, {0, 0}, {0.5, 0.5}}));
  EXPECT_EQ(label_anchor_of(Style{{"labelAt", "-0.5,5"}, {"labelOffset", "3,-4"}, {"align", "left"},
                                  {"verticalAlign", "bottom"}}),
            (LabelAnchor{-0.5, 5, {3, -4}, {0, 1}}));
  // Past the ends, along stays on the line; a bad value is ignored.
  EXPECT_EQ(label_anchor_of(Style{{"labelAt", "7"}})->along, 1);
  EXPECT_EQ(label_anchor_of(Style{{"labelAt", "x"}, {"align", "right"}, {"verticalAlign", "top"}}),
            (LabelAnchor{0, 0, {0, 0}, {1, 0}}));
}

TEST(LabelAnchor, PlacesTheLabelAlongTheRouteByLength) {
  const std::vector<Point> line{{0, 0}, {100, 0}};
  EXPECT_EQ(label_at(line, {40, 10}, LabelAnchor{}), (Box{30, -5, 40, 10}));
  // A quarter of the way, above the line with draw.io's 2 px spacing.
  EXPECT_EQ(label_at(line, {40, 10}, LabelAnchor{-0.5, 0, {0, 0}, {0.5, 1}}), (Box{5, -12, 40, 10}));
  // AWAY goes to the left of the direction of travel: up on a line to the right, right on one going
  // down. Halfway along a loop out and back is on its middle leg.
  EXPECT_EQ(label_at(line, {40, 10}, LabelAnchor{0, 5, {0, 0}, {0.5, 0.5}}), (Box{30, -10, 40, 10}));
  const std::vector<Point> loop{{0, 0}, {30, 0}, {30, 30}, {10, 30}};
  EXPECT_EQ(label_at(loop, {40, 10}, LabelAnchor{0, 5, {1, 2}, {0, 0.5}}), (Box{38, 7, 40, 10}));
  // At the ends, and on a route of one point.
  EXPECT_EQ(label_at(loop, {40, 10}, LabelAnchor{1, 0, {0, 0}, {0.5, 0.5}}), (Box{-10, 25, 40, 10}));
  EXPECT_EQ(label_at({{7, 8}}, {40, 10}, LabelAnchor{}), (Box{-13, 3, 40, 10}));
}

TEST(FixedLayout, KeepsPinsWrapsContainersAndReportsUnmetHints) {
  auto g = lay_out(R"(model {
  sys = system "System" {
    a = process "A"
    b = process "B"
  }
  c = process "C"
  sys.a -> sys.b "go"
}
view v {
  include *
  layout fixed
  pin sys.a at 10, 20 size 100, 40
  pin sys.b at 200, 20
  pin c below sys.a
}
)", "v");
  EXPECT_EQ(*g.nodes.find("sys.a"), (Box{10, 20, 100, 40}));
  EXPECT_EQ(g.nodes.find("sys.b")->x, 200);
  // The container wraps its children with 16 px padding and 32 px for its title.
  const Box& sys = *g.nodes.find("sys");
  EXPECT_EQ(sys.x, 10 - 16);
  EXPECT_EQ(sys.y, 20 - 32);
  // `c` is unpinned: it goes in the row below everything pinned, and the `pin … below` holds.
  EXPECT_GT(g.nodes.find("c")->y, 60);
  EXPECT_TRUE(g.ignored_hints.empty());
  const EdgeRoute* edge = g.edges.find("sys.a->sys.b");
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->points.front().x, 110);  // leaves `a` on its right side
  EXPECT_TRUE(edge->label.has_value());
}

TEST(FixedLayout, PlacesAdornmentsOnTheirParentsBoxes) {
  auto g = lay_out(R"(model {
  box = process "Box" {
    icon = process "" style "anchor=1,0;anchorOffset=-27,7" {
      dot = process "" style "anchor=0.5,1;anchorOffset=-2,-4"
    }
    loose = process "" style "anchor=0,1"
    inner = process "Inner"
  }
  wrapper = system "Wrapper" {
    tag = process "" style "anchor=0,0;anchorOffset=4,4"
    member = process "Member"
  }
  odd = system "Odd" {
    a = process "" style "anchor=1"
    b = process "" style "anchor=a,b"
    c = process "" style "anchor=1,0,5"
    d = process "" style "anchor=,"
    e = process "" style "anchor=1e308,0"
    far = process "" style "anchor=1,1;anchorOffset=1e8,0"
  }
  grouped = system "Grouped" {
    group "G" {
      g = process "" style "anchor=1,0"
    }
  }
}
view v {
  include *
  layout fixed
  pin box at 100, 100 size 180, 90
  size box.icon 20, 20
  size box.icon.dot 4, 4
  pin box.inner at 120, 140
  pin box.loose at 0, 0 size 10, 10
  pin wrapper.member at 400, 100 size 100, 40
  size wrapper.tag 8, 8
  pin odd at 600, 100 size 100, 100
  pin odd.a at 600, 300 size 10, 10
  pin odd.b at 620, 300 size 10, 10
  pin odd.c at 640, 300 size 10, 10
  pin odd.d at 660, 300 size 10, 10
  pin odd.e at 680, 300 size 10, 10
  size odd.far 10, 10
}
)", "v");
  EXPECT_EQ(*g.nodes.find("box.icon"), (Box{253, 107, 20, 20}));
  // An adornment can carry one; a pin does not move one off its parent (its size is `size`'s).
  EXPECT_EQ(*g.nodes.find("box.icon.dot"), (Box{261, 123, 4, 4}));
  EXPECT_EQ(g.nodes.find("box.loose")->x, 100);
  EXPECT_EQ(g.nodes.find("box.loose")->y, 190);
  // An unpinned container wraps its other children; its adornment sits on the wrapped box.
  const Box& wrapper = *g.nodes.find("wrapper");
  EXPECT_EQ(wrapper, (Box{400 - 16, 100 - 32, 100 + 32, 40 + 48}));
  EXPECT_EQ(*g.nodes.find("wrapper.tag"), (Box{wrapper.x + 4, wrapper.y + 4, 8, 8}));
  // Malformed anchors are no anchors: those children stay at their pins.
  for (const char* id : {"odd.a", "odd.b", "odd.c", "odd.d", "odd.e"}) EXPECT_EQ(g.nodes.find(id)->y, 300) << id;
  // An offset out of bounds is no offset: the corner itself.
  EXPECT_EQ(*g.nodes.find("odd.far"), (Box{700, 200, 10, 10}));
  // In a group, an element is laid out with the group, not on its parent.
  const Box* grouped = g.nodes.find("grouped.g");
  ASSERT_NE(grouped, nullptr);
  EXPECT_NE(grouped->y, g.nodes.find("grouped")->y);
}

TEST(SequenceLayout, StacksStepsAndClosesOpenActivations) {
  auto g = lay_out(R"(model {
  user = person "User"
  web = container "Web"
}
view login {
  include user web
  layout sequence
  sequence {
    user -> web "Log in"
    activate web
    alt "ok" { web --> user "Welcome" } else "no" { web --> user "Error" }
    note over user web "done"
  }
}
)", "login");
  ASSERT_TRUE(g.sequence.has_value());
  const auto& seq = *g.sequence;
  EXPECT_EQ(seq.lifelines.size(), 2u);
  EXPECT_EQ(seq.messages.size(), 3u);
  ASSERT_EQ(seq.frames.size(), 1u);
  EXPECT_EQ(seq.frames[0].separators.size(), 1u);
  EXPECT_EQ(seq.notes.size(), 1u);
  ASSERT_EQ(seq.activations.size(), 1u);  // never deactivated: closed at the end
  // Each message sits below the one before it.
  EXPECT_LT(seq.messages[0].points[0].y, seq.messages[1].points[0].y);
  EXPECT_LT(seq.messages[1].points[0].y, seq.messages[2].points[0].y);
}

TEST(Render, DrawsNodesEdgesMarkersAndEscapesText) {
  auto parsed = dsl::parse(R"(model {
  a = process "A & <B>"
  b = decision "Ok?"
  a -> b "yes"
}
view v {
  include *
  layout fixed
  pin a at 0, 0
  pin b at 300, 0
}
)");
  auto view = dd::test::ok(resolve_view(parsed.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  std::string svg = render_svg(view, geometry);
  EXPECT_EQ(svg.rfind("<svg xmlns=\"http://www.w3.org/2000/svg\"", 0), 0u);
  EXPECT_NE(svg.find("A &amp; &lt;B&gt;"), std::string::npos);
  EXPECT_NE(svg.find("<polygon points="), std::string::npos);  // the decision is a rhombus
  EXPECT_NE(svg.find("marker-end=\"url(#dd-m-classic-f-475569)\""), std::string::npos);
  EXPECT_NE(svg.find("<marker id=\"dd-m-classic-f-475569\""), std::string::npos);
  EXPECT_EQ(svg.substr(svg.size() - 6), "</svg>");
}

TEST(Render, TextWidthAndWrappingMatchTheTypeScriptHeuristics) {
  EXPECT_DOUBLE_EQ(text_width("il", 10), 6);
  EXPECT_DOUBLE_EQ(text_width("Mw", 10), 17);
  EXPECT_DOUBLE_EQ(text_width("A b", 10), 6.6 + 3 + 5.5);
  EXPECT_EQ(wrap_text("one two three", 40, 13), (std::vector<std::string>{"one", "two", "three"}));
  EXPECT_EQ(wrap_text("a\nb", 1000, 13), (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(wrap_text("  spaced\xC2\xA0out  ", 1000, 13), (std::vector<std::string>{"spaced out"}));
  // A line breaks exactly where text_width says it is too wide.
  double exact = text_width("aa bb", 13);
  EXPECT_EQ(wrap_text("aa bb", exact, 13), (std::vector<std::string>{"aa bb"}));
  EXPECT_EQ(wrap_text("aa bb", exact - 1e-9, 13), (std::vector<std::string>{"aa", "bb"}));
}

TEST(Render, WrappingALongParagraphTakesTimeInItsLength) {
  std::string text;
  for (int i = 0; i < 20000; ++i) text += "word ";
  auto start = std::chrono::steady_clock::now();
  auto lines = wrap_text(text, 1e9, 13);
  auto took = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(lines.size(), 1u);
  // Linear: well under a second even with sanitizers; re-measuring the line per word took minutes.
  EXPECT_LT(took, std::chrono::seconds(2));
}

// ── The layered engine (ports of layout.test.ts, which ran them against ELK) ─────────────────

namespace {

const char* const kLayered = R"(model {
  a = process
  b = process
  c = process
  sys = system {
    x = container
    y = container { port in west }
  }
  a -> b
  b -> c
  sys.x -> sys.y.in
  a -> sys.x
}
view flow {
  include a b c
  layout layered direction right
  pin c below a
}
view all {
  include *
  layout layered direction down
}
)";

bool inside(const Geometry& g, const std::string& child, const std::string& parent) {
  const Box& c = *g.nodes.find(child);
  const Box& p = *g.nodes.find(parent);
  return c.x >= p.x && c.y >= p.y && c.x + c.width <= p.x + p.width && c.y + c.height <= p.y + p.height;
}

Geometry layered(const std::string& view_id, bool constraints = true) {
  auto parsed = dsl::parse(kLayered);
  EXPECT_TRUE(parsed.diagnostics.empty());
  auto view = dd::test::ok(resolve_view(parsed.model, view_id));
  return layered_layout(to_layout_graph(view, create_measure()), constraints ? view.view.hints : std::vector<LayoutHint>{});
}

}  // namespace

TEST(LayeredEngine, LaysOutAChainLeftToRightAndRoutesEveryEdge) {
  Geometry g = layered("flow", false);
  EXPECT_LT(g.nodes.find("a")->x, g.nodes.find("b")->x);
  EXPECT_LT(g.nodes.find("b")->x, g.nodes.find("c")->x);
  ASSERT_NE(g.edges.find("a->b"), nullptr);
  EXPECT_GE(g.edges.find("a->b")->points.size(), 2u);
  EXPECT_GT(g.width, 0);
}

TEST(LayeredEngine, AppliesAPinTheLayeringCannotExpressInTheConstraintPass) {
  // `c` sits two layers after `a`, so layering alone cannot put it below `a`.
  Geometry g = layered("flow");
  EXPECT_TRUE(g.ignored_hints.empty());
  EXPECT_GE(g.nodes.find("c")->y, g.nodes.find("a")->y + g.nodes.find("a")->height);
}

TEST(LayeredEngine, PlacesNestedNodesInsideTheirContainer) {
  Geometry g = layered("all");
  EXPECT_TRUE(inside(g, "sys.x", "sys"));
  EXPECT_TRUE(inside(g, "sys.y", "sys"));
  for (const char* id : {"a->b", "b->c", "sys.x->sys.y.in", "a->sys.x"}) EXPECT_NE(g.edges.find(id), nullptr) << id;
}

TEST(LayeredEngine, EntersAPortOnItsSide) {
  Geometry g = layered("all");
  const Box& y = *g.nodes.find("sys.y");
  const auto& route = g.edges.find("sys.x->sys.y.in")->points;
  EXPECT_DOUBLE_EQ(route.back().x, y.x);  // the west side
}

TEST(LayeredEngine, KeepsEdgesOrthogonalAndNodesApart) {
  Geometry g = layered("all");
  for (const auto& id : g.edges.keys()) EXPECT_TRUE(orthogonal(g.edges.find(id)->points)) << id;
  std::vector<std::string> leaves{"a", "b", "c", "sys.x", "sys.y"};
  for (std::size_t i = 0; i < leaves.size(); ++i)
    for (std::size_t j = i + 1; j < leaves.size(); ++j) {
      const Box& p = *g.nodes.find(leaves[i]);
      const Box& q = *g.nodes.find(leaves[j]);
      bool overlap = p.x < q.x + q.width && q.x < p.x + p.width && p.y < q.y + q.height && q.y < p.y + p.height;
      EXPECT_FALSE(overlap) << leaves[i] << " and " << leaves[j];
    }
}

TEST(LayeredEngine, HonoursOrderHintsAcrossTheFlow) {
  auto parsed = dsl::parse(R"(model {
  root = process
  p = process
  q = process
  r = process
  root -> p
  root -> q
  root -> r
}
view v {
  include *
  order r q p
}
)");
  auto view = dd::test::ok(resolve_view(parsed.model, "v"));
  Geometry g = layered_layout(to_layout_graph(view, create_measure()), view.view.hints, false);
  EXPECT_TRUE(g.ignored_hints.empty());
  EXPECT_LT(g.nodes.find("r")->x, g.nodes.find("q")->x);
  EXPECT_LT(g.nodes.find("q")->x, g.nodes.find("p")->x);
}

TEST(LayeredEngine, BreaksCyclesAndHandlesSelfLoops) {
  auto parsed = dsl::parse(R"(model {
  a = process
  b = process
  c = process
  a -> b
  b -> c
  c -> a "again"
}
view v { include * }
)");
  auto view = dd::test::ok(resolve_view(parsed.model, "v"));
  LayoutGraph graph = to_layout_graph(view, create_measure());
  // Views drop self-loops, but the engine accepts them from other callers.
  graph.edges.push_back({"b->b", "b", "b", std::nullopt, std::nullopt, std::nullopt, EdgeRouting::Default});
  Geometry g = layered_layout(graph, {});
  EXPECT_EQ(g.edges.size(), 4u);
  ASSERT_NE(g.edges.find("c->a"), nullptr);
  EXPECT_TRUE(g.edges.find("c->a")->label.has_value());
  ASSERT_NE(g.edges.find("b->b"), nullptr);
  EXPECT_EQ(g.edges.find("b->b")->points.size(), 4u);
}

#include "diadraw/editor/editor.hpp"

TEST(LayeredLayout, EdgesMeetDiamondAndEllipseOutlinesNotTheirBoxes) {
  dd::editor::Session session(R"(model {
  check = decision "Valid?"
  yes = process "Go on"
  no = process "Stop"
  done = process "Done" shape ellipse
  check -> yes "yes"
  check -> no "no"
  yes -> done
}
view v { include *  layout layered direction down }
)");
  auto r = session.render("v");
  ASSERT_TRUE(r);
  const auto& g = r->geometry;
  const dd::Box& d = *g.nodes.find("check");
  for (const auto& id : g.edges.keys()) {
    const auto& pts = g.edges.find(id)->points;
    if (id.starts_with("check->")) {
      // On the rhombus: |dx|/(w/2) + |dy|/(h/2) == 1.
      double cx = d.x + d.width / 2, cy = d.y + d.height / 2;
      double on = std::abs(pts.front().x - cx) / (d.width / 2) + std::abs(pts.front().y - cy) / (d.height / 2);
      EXPECT_NEAR(on, 1, 0.01) << id;
    }
    if (id == "yes->done") {
      const dd::Box& e = *g.nodes.find("done");
      double dx = (pts.back().x - e.x - e.width / 2) / (e.width / 2), dy = (pts.back().y - e.y - e.height / 2) / (e.height / 2);
      EXPECT_NEAR(dx * dx + dy * dy, 1, 0.02);
    }
  }
}

TEST(Rotation, LinesMeetATurnedBoxOnItsTurnedOutlineInAFixedView) {
  auto g = lay_out(R"(model {
  a = process "A"
  b = process "B"
  a -> b
}
view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 40
  pin b at 300, 0 size 100, 40
  rotate b 90
}
)", "v");
  EXPECT_EQ(*g.rotations.find("b"), 90);
  EXPECT_FALSE(g.rotations.contains("a"));
  // Upright, b's left edge is at x 300; turned a quarter, its outline is 20 either side of 350.
  EXPECT_NEAR(g.edges.find("a->b")->points.back().x, 330, 1e-9);
}

TEST(Rotation, EveryAutoLayoutKeepsSizesAndMakesRoomForTurns) {
  for (const char* algorithm : {"layered", "tree", "grid", "radial", "force"}) {
    auto g = lay_out(std::string(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
  a -> b
  a -> c
}
view v {
  include *
  layout )") + algorithm + R"(
  size a 220, 30
  rotate b 90
}
)", "v");
    SCOPED_TRACE(algorithm);
    EXPECT_EQ(g.nodes.find("a")->width, 220);
    EXPECT_EQ(g.nodes.find("a")->height, 30);
    ASSERT_TRUE(g.rotations.contains("b"));
    // The turned b does not cover a or c.
    Box b = rotated_bounds(*g.nodes.find("b"), 90);
    for (const char* other : {"a", "c"}) {
      const Box& o = *g.nodes.find(other);
      bool overlap = b.x < o.x + o.width && o.x < b.x + b.width && b.y < o.y + o.height && o.y < b.y + b.height;
      EXPECT_FALSE(overlap) << other;
    }
    EXPECT_TRUE(g.ignored_hints.empty());
  }
}

TEST(Rotation, ContainersAndSequenceViewsDoNotTurnAndSay) {
  auto g = lay_out(R"(model {
  s = system "S" {
    a = container "A"
  }
  b = person "B"
}
view v {
  include s s.a
  rotate s 45
}
)", "v");
  EXPECT_FALSE(g.rotations.contains("s"));
  EXPECT_EQ(g.ignored_hints.size(), 1u);
  auto q = lay_out(R"(model {
  s = system "S"
  b = person "B"
}
view q {
  include s b
  layout sequence
  sequence {
    b -> s "hi"
  }
  rotate b 30
  size b 200, 60
}
)", "q");
  EXPECT_TRUE(q.rotations.empty());
  EXPECT_EQ(q.ignored_hints.size(), 1u);
}
