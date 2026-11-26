// Port of packages/editor/src/editor.test.ts, plus the Session facade.

#include "diadraw/editor/editor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>

#include "helpers.hpp"

using namespace dd;
using namespace dd::editor;

namespace {

Box box(double x, double y) { return {x, y, 100, 50}; }

Geometry sample_geometry() {
  Geometry g;
  g.width = 500;
  g.height = 300;
  g.nodes.set("a", box(0, 0));
  g.nodes.set("b", box(200, 0));
  g.nodes.set("c", box(0, 150));
  return g;
}

const std::vector<ElementId> kCandidates{"a", "b", "c"};

Model sample_model() {
  Model m;
  m.elements.set("s", make_element("s", "system"));
  m.elements.set("s.a", make_element("s.a", "container", {}, "s"));
  m.elements.set("b", make_element("b", "process"));
  m.elements.set("c", make_element("c", "process"));
  m.relationships.set("b->c", make_relationship("b->c", {"b", {}}, {"c", {}}));
  m.relationships.set("b->s.a", make_relationship("b->s.a", {"b", {}}, {"s.a", {}}));
  View v = make_view("v");
  v.hints = {PinHint{"c", Placement::Below, "b"}, OrderHint{{"b", "c"}}, AlignHint{{"b", "c"}, Axis::Horizontal}};
  m.views.set("v", v);
  return m;
}

const char* const kSource = R"(model {
  b = process "B"   // start
  c = process "C"
  b -> c
}

view v {
  include *
  pin c below b   // dragged
}
)";

}  // namespace

TEST(HintForDrop, AlignsWhenTheDropLinesUpWithANeighbour) {
  auto g = sample_geometry();
  EXPECT_EQ(hint_for_drop(g, "c", box(205, 80), kCandidates), (LayoutHint{AlignHint{{"b", "c"}, Axis::Vertical}}));
  EXPECT_EQ(hint_for_drop(g, "c", box(400, 5), kCandidates), (LayoutHint{AlignHint{{"b", "c"}, Axis::Horizontal}}));
}

TEST(HintForDrop, PinsBesideTheNearestNeighbourOnTheSideItWasDropped) {
  auto g = sample_geometry();
  EXPECT_EQ(hint_for_drop(g, "c", box(260, 90), kCandidates), (LayoutHint{PinHint{"c", Placement::Below, "b"}}));
  EXPECT_EQ(hint_for_drop(g, "a", box(330, 40), kCandidates), (LayoutHint{PinHint{"a", Placement::RightOf, "b"}}));
}

TEST(HintForDrop, PinsAtCoordinatesInFreeformMode) {
  auto g = sample_geometry();
  EXPECT_EQ(hint_for_drop(g, "a", Box{12.4, 30.6, 100, 50}, kCandidates, true),
            (LayoutHint{PinAtHint{"a", 12, 31, std::nullopt, std::nullopt}}));
}

TEST(Gestures, ReplaceTheHintsThatPlaceTheDraggedElement) {
  Model m = sample_model();
  auto op = dd::test::ok(place_operation(m, "v", "c", PinHint{"c", Placement::RightOf, "b"}));
  Model after = dd::test::apply_ok(m, op).model;
  EXPECT_EQ(after.views.find("v")->hints,
            (std::vector<LayoutHint>{OrderHint{{"b", "c"}}, PinHint{"c", Placement::RightOf, "b"}}));
}

TEST(Gestures, CreateElementsWithFreshIds) {
  Model m = sample_model();
  EXPECT_EQ(fresh_element_id(m, "process"), "process1");
  EXPECT_EQ(create_operation(m, "container", std::string("s")).id, "s.container1");
}

TEST(Gestures, ConnectWithTheIdTheTextWillProduce) {
  auto op = connect_operation(sample_model(), "b", "c");
  EXPECT_EQ(std::get<op::AddRelationship>(op).relationship.id, "b->c#2");
}

TEST(Gestures, DeleteOutermostElementsAndOnlyRelationshipsThatSurviveThem) {
  Model m = sample_model();
  auto op = delete_operation(m, {{"s", "s.a"}, {"b->s.a", "b->c"}});
  ASSERT_TRUE(op.has_value());
  EXPECT_EQ(*op, (Operation{op::Batch{{op::RemoveRelationship{"b->c"}, op::RemoveElement{"s"}}}}));
  EXPECT_FALSE(delete_operation(m, {}).has_value());
}

TEST(Gestures, OfferOnlyLeafNodesAsDropReferences) {
  EXPECT_EQ(drop_candidates(sample_model(), {"s", "s.a", "b"}), (std::vector<ElementId>{"s.a", "b"}));
}

TEST(Session, ADragReplacesTheOldPinWithTheNewHint) {
  Session session(kSource);
  auto op = dd::test::ok(place_operation(session.model(), "v", "c", AlignHint{{"b", "c"}, Axis::Vertical}));
  auto r = dd::test::ok(session.apply(op));
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(dd::test::contains(r.source, "  include *\n  align b c vertically\n}"));
  EXPECT_TRUE(dd::test::contains(r.source, "// start"));
}

TEST(Session, ConnectCreateAndDeleteRoundTripThroughTheText) {
  Session session(kSource);
  EXPECT_TRUE(dd::test::ok(session.connect("c", "b", std::string("back"))).minimal);
  auto [id, created] = dd::test::ok(session.create("decision"));
  EXPECT_EQ(id, "decision1");
  EXPECT_TRUE(created.minimal);
  auto removed = dd::test::ok(session.remove({{"b"}, {}}));
  ASSERT_TRUE(removed.has_value());
  EXPECT_TRUE(removed->minimal);
  EXPECT_EQ(session.source(), R"(model {
  c = process "C"
  decision1 = decision "New decision"
}

view v {
  include *
}
)");
}

TEST(Session, TellsItsListenerAboutEveryChange) {
  Session session(kSource);
  std::vector<std::string> heard;
  session.set_listener([&](const SessionEvent& e) {
    if (e.kind == SessionEvent::Kind::Libraries) {
      heard.push_back("libraries");
      return;
    }
    EXPECT_EQ(session.source() == std::string(e.before), false);
    heard.push_back(e.update ? "update" : "text");
  });
  session.set_source(kSource + std::string("\n"));
  dd::test::ok(session.connect("c", "b"));
  session.refresh_libraries();
  session.set_listener({});
  session.set_source(kSource);
  EXPECT_EQ(heard, (std::vector<std::string>{"text", "update", "libraries"}));
}

TEST(Session, RendersViewsAndTurnsADropIntoAHint) {
  Session session(kSource);
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_TRUE(rendered.geometry.nodes.contains("b"));
  EXPECT_TRUE(dd::test::contains(rendered.scene.content, "data-id=\"b\""));
  const Box b = *rendered.geometry.nodes.find("b");
  // Drop `c` well to the right of `b`, level with it: an alignment.
  auto update = dd::test::ok(session.drop("v", "c", Box{b.x + 400, b.y, b.width, b.height}));
  ASSERT_TRUE(update.has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "align b c horizontally"));
  EXPECT_FALSE(dd::test::contains(session.source(), "pin c below b"));
}

TEST(Session, ADropInADocumentWithoutViewsWritesTheView) {
  // No `view` block: the canvas shows the implied default view, so the drop writes that view.
  Session session("model {\n  user = person \"Customer\"\n  shop = system \"Shop\"\n  user -> shop \"browses\"\n}\n");
  const Box shop = *dd::test::ok(session.render("default")).geometry.nodes.find("shop");
  Box to{shop.x + 300, shop.y + 200, shop.width, shop.height};
  auto update = dd::test::ok(session.drop("default", "shop", to, true));
  ASSERT_TRUE(update.has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "view default {"));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin shop at"));
  EXPECT_TRUE(dd::test::contains(session.source(), "user -> shop \"browses\""));
  const Box after = *dd::test::ok(session.render("default")).geometry.nodes.find("shop");
  EXPECT_NEAR(after.x, to.x, 1);
  EXPECT_NEAR(after.y, to.y, 1);
}

TEST(Session, AnExactDropInALaidOutViewLandsWhereItWasLetGo) {
  // The canvas's plain drag: the node stays where it is dropped, the layout arranges the rest.
  Session session(kSource);
  const Box b = *dd::test::ok(session.render("v")).geometry.nodes.find("b");
  Box to{b.x + 400, b.y + 300, b.width, b.height};
  auto update = dd::test::ok(session.drop("v", "b", to, true));
  ASSERT_TRUE(update.has_value());
  auto rendered = dd::test::ok(session.render("v"));
  const Box after = *rendered.geometry.nodes.find("b");
  EXPECT_NEAR(after.x, to.x, 1);
  EXPECT_NEAR(after.y, to.y, 1);
}

// ── Fixed views: resizing nodes and dragging bend points ──

namespace {

const char* const kFixed = R"(model {
  a = process "A"
  b = process "B"
  pay = a -> b "pays"
  a -> b
}

view v {
  include *
  layout fixed
  pin a at 40, 40   // the source
  pin b at 300, 40 size 120, 60
  route "pay" via 200, 0 from 1, 0.5
  note "Draft" at 0, 200
}
)";

}  // namespace

TEST(FixedView, AResizeWritesTheSizeIntoThePinWhereItStands) {
  Session session(kFixed);
  auto r = dd::test::ok(session.resize("v", "a", Box{30.4, 40, 150.6, 70}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->minimal);
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin a at 30, 40 size 151, 70   // the source\n  pin b"));
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("a"), (Box{30, 40, 151, 70}));
  // The same box again changes nothing.
  EXPECT_FALSE(dd::test::ok(session.resize("v", "a", Box{30, 40, 151, 70})).has_value());
}

TEST(FixedView, AResizeOfAnUnpinnedNodeAddsAPin) {
  Session session(kFixed);
  ASSERT_TRUE(dd::test::ok(session.resize("v", "b", Box{300, 40, 100, 50})).has_value());
  ASSERT_TRUE(dd::test::ok(session.resize("v", "a", Box{0, 0, 80, 40})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "pin b at 300, 40 size 100, 50\n"));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin a at 0, 0 size 80, 40"));
}

TEST(FixedView, FittingDropsThePinnedSizeAndKeepsThePlace) {
  Session session(kFixed);
  const Box natural = *dd::test::ok(session.render("v")).geometry.nodes.find("a");
  ASSERT_TRUE(dd::test::ok(session.resize("v", "a", Box{30, 40, 300, 200})).has_value());
  auto r = dd::test::ok(session.fit("v", {"a", "b"}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin a at 30, 40   // the source\n  pin b at 300, 40\n  route"));
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("a"), (Box{30, 40, natural.width, natural.height}));
  // Already fitted: nothing to change.
  EXPECT_FALSE(dd::test::ok(session.fit("v", {"a", "b"})).has_value());
  EXPECT_FALSE(session.fit("v", {"nope"}).ok());
  EXPECT_FALSE(session.fit("nope", {"a"}).ok());
}

TEST(FixedView, FittingAContainerLetsItWrapItsChildren) {
  Session session(R"(model {
  shop = system "Shop" {
    web = container "Web"
  }
}

view v {
  include *
  layout fixed
  pin shop at 0, 0 size 600, 400
  size shop 500, 500
  pin shop.web at 100, 120
}
)");
  ASSERT_TRUE(dd::test::ok(session.fit("v", {"shop"})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "shop at"));
  EXPECT_FALSE(dd::test::contains(session.source(), "size shop"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin shop.web at 100, 120\n}"));
  auto rendered = dd::test::ok(session.render("v"));
  const Box& shop = *rendered.geometry.nodes.find("shop");
  const Box& web = *rendered.geometry.nodes.find("shop.web");
  EXPECT_LT(shop.x, web.x);
  EXPECT_LT(shop.y, web.y);
  EXPECT_GT(shop.x + shop.width, web.x + web.width);
  EXPECT_GT(shop.y + shop.height, web.y + web.height);
  EXPECT_LT(shop.width, 600);
}

TEST(FixedView, AMoveIsAlwaysExactAndKeepsTheSize) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  // Dropped right next to `a`, which a layered view would turn into an alignment.
  auto r = dd::test::ok(session.drop("v", "b", Box{400.2, 41, 120, 60}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin b at 400, 41 size 120, 60\n  route"));
}

TEST(LaidOutView, AResizeWritesASizeTheLayoutKeeps) {
  Session session(kSource);
  auto r = dd::test::ok(session.resize("v", "b", Box{12, 7, 200.4, 90}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin c below b   // dragged\n  size b 200, 90\n}"));
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(rendered.geometry.nodes.find("b")->width, 200);
  EXPECT_EQ(rendered.geometry.nodes.find("b")->height, 90);
  // Again: rewritten where it stands; the same size changes nothing.
  ASSERT_TRUE(dd::test::ok(session.resize("v", "b", Box{0, 0, 180, 60})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  size b 180, 60\n}"));
  EXPECT_FALSE(dd::test::ok(session.resize("v", "b", Box{0, 0, 180, 60})).has_value());
  // Bend points still need a fixed view.
  EXPECT_FALSE(session.reroute("v", "b->c", {{1, 2}}).ok());
}

TEST(FixedView, FittingAContainerAndAChildDropsTheContainersPinAndTheChildsSize) {
  Session session(R"(model {
  shop = system "Shop" {
    web = container "Web"
  }
}

view v {
  include *
  layout fixed
  pin shop at 0, 0
  pin shop.web at 100, 120 size 300, 200
}
)");
  // A container pinned without a size takes a leaf's size there, so fitting it still changes it.
  ASSERT_TRUE(dd::test::ok(session.fit("v", {"shop", "shop.web"})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "shop at"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin shop.web at 100, 120\n}"));
  EXPECT_FALSE(dd::test::ok(session.fit("v", {"shop", "shop.web"})).has_value());
}

TEST(LaidOutView, FittingRemovesTheSizeInOneStep) {
  Session session(kSource);
  const Box natural = *dd::test::ok(session.render("v")).geometry.nodes.find("b");
  ASSERT_TRUE(dd::test::ok(session.resize("v", "b", Box{0, 0, 300, 200})).has_value());
  std::string sized = session.source();
  // Two sizes for one element (typed by hand): both go, as one update.
  sized.replace(sized.find("  size b 300, 200\n"), 0, "  size b 10, 10\n");
  session.set_source(sized);
  auto r = dd::test::ok(session.fit("v", {"b"}));
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "size b"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin c below b   // dragged\n}"));
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(rendered.geometry.nodes.find("b")->width, natural.width);
  EXPECT_EQ(rendered.geometry.nodes.find("b")->height, natural.height);
  EXPECT_FALSE(dd::test::ok(session.fit("v", {"b", "c"})).has_value());
}

TEST(LaidOutView, ARotationIsWrittenRewrittenAndRemoved) {
  Session session(kSource);
  ASSERT_TRUE(dd::test::ok(session.rotate("v", "b", 30.04)).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  rotate b 30\n}"));
  auto rendered = dd::test::ok(session.render("v"));
  ASSERT_TRUE(rendered.geometry.rotations.find("b"));
  EXPECT_EQ(*rendered.geometry.rotations.find("b"), 30);
  EXPECT_TRUE(dd::test::contains(rendered.scene.content, "transform=\"rotate(30 "));
  ASSERT_TRUE(dd::test::ok(session.rotate("v", "b", 405)).has_value());  // 405° is 45°
  EXPECT_TRUE(dd::test::contains(session.source(), "  rotate b 45\n}"));
  EXPECT_FALSE(dd::test::ok(session.rotate("v", "b", 45)).has_value());
  ASSERT_TRUE(dd::test::ok(session.rotate("v", "b", 360)).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "rotate"));
}

TEST(LaidOutView, ATurnedNodeTakesTheRoomOfItsTurnedShape) {
  Session session(kSource);
  dd::test::ok(session.rotate("v", "b", 90));
  auto r = dd::test::ok(session.render("v"));
  Box b = *r.geometry.nodes.find("b");
  Box c = *r.geometry.nodes.find("c");
  // The box stays the unturned shape; the layout made room for the turned one, so `c` below it
  // starts lower than with an upright `b`.
  Session upright(kSource);
  auto u = dd::test::ok(upright.render("v"));
  EXPECT_EQ(b.width, u.geometry.nodes.find("b")->width);
  EXPECT_GT(c.y - u.geometry.nodes.find("c")->y, (b.width - b.height) / 2 - 1);
  // The line from b to c meets b's turned outline: its bottom edge, turned, is b's half width
  // below the centre.
  const auto& points = r.geometry.edges.find("b->c")->points;
  EXPECT_NEAR(points.front().y, b.y + b.height / 2 + b.width / 2, 0.05);
}

TEST(FixedView, DraggedBendPointsRewriteTheRouteByItsNameAndKeepWhereItLeaves) {
  Session session(kFixed);
  auto r = dd::test::ok(session.reroute("v", "pay", {{210.4, -10}, {250, -10}}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->minimal);
  EXPECT_TRUE(dd::test::contains(session.source(), "  route \"pay\" via 210, -10 250, -10 from 1, 0.5\n  note"));
  EXPECT_FALSE(dd::test::ok(session.reroute("v", "pay", {{210, -10}, {250, -10}})).has_value());
}

TEST(FixedView, AnUnnamedEdgeGetsARouteByItsDerivedId) {
  Session session(kFixed);
  ASSERT_TRUE(dd::test::ok(session.reroute("v", "a->b", {{100, 150}})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "route \"a->b\" via 100, 150\n}"));
  auto rendered = dd::test::ok(session.render("v"));
  const auto& points = rendered.geometry.edges.find("a->b")->points;
  ASSERT_EQ(points.size(), 3u);
  EXPECT_EQ(points[1], (Point{100, 150}));
}

TEST(FixedView, RemovingEveryBendPointDropsAPlainRoute) {
  Session session(kFixed);
  dd::test::ok(session.reroute("v", "a->b", {{100, 150}}));
  ASSERT_TRUE(dd::test::ok(session.reroute("v", "a->b", {})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "route \"a->b\""));
  EXPECT_FALSE(dd::test::ok(session.reroute("v", "a->b", {})).has_value());
  // A route that says where the edge leaves stays, without its bend points.
  ASSERT_TRUE(dd::test::ok(session.reroute("v", "pay", {})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  route \"pay\" from 1, 0.5\n"));
  EXPECT_FALSE(session.reroute("v", "nope", {}).ok());
}

TEST(FixedView, AScopedPinKeepsTheNameAsWritten) {
  Session session(R"(model {
  shop = system "Shop" {
    web = container "Web"
  }
}

view inside of shop {
  include *
  layout fixed
  pin web at 0, 0
}
)");
  ASSERT_TRUE(dd::test::ok(session.resize("inside", "shop.web", Box{0, 0, 90, 40})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin web at 0, 0 size 90, 40\n"));
}

// ── Moving several elements together ──

TEST(FixedView, MovingSeveralElementsPinsEachAndShiftsTheRoutesBetweenThem) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  auto r = dd::test::ok(session.move("v", {"a", "b"}, Point{30.4, -20}));
  ASSERT_TRUE(r.has_value());
  // One change: both pins rewritten where they stand (b keeps its size), and the route whose two
  // ends moved follows them, keeping where it leaves its source.
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin a at 70, 20   // the source\n"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin b at 330, 20 size 120, 60\n"));
  EXPECT_TRUE(dd::test::contains(session.source(), "route \"pay\" via 230, -20 from 1, 0.5\n"));
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(rendered.geometry.nodes.find("b")->x, 330);
  EXPECT_EQ(rendered.geometry.nodes.find("b")->y, 20);
}

TEST(FixedView, MovingOneEndOfARouteLeavesItsBendPoints) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  ASSERT_TRUE(dd::test::ok(session.move("v", {"a"}, Point{0, 100})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "pin a at 40, 140"));
  EXPECT_TRUE(dd::test::contains(session.source(), "route \"pay\" via 200, 0 from 1, 0.5\n"));
}

TEST(Session, MovingSeveralElementsInALaidOutViewPinsEachWhereItLands) {
  Session session(kSource);
  auto before = dd::test::ok(session.render("v")).geometry;
  ASSERT_TRUE(dd::test::ok(session.move("v", {"b", "c", "b"}, Point{0, 300})).has_value());
  auto after = dd::test::ok(session.render("v")).geometry;
  // `pin c below b` gives way to c's own pin.
  EXPECT_FALSE(dd::test::contains(session.source(), "pin c below b"));
  for (const char* id : {"b", "c"}) {
    EXPECT_NEAR(after.nodes.find(id)->x, before.nodes.find(id)->x, 1) << id;
    EXPECT_NEAR(after.nodes.find(id)->y, before.nodes.find(id)->y + 300, 1) << id;
  }
}

TEST(Session, AMoveByNothingOrOfNothingKnownChangesNothing) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  std::string source = session.source();
  EXPECT_FALSE(dd::test::ok(session.move("v", {"a", "b"}, Point{0, 0})).has_value());
  EXPECT_FALSE(dd::test::ok(session.move("v", {"nope"}, Point{10, 0})).has_value());
  EXPECT_EQ(session.source(), source);
  EXPECT_FALSE(session.move("missing", {"a"}, Point{10, 0}));
}

TEST(Session, AMoveByAnOffsetThatIsNotFiniteFails) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  std::string source = session.source();
  EXPECT_FALSE(session.move("v", {"a"}, Point{std::nan(""), 0}));
  EXPECT_FALSE(session.move("v", {"a"}, Point{0, std::numeric_limits<double>::infinity()}));
  EXPECT_EQ(session.source(), source);
}

TEST(FixedView, MovingHundredsOfElementsIsOneMinimalPatch) {
  // Every pin is rewritten where it stands, in one pass over the text.
  std::string source = "model {\n";
  for (int i = 0; i < 300; ++i) source += "  n" + std::to_string(i) + " = process \"N\"\n";
  for (int i = 1; i < 300; ++i) source += "  n" + std::to_string(i - 1) + " -> n" + std::to_string(i) + "\n";
  source += "}\n\nview v {\n  include *\n  layout fixed\n";
  for (int i = 0; i < 300; ++i)
    source += "  pin n" + std::to_string(i) + " at " + std::to_string(i * 10) + ", 0  // n" + std::to_string(i) + "\n";
  for (int i = 1; i < 300; ++i)
    source += "  route \"n" + std::to_string(i - 1) + "->n" + std::to_string(i) + "\" via " + std::to_string(i * 10) +
              ", 50\n";
  source += "}\n";
  Session session(source);
  dd::test::ok(session.render("v"));
  std::vector<ElementId> all;
  for (int i = 0; i < 300; ++i) all.push_back("n" + std::to_string(i));
  auto r = dd::test::ok(session.move("v", all, Point{5, 7}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->minimal);
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin n0 at 5, 7  // n0\n"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  pin n299 at 2995, 7  // n299\n"));
  EXPECT_TRUE(dd::test::contains(session.source(), "  route \"n298->n299\" via 2995, 57\n"));
}

TEST(Session, AGroupMoveRemovesTheRelativeHintsOfEveryMovedElementOnce) {
  // `align b c` places both: it goes once, and each gets its own pin.
  Session session(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
}

view v {
  include *
  align b c horizontally
  pin a below b
}
)");
  dd::test::ok(session.render("v"));
  ASSERT_TRUE(dd::test::ok(session.move("v", {"a", "b", "c"}, Point{10, 10})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "align"));
  EXPECT_FALSE(dd::test::contains(session.source(), "below"));
  for (const char* id : {"a", "b", "c"})
    EXPECT_TRUE(dd::test::contains(session.source(), std::string("pin ") + id + " at")) << id;
}

TEST(FixedView, AnUnpinnedContainerFollowsItsChildrenOrKeepsItsDrawnSize) {
  const char* source = R"(model {
  shop = system "Shop" {
    web = container "Web"
    api = container "API"
  }
  db = database "DB"
}

view v {
  include *
  layout fixed
  pin shop.web at 40, 60
  pin shop.api at 240, 60
  pin db at 500, 60
}
)";
  {
    // Moved with all of its children, it stays unpinned and wraps them where they land.
    Session session(source);
    const Box before = *dd::test::ok(session.render("v")).geometry.nodes.find("shop");
    ASSERT_TRUE(dd::test::ok(session.move("v", {"shop", "shop.web", "shop.api", "db"}, Point{100, 50})).has_value());
    EXPECT_FALSE(dd::test::contains(session.source(), "pin shop at"));
    const Box after = *dd::test::ok(session.render("v")).geometry.nodes.find("shop");
    EXPECT_EQ(after, (Box{before.x + 100, before.y + 50, before.width, before.height}));
  }
  {
    // Moved without one of them, it is pinned at the size it was drawn, as the drag showed it.
    Session session(source);
    const Box before = *dd::test::ok(session.render("v")).geometry.nodes.find("shop");
    ASSERT_TRUE(dd::test::ok(session.move("v", {"shop", "shop.web", "db"}, Point{100, 50})).has_value());
    const Box after = *dd::test::ok(session.render("v")).geometry.nodes.find("shop");
    EXPECT_EQ(after, (Box{before.x + 100, before.y + 50, before.width, before.height}));
  }
}

namespace {

/// A view of `n` processes in a row, placed by `placement(i)` (empty: placed by the layout alone).
std::string many(int n, const std::string& layout, const std::function<std::string(int)>& placement) {
  std::string source = "model {\n";
  for (int i = 0; i < n; ++i) source += "  n" + std::to_string(i) + " = process \"N\"\n";
  source += "}\n\nview v {\n  include *\n  layout " + layout + "\n";
  for (int i = 0; i < n; ++i)
    if (auto line = placement(i); !line.empty()) source += "  " + line + "\n";
  return source + "}\n";
}

std::vector<ElementId> all_of(int n) {
  std::vector<ElementId> ids;
  for (int i = 0; i < n; ++i) ids.push_back("n" + std::to_string(i));
  return ids;
}

}  // namespace

TEST(Session, MovingHundredsOfUnpinnedElementsAppendsTheirPinsInOrder) {
  Session session(many(200, "fixed", [](int) { return std::string(); }));
  dd::test::ok(session.render("v"));
  auto r = dd::test::ok(session.move("v", all_of(200), Point{5, 7}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->minimal);
  auto first = session.source().find("  pin n0 at ");
  auto second = session.source().find("  pin n1 at ");
  auto last = session.source().find("  pin n199 at ");
  ASSERT_NE(first, std::string::npos);
  EXPECT_LT(first, second);
  EXPECT_LT(second, last);
  // Appended where the view ends, the last one just before its closing brace.
  auto close = session.source().find("\n}\n", last);
  EXPECT_EQ(session.source().find('\n', last), close);
}

TEST(Session, MovingHundredsOfRelativelyPlacedElementsRemovesEveryOldHint) {
  Session session(many(200, "layered", [](int i) {
    return i ? "pin n" + std::to_string(i) + " below n" + std::to_string(i - 1) : std::string();
  }));
  dd::test::ok(session.render("v"));
  auto r = dd::test::ok(session.move("v", all_of(200), Point{5, 7}));
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->minimal);
  EXPECT_FALSE(dd::test::contains(session.source(), "below"));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin n199 at "));
}

TEST(Session, RelativeHintsOnOneLineAllGoInAGroupMove) {
  Session session(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
}

view v { include * pin b below a pin c below b }
)");
  dd::test::ok(session.render("v"));
  ASSERT_TRUE(dd::test::ok(session.move("v", {"b", "c"}, Point{10, 0})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "below"));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin b at "));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin c at "));
}

TEST(FixedView, AMoveThatRoundsToNothingChangesNothing) {
  Session session(kFixed);
  dd::test::ok(session.render("v"));
  // `b` is pinned where it is drawn; `a` is pinned too, so a fifth of a unit rewrites nothing.
  EXPECT_FALSE(dd::test::ok(session.move("v", {"a", "b"}, Point{0.2, 0})).has_value());
}

TEST(Views, AddRenameAndRemoveThroughTheText) {
  Session session(kSource);
  EXPECT_TRUE(fresh_view_id(session.model()) == "view1");
  auto added = dd::test::ok(add_view_operation(session.parsed().model, "inside", "b"));
  (void)dd::test::ok(session.apply(added));
  EXPECT_TRUE(dd::test::contains(session.source(), "\n\nview inside of b {\n  include *\n}\n"));
  EXPECT_TRUE((session.model().views.keys() == std::vector<std::string>{"v", "inside"}));

  auto renamed = dd::test::ok(rename_view_operation(session.parsed().model, "v", "overview"));
  ASSERT_TRUE(renamed.has_value());
  (void)dd::test::ok(session.apply(*renamed));
  EXPECT_TRUE(dd::test::contains(session.source(), "view overview {\n  include *\n  pin c below b   // dragged\n}"));
  EXPECT_TRUE((session.model().views.keys() == std::vector<std::string>{"overview", "inside"}));
  EXPECT_FALSE(dd::test::ok(rename_view_operation(session.parsed().model, "inside", "inside")).has_value());

  (void)dd::test::ok(session.apply(dd::test::ok(remove_view_operation(session.parsed().model, "inside"))));
  EXPECT_FALSE(dd::test::contains(session.source(), "view inside"));
  EXPECT_TRUE((session.model().views.keys() == std::vector<std::string>{"overview"}));
}

TEST(Views, RefuseBadNamesTakenNamesAndTheLastView) {
  Session session(kSource);
  const Model& declared = session.parsed().model;
  EXPECT_FALSE(add_view_operation(declared, "v").ok());
  EXPECT_FALSE(add_view_operation(declared, "2nd").ok());
  EXPECT_FALSE(add_view_operation(declared, "with space").ok());
  EXPECT_FALSE(add_view_operation(declared, "").ok());
  EXPECT_FALSE(add_view_operation(declared, "x", "nope").ok());
  EXPECT_FALSE(rename_view_operation(declared, "v", "bad-name").ok());
  EXPECT_FALSE(rename_view_operation(declared, "nope", "w").ok());
  EXPECT_FALSE(remove_view_operation(declared, "v").ok());
  EXPECT_FALSE(remove_view_operation(declared, "nope").ok());
}

TEST(Views, WithoutViewsTheImpliedDefaultIsWrittenOut) {
  const std::string text = "model {\n  a = process\n}\n";
  Session session(text);
  // A new view next to the implied one keeps the implied one, now in the text.
  (void)dd::test::ok(session.apply(dd::test::ok(add_view_operation(session.parsed().model, "second"))));
  EXPECT_TRUE(dd::test::contains(session.source(), "view default {\n  include *\n}\n\nview second {"));
  EXPECT_TRUE((session.model().views.keys() == std::vector<std::string>{"default", "second"}));
  // A default view of its own cannot be added twice.
  Session fresh(text);
  EXPECT_FALSE(add_view_operation(fresh.parsed().model, "default").ok());
  // Renaming the implied view writes it under the new name.
  auto renamed = dd::test::ok(rename_view_operation(fresh.parsed().model, "default", "main"));
  ASSERT_TRUE(renamed.has_value());
  (void)dd::test::ok(fresh.apply(*renamed));
  EXPECT_TRUE(dd::test::contains(fresh.source(), "view main {"));
  EXPECT_TRUE((fresh.model().views.keys() == std::vector<std::string>{"main"}));
  // And the implied view is the only one: it cannot go.
  Session only(text);
  EXPECT_FALSE(remove_view_operation(only.parsed().model, "default").ok());
}

namespace {

// Laid out fresh, the second text puts `a` right of `c`; laid out from the first text's layout,
// it keeps `a` on the left. So a view that starts from its last layout shows `a` left of `c`.
const char* const kOrderFirst = "model {\n  a = process\n  b = process\n  c = process\n  a -> b\n  a -> c\n}\n";
const char* const kOrderSecond =
    "model {\n  d = process\n  c = process\n  b = process\n  a = process\n  a -> b\n  a -> c\n  d -> c\n}\n";

bool a_left_of_c(const Geometry& g) {
  const Box* a = g.nodes.find("a");
  const Box* c = g.nodes.find("c");
  if (!a || !c) dd::test::stop("a or c is not laid out");
  return a->x < c->x;
}

}  // namespace

TEST(Views, ARenamedViewStartsFromItsLastLayout) {
  for (bool declared : {true, false}) {
    std::string view = declared ? "\nview v {\n  include *\n}\n" : "";
    std::string id = declared ? "v" : "default";
    Session session(kOrderFirst + view);
    (void)dd::test::ok(session.render(id));
    session.set_source(kOrderSecond + view);
    auto renamed = dd::test::ok(rename_view_operation(session.parsed().model, id, "w"));
    ASSERT_TRUE(renamed.has_value());
    (void)dd::test::ok(session.apply(*renamed));
    EXPECT_TRUE(a_left_of_c(dd::test::ok(session.render("w")).geometry)) << id;
    // The same text laid out from nothing orders them the other way.
    Session fresh(session.source());
    EXPECT_FALSE(a_left_of_c(dd::test::ok(fresh.render("w")).geometry)) << id;
  }
}

TEST(FieldProblems, NameTheFieldsAnElementEditWouldRefuse) {
  const std::string source = "model {\n  shop = system {\n    web = container\n    db = container\n  }\n}\n";
  auto parsed = dsl::parse(source);
  Element web = *parsed.model.elements.find("shop.web");
  EXPECT_TRUE(element_problems(source, parsed, web, "web").empty());
  EXPECT_EQ(element_problems(source, parsed, web, "db"),
            (std::vector<FieldProblem>{{"id", "There is already an element \"shop.db\""}}));
  EXPECT_EQ(element_id_problem(source, parsed, "shop.web", "store"), std::nullopt);
  EXPECT_NE(element_id_problem(source, parsed, "shop.web", "9x"), std::nullopt);

  Element edited = web;
  edited.kind = "";
  edited.shape = "a.b.c";
  edited.style = Style{{"fill", "red;blue"}};
  auto problems = element_problems(source, parsed, edited, "web");
  ASSERT_EQ(problems.size(), 3U);
  EXPECT_EQ(problems[0].field, "kind");
  EXPECT_NE(problems[0].message.find("Every element has a kind"), std::string::npos);
  EXPECT_EQ(problems[1].field, "shape");
  EXPECT_EQ(problems[2], (FieldProblem{"style", "The value of style key \"fill\" cannot hold \";\""}));
  edited.kind = "web app";
  EXPECT_NE(element_problems(source, parsed, edited, "web")[0].message.find("is not a kind"), std::string::npos);

  // An element that is gone has none.
  edited.id = "shop.gone";
  EXPECT_TRUE(element_problems(source, parsed, edited, "9x").empty());
}

TEST(FieldProblems, NameTheFieldsARelationshipEditWouldRefuse) {
  auto parsed = dsl::parse("model {\n  a = process\n  b = process\n  flow = a -> b\n  b -> a\n}\n");
  const Model& model = parsed.model;
  Relationship back = *model.relationships.find("b->a");
  EXPECT_TRUE(relationship_problems(model, back, std::nullopt).empty());
  EXPECT_TRUE(relationship_problems(model, back, "reply").empty());
  EXPECT_EQ(relationship_problems(model, back, "flow"),
            (std::vector<FieldProblem>{{"name", "Another relationship is already named \"flow\""}}));
  EXPECT_EQ(relationship_problems(model, back, "2x").at(0).field, "name");
  EXPECT_TRUE(relationship_problems(model, *model.relationships.find("flow"), "flow").empty());
  back.style = Style{{"a=b", "1"}};
  EXPECT_EQ(relationship_problems(model, back, std::nullopt),
            (std::vector<FieldProblem>{{"style", "Style key \"a=b\" cannot hold \"=\" or \";\""}}));
  back.id = "gone";
  EXPECT_TRUE(relationship_problems(model, back, "2x").empty());
}

namespace {

// draw.io's fig4: a component icon anchored on its box's top right corner.
const char* kAdorned = R"(model {
  comp = process "Component" style "whiteSpace=wrap" {
    icon = process "" shape module style "anchor=1,0;anchorOffset=-27,7"
  }
  other = process "Other"
}

view v {
  include *
  layout fixed
  pin comp at 170, 180 size 180, 90
  size comp.icon 20, 20
  pin other at 600, 180
}
)";

}  // namespace

TEST(FixedView, AnAdornmentStaysOnItsParentsCorner) {
  Session session(kAdorned);
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("comp.icon"), (Box{323, 187, 20, 20}));
  // Moved: the icon goes along, with no hint of its own.
  ASSERT_TRUE(dd::test::ok(session.drop("v", "comp", Box{300, 250, 180, 90})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "pin comp.icon"));
  rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("comp.icon"), (Box{453, 257, 20, 20}));
  // Resized: still on the top right corner.
  ASSERT_TRUE(dd::test::ok(session.resize("v", "comp", Box{300, 250, 260, 120})).has_value());
  rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("comp.icon"), (Box{533, 257, 20, 20}));
  // Moved together with another element: the icon follows its parent, not a pin.
  ASSERT_TRUE(dd::test::ok(session.move("v", {"comp", "comp.icon", "other"}, Point{10, 10})).has_value());
  EXPECT_FALSE(dd::test::contains(session.source(), "pin comp.icon"));
  rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("comp.icon"), (Box{543, 267, 20, 20}));
}

TEST(FixedView, AnAdornmentDraggedOrResizedKeepsItsParent) {
  Session session(kAdorned);
  dd::test::ok(session.render("v"));
  // Dragged to the top left corner: its offset changes, it still hangs on the right edge.
  ASSERT_TRUE(dd::test::ok(session.drop("v", "comp.icon", Box{175, 185, 20, 20})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "anchor=1,0;anchorOffset=-175,5\"")) << session.source();
  EXPECT_FALSE(dd::test::contains(session.source(), "pin comp.icon"));
  // Dropped where it is: nothing to change.
  dd::test::ok(session.render("v"));
  EXPECT_FALSE(dd::test::ok(session.drop("v", "comp.icon", Box{175, 185, 20, 20})).has_value());
  // Moved alone in a group move.
  ASSERT_TRUE(dd::test::ok(session.move("v", {"comp.icon"}, Point{5, 0})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "anchorOffset=-170,5\"")) << session.source();
  // Resized: the size goes into `size`, the corner into its offset.
  dd::test::ok(session.render("v"));
  ASSERT_TRUE(dd::test::ok(session.resize("v", "comp.icon", Box{178, 185, 30, 24})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "anchorOffset=-172,5\"")) << session.source();
  EXPECT_TRUE(dd::test::contains(session.source(), "size comp.icon 30, 24")) << session.source();
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_EQ(*rendered.geometry.nodes.find("comp.icon"), (Box{178, 185, 30, 24}));
}

TEST(FixedView, AnAdornmentDoesNotMakeItsParentAContainer) {
  // Fitting the box keeps its pin: the icon on it does not make it wrap anything.
  Session session(kAdorned);
  dd::test::ok(session.render("v"));
  dd::test::ok(session.fit("v", {"comp"}));
  EXPECT_TRUE(dd::test::contains(session.source(), "pin comp at 170, 180")) << session.source();
  // An unpinned container moved together with its adornment and its child follows them.
  Session moved(R"(model {
  sys = system "Sys" style "whiteSpace=wrap" {
    icon = process "" style "anchor=1,0;anchorOffset=-24,4"
    a = process "A"
  }
}

view v {
  include *
  layout fixed
  pin sys.a at 100, 100 size 100, 40
  size sys.icon 20, 20
}
)");
  auto before = dd::test::ok(moved.render("v"));
  const Box sys = *before.geometry.nodes.find("sys");
  ASSERT_TRUE(dd::test::ok(moved.move("v", {"sys", "sys.icon", "sys.a"}, Point{50, 50})).has_value());
  EXPECT_FALSE(dd::test::contains(moved.source(), "pin sys at")) << moved.source();
  EXPECT_FALSE(dd::test::contains(moved.source(), "pin sys.icon")) << moved.source();
  auto after = dd::test::ok(moved.render("v"));
  EXPECT_EQ(*after.geometry.nodes.find("sys"), (Box{sys.x + 50, sys.y + 50, sys.width, sys.height}));
  EXPECT_EQ(*after.geometry.nodes.find("sys.icon"), (Box{sys.x + 50 + sys.width - 24, sys.y + 54, 20, 20}));
}

TEST(FixedView, DraggingAnAdornmentPreviewsNoPin) {
  Session session(kAdorned);
  dd::test::ok(session.render("v"));
  EXPECT_FALSE(dd::test::ok(session.drop_hint("v", "comp.icon", Box{175, 185, 20, 20}, false)).has_value());
  EXPECT_TRUE(dd::test::ok(session.drop_hint("v", "comp", Box{175, 185, 180, 90}, false)).has_value());
}

TEST(FixedView, AResizeFromTheFarCornerKeepsAFractionalAnchorOffset) {
  std::string source = kAdorned;
  source.replace(source.find("anchorOffset=-27,7"), 18, "anchorOffset=-27.5,7");
  Session session(source);
  auto rendered = dd::test::ok(session.render("v"));
  const Box icon = *rendered.geometry.nodes.find("comp.icon");
  ASSERT_TRUE(dd::test::ok(session.resize("v", "comp.icon", Box{icon.x, icon.y, 30, 30})).has_value());
  EXPECT_TRUE(dd::test::contains(session.source(), "anchorOffset=-27.5,7\"")) << session.source();
  EXPECT_TRUE(dd::test::contains(session.source(), "size comp.icon 30, 30")) << session.source();
}
