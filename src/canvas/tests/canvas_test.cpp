#include "diadraw/canvas/canvas.hpp"
#include "diadraw/layout/layout.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using dd::canvas::Canvas;
using dd::canvas::Edit;
using dd::canvas::Modifiers;

constexpr const char* kSource = R"(model {
  user = person "Customer"
  shop = system "Shop"
  db = database "Orders"
  user -> shop "browses"
}

view main {
  include *
  layout layered direction down
}
)";

/// A canvas showing kSource's view, with every callback logged.
struct CanvasTest : ::testing::Test {
  dd::editor::Session session{kSource};
  Canvas canvas;
  std::vector<std::string> log;

  void SetUp() override {
    auto r = session.render("main");
    ASSERT_TRUE(r);
    canvas.show(*r);
    auto& c = canvas.callbacks;
    c.selected = [&](const dd::editor::Selection& s) {
      std::string line = "select";
      for (const auto& e : s.elements) line += " " + e;
      for (const auto& e : s.relationships) line += " " + e;
      log.push_back(line);
    };
    c.dropped = [&](const std::string& id, const dd::Box&, bool freeform) {
      log.push_back("drop " + id + (freeform ? " freeform" : ""));
    };
    c.connected = [&](const std::string& a, const std::string& b) { log.push_back("connect " + a + " " + b); };
    c.reconnected = [&](const std::string& id, dd::editor::End end, const std::string& element) {
      log.push_back("reconnect " + id + (end == dd::editor::End::Source ? " source " : " target ") + element);
    };
    c.renamed = [&](const std::string& id, const std::string& name) { log.push_back("rename " + id + " " + name); };
    c.relabelled = [&](const std::string& id, const std::optional<std::string>& label) {
      log.push_back("relabel " + id + " " + label.value_or("-"));
    };
    c.deleted = [&](const dd::editor::Selection& s) { log.push_back("delete " + std::to_string(s.elements.size())); };
    c.name_of = [](const std::string& id) { return "name of " + id; };
    c.label_of = [](const std::string&) { return std::string("browses"); };
  }

  dd::draw::Point center(const std::string& id) {
    auto p = canvas.center_of(id);
    EXPECT_TRUE(p);
    return p.value_or(dd::draw::Point{});
  }
  void click(dd::draw::Point p, Modifiers m = {}) {
    canvas.press(p.x, p.y, 1, m);
    canvas.release(p.x, p.y, m);
  }
  void drag(dd::draw::Point a, dd::draw::Point b, Modifiers m = {}) {
    canvas.press(a.x, a.y, 1, m);
    for (int i = 1; i <= 5; ++i) canvas.drag(a.x + (b.x - a.x) * i / 5, a.y + (b.y - a.y) * i / 5, m);
    canvas.release(b.x, b.y, m);
  }
};

TEST_F(CanvasTest, ClickSelectsAndShiftAdds) {
  click(center("shop"));
  click(center("db"), {.shift = true});
  click(center("shop"), {.shift = true});
  click({1, 1});
  EXPECT_EQ(log, (std::vector<std::string>{"select shop", "select shop db", "select db", "select"}));
}

TEST_F(CanvasTest, SmallMovesAreClicksAndDragsDrop) {
  auto p = center("db");
  drag(p, {p.x + 2, p.y});
  EXPECT_EQ(log, (std::vector<std::string>{"select db"}));
  drag(p, {p.x + 200, p.y});
  EXPECT_EQ(log.back(), "drop db freeform");
  drag(p, {p.x + 200, p.y}, {.snap = true});
  EXPECT_EQ(log.back(), "drop db");
}

TEST_F(CanvasTest, ADropKeepsTheModifierItsPreviewShowed) {
  // Option let go a moment before the button: the drop is still placed by the neighbours.
  auto p = center("db");
  canvas.press(p.x, p.y, 1, {.snap = true});
  canvas.drag(p.x + 100, p.y, {.snap = true});
  canvas.drag(p.x + 200, p.y, {.snap = true});
  canvas.release(p.x + 200, p.y, {});
  EXPECT_EQ(log.back(), "drop db");
}

TEST_F(CanvasTest, OverlayShowsGhostWhileDragging) {
  auto p = center("db");
  click(p);
  // Selection outline, connect handle, four resize corners, the rotate handle and its stalk.
  auto idle = canvas.overlay().items.size();
  EXPECT_EQ(idle, 8u);
  canvas.press(p.x, p.y, 1, {});
  canvas.drag(p.x + 100, p.y, {});
  EXPECT_EQ(canvas.overlay().items.size(), 2u);  // outline and ghost; the handle hides
  canvas.release(p.x + 100, p.y, {});
}

TEST_F(CanvasTest, HandleConnects) {
  click(center("shop"));
  auto h = canvas.handle_point();
  ASSERT_TRUE(h);
  drag(*h, center("db"));
  EXPECT_EQ(log.back(), "connect shop db");
}

TEST_F(CanvasTest, DraggingAnEndOfASelectedLineReconnectsIt) {
  EXPECT_FALSE(canvas.end_point(dd::editor::End::Target));  // nothing selected: no handles
  auto l = canvas.label_point("user->shop");
  ASSERT_TRUE(l);
  click(*l);
  EXPECT_EQ(log.back(), "select user->shop");
  EXPECT_EQ(canvas.overlay().items.size(), 3u);  // the highlighted line and a handle on each end
  auto target = canvas.end_point(dd::editor::End::Target);
  auto source = canvas.end_point(dd::editor::End::Source);
  ASSERT_TRUE(target && source);

  drag(*target, center("db"));
  EXPECT_EQ(log.back(), "reconnect user->shop target db");
  drag(*source, center("db"));
  EXPECT_EQ(log.back(), "reconnect user->shop source db");

  // Dropping on either end's own element, or on nothing, changes nothing.
  auto before = log.size();
  drag(*target, center("shop"));
  drag(*target, center("user"));
  drag(*target, {1, 1});
  EXPECT_EQ(log.size(), before);
}

TEST_F(CanvasTest, ReconnectShowsTheLineFollowingThePointer) {
  click(*canvas.label_point("user->shop"));
  auto target = canvas.end_point(dd::editor::End::Target);
  ASSERT_TRUE(target);
  auto db = center("db");
  canvas.press(target->x, target->y, 1, {});
  canvas.drag(db.x, db.y, {});
  // The highlighted line, the dashed line to the pointer, the ghost on db and the source's handle.
  EXPECT_EQ(canvas.overlay().items.size(), 4u);
  canvas.release(db.x, db.y, {});
}

TEST_F(CanvasTest, DoubleClickEditsNodesAndEdges) {
  std::optional<Edit> edit;
  auto p = center("db");
  canvas.press(p.x, p.y, 2, {}, &edit);
  ASSERT_TRUE(edit);
  EXPECT_EQ(edit->id, "db");
  EXPECT_EQ(edit->value, "name of db");
  canvas.commit_edit(*edit, "  name of db ");  // unchanged: nothing
  canvas.commit_edit(*edit, "Order store");
  auto l = canvas.label_point("user->shop");
  ASSERT_TRUE(l);
  canvas.press(l->x, l->y, 2, {}, &edit);
  ASSERT_TRUE(edit);
  EXPECT_TRUE(edit->relationship);
  canvas.commit_edit(*edit, "");
  EXPECT_EQ(log, (std::vector<std::string>{"rename db Order store", "relabel user->shop -"}));
}

TEST_F(CanvasTest, ANameEditTakesSeveralLines) {
  std::optional<Edit> edit;
  auto p = center("db");
  canvas.press(p.x, p.y, 2, {}, &edit);
  ASSERT_TRUE(edit);
  EXPECT_TRUE(edit->multiline());
  // Native fields give line breaks as \r\n or \r; the name keeps them as \n.
  canvas.commit_edit(*edit, "Order\r\nstore\rarchive\n");
  auto l = canvas.label_point("user->shop");
  ASSERT_TRUE(l);
  canvas.press(l->x, l->y, 2, {}, &edit);
  ASSERT_TRUE(edit);
  EXPECT_FALSE(edit->multiline());
  canvas.commit_edit(*edit, "buys\r\nfrom\r");  // pasted line breaks are normalised here too
  EXPECT_EQ(log, (std::vector<std::string>{"rename db Order\nstore\narchive", "relabel user->shop buys\nfrom"}));
}

TEST_F(CanvasTest, DeleteAndEscape) {
  EXPECT_FALSE(canvas.delete_selection());
  click(center("user"));
  EXPECT_TRUE(canvas.delete_selection());
  EXPECT_EQ(log, (std::vector<std::string>{"select user", "select", "delete 1"}));
}

TEST_F(CanvasTest, ZoomKeepsPointsOnTheirNodes) {
  canvas.set_zoom(2.5);
  auto p = center("shop");
  EXPECT_EQ(canvas.node_at(canvas.to_user(p.x, p.y), false), "shop");
  canvas.set_zoom(100);
  EXPECT_EQ(canvas.zoom(), 8);
}

TEST_F(CanvasTest, LaidOutViewsResizeAndTurnNodesButHaveNoBendHandles) {
  EXPECT_FALSE(canvas.fixed());
  click(center("shop"));
  EXPECT_EQ(canvas.resize_points().size(), 4u);
  EXPECT_TRUE(canvas.rotate_point());
  auto l = canvas.label_point("user->shop");
  ASSERT_TRUE(l);
  click(*l);
  EXPECT_EQ(log.back(), "select user->shop");
  EXPECT_TRUE(canvas.bend_points().empty());
}

TEST_F(CanvasTest, WithTheGridShownALaidOutResizeKeepsWholeSteps) {
  // A layout places the node, so only its size is written: whole grid steps, the smallest one step.
  std::vector<dd::Box> sizes;
  canvas.callbacks.resized = [&](const std::string&, const dd::Box& b) { sizes.push_back(b); };
  canvas.set_grid(10);
  click(center("shop"));
  auto c = canvas.resize_points()[2];
  double unit = 1 / canvas.units_per_pixel();
  drag(c, {c.x + 23 * unit, c.y + 7 * unit});
  ASSERT_EQ(sizes.size(), 1u);
  EXPECT_EQ(std::fmod(sizes[0].width, 10), 0);
  EXPECT_EQ(std::fmod(sizes[0].height, 10), 0);
  click(center("shop"));
  c = canvas.resize_points()[2];
  drag(c, {c.x - 1000 * unit, c.y - 1000 * unit});
  ASSERT_EQ(sizes.size(), 2u);
  EXPECT_EQ(sizes[1].width, 10);
  EXPECT_EQ(sizes[1].height, 10);
}

constexpr const char* kFixedSource = R"(model {
  a = process "A"
  b = process "B"
  pay = a -> b
}

view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 50
  pin b at 300, 0 size 100, 50
  route "pay" via 200, 150
}
)";

/// A canvas showing a fixed view, logging resizes and reroutes in diagram units.
struct FixedCanvasTest : ::testing::Test {
  dd::editor::Session session{kFixedSource};
  Canvas canvas;
  std::vector<std::string> log;

  static std::string num(double v) { return std::to_string(static_cast<int>(std::lround(v))); }

  void SetUp() override {
    auto r = session.render("v");
    ASSERT_TRUE(r);
    canvas.show(*r);
    canvas.set_zoom(2);
    canvas.callbacks.resized = [&](const std::string& id, const dd::Box& b) {
      log.push_back("resize " + id + " " + num(b.x) + "," + num(b.y) + " " + num(b.width) + "x" + num(b.height));
    };
    canvas.callbacks.rerouted = [&](const std::string& id, const std::vector<dd::Point>& points) {
      std::string line = "route " + id;
      for (const auto& p : points) line += " " + num(p.x) + "," + num(p.y);
      log.push_back(line);
    };
    canvas.callbacks.dropped = [&](const std::string& id, const dd::Box&, bool) { log.push_back("drop " + id); };
    canvas.callbacks.rotated = [&](const std::string& id, double degrees) {
      log.push_back("rotate " + id + " " + num(degrees));
    };
    canvas.callbacks.fitted = [&](const std::string& id) { log.push_back("fit " + id); };
  }

  /// A diagram point in view coordinates.
  dd::draw::Point at(double x, double y) { return canvas.to_view(x, y); }
  void click(dd::draw::Point p) {
    canvas.press(p.x, p.y, 1, {});
    canvas.release(p.x, p.y, {});
  }
  void drag(dd::draw::Point a, dd::draw::Point b, Modifiers m = {}) {
    canvas.press(a.x, a.y, 1, m);
    for (int i = 1; i <= 5; ++i) canvas.drag(a.x + (b.x - a.x) * i / 5, a.y + (b.y - a.y) * i / 5, m);
    canvas.release(b.x, b.y, m);
  }
};

TEST_F(FixedCanvasTest, CornersResizeTheSelectedNode) {
  EXPECT_TRUE(canvas.fixed());
  click(at(50, 25));
  auto corners = canvas.resize_points();
  ASSERT_EQ(corners.size(), 4u);
  drag(corners[2], at(140, 70));  // bottom right
  EXPECT_EQ(log.back(), "resize a 0,0 140x70");
  drag(corners[0], at(-20, 10));  // top left: the bottom right stays
  EXPECT_EQ(log.back(), "resize a -20,10 120x40");
  drag(corners[1], at(-500, -500));  // past the opposite corner: the smallest box
  EXPECT_EQ(log.back(), "resize a 0,-500 10x550");
  // The body of the node still moves it.
  drag(at(50, 25), at(80, 25));
  EXPECT_EQ(log.back(), "drop a");
}

TEST_F(FixedCanvasTest, DoubleClickingACornerFitsTheNode) {
  click(at(50, 25));
  auto corner = canvas.resize_points()[2];
  canvas.press(corner.x, corner.y, 1, {});
  canvas.release(corner.x, corner.y, {});
  std::optional<Edit> edit;
  canvas.press(corner.x, corner.y, 2, {}, &edit);
  canvas.release(corner.x, corner.y, {});
  EXPECT_EQ(log.back(), "fit a");
  EXPECT_FALSE(edit);
  // A double-click on the body still renames.
  log.clear();
  canvas.press(at(50, 25).x, at(50, 25).y, 2, {}, &edit);
  canvas.release(at(50, 25).x, at(50, 25).y, {});
  EXPECT_TRUE(log.empty());
  EXPECT_TRUE(edit);
}

TEST_F(FixedCanvasTest, DoubleClickingNearTheCornerOfAnUnselectedNodeRenamesIt) {
  // Seen from far away the handles' grip covers much of a small box; an unselected node shows no
  // handles, so a double-click on it renames it whatever the first click selected.
  canvas.set_zoom(0.25);
  dd::draw::Point p = at(4, 4);
  std::optional<Edit> edit;
  // AppKit: one press per click, with its count.
  click(p);
  canvas.press(p.x, p.y, 2, {}, &edit);
  canvas.release(p.x, p.y, {});
  EXPECT_TRUE(edit);
  // gtkmm and WinUI: the second click is a single press too, then the double-click comes.
  canvas.set_selection({});
  canvas.set_double_click_repeats_press(true);
  edit.reset();
  click(p);
  click(p);
  canvas.press(p.x, p.y, 2, {}, &edit);
  EXPECT_TRUE(edit);
  EXPECT_EQ(std::count(log.begin(), log.end(), "fit a"), 0);
}

TEST_F(FixedCanvasTest, DoubleClickingACornerFitsWhenTheSecondClickRepeatsThePress) {
  click(at(50, 25));
  auto corner = canvas.resize_points()[2];
  canvas.set_double_click_repeats_press(true);
  click(corner);
  click(corner);
  canvas.press(corner.x, corner.y, 2, {});
  EXPECT_EQ(log.back(), "fit a");
  // A third click on the handle does nothing, and opens no rename field.
  log.clear();
  std::optional<Edit> edit;
  canvas.press(corner.x, corner.y, 3, {}, &edit);
  EXPECT_FALSE(edit);
  EXPECT_TRUE(log.empty());
}

TEST_F(FixedCanvasTest, AClickRightAfterAResizeDoesNotFit) {
  click(at(50, 25));
  auto corner = canvas.resize_points()[2];
  drag(corner, at(140, 70));
  EXPECT_EQ(log.back(), "resize a 0,0 140x70");
  // The handle has not moved, as the shell has not re-rendered yet. AppKit order:
  canvas.press(corner.x, corner.y, 2, {});
  // gtkmm and WinUI order, where the drag's press would otherwise count as the first click:
  canvas.set_double_click_repeats_press(true);
  drag(corner, at(140, 70));
  click(corner);
  canvas.press(corner.x, corner.y, 2, {});
  EXPECT_EQ(std::count(log.begin(), log.end(), "fit a"), 0);
}

TEST_F(FixedCanvasTest, ShiftKeepsTheProportionsOfAResize) {
  click(at(50, 25));  // a is 100x50
  auto corners = canvas.resize_points();
  drag(corners[2], at(300, 60), {.shift = true});  // the wider stretch leads
  EXPECT_EQ(log.back(), "resize a 0,0 300x150");
  drag(corners[0], at(20, 40), {.shift = true});  // top left in: the bottom right stays
  EXPECT_EQ(log.back(), "resize a 20,10 80x40");
  drag(corners[0], at(1000, 1000), {.shift = true});  // past the opposite corner: the smallest box
  EXPECT_EQ(log.back(), "resize a 80,40 20x10");
}

TEST_F(FixedCanvasTest, AShiftResizeIgnoresTheGridAndKeepsItsPreview) {
  canvas.set_grid(20);
  click(at(50, 25));
  auto c = canvas.resize_points()[2];
  drag(c, at(130, 60), {.shift = true});  // 130x65: in proportion, off the grid
  EXPECT_EQ(log.back(), "resize a 0,0 130x65");
  // Shift let go just before the button: what is written is what the preview showed.
  auto to = at(300, 60);
  canvas.press(c.x, c.y, 1, {.shift = true});
  for (int i = 1; i <= 5; ++i)
    canvas.drag(c.x + (to.x - c.x) * i / 5, c.y + (to.y - c.y) * i / 5, {.shift = true});
  canvas.release(to.x, to.y, {});
  EXPECT_EQ(log.back(), "resize a 0,0 300x150");
}

TEST_F(FixedCanvasTest, OverlayShowsCornersAndTheNewSizeWhileResizing) {
  click(at(50, 25));
  // Outline, connect handle, four corners, rotate handle and stalk.
  EXPECT_EQ(canvas.overlay().items.size(), 8u);
  auto c = canvas.resize_points()[2];
  canvas.press(c.x, c.y, 1, {});
  canvas.drag(c.x + 40, c.y + 40, {});
  auto items = canvas.overlay().items;
  EXPECT_EQ(items.size(), 3u);  // outline, ghost and its size; the handles hide
  EXPECT_EQ(items.back().kind, dd::draw::Item::Label);
  EXPECT_EQ(items.back().text.content, "size 120 \u00d7 70");
  canvas.release(c.x + 40, c.y + 40, {});
}

TEST_F(FixedCanvasTest, BendPointsMoveAddAndGo) {
  auto l = canvas.label_point("pay");
  ASSERT_TRUE(l);
  click(*l);
  auto handles = canvas.bend_points();
  ASSERT_EQ(handles.size(), 3u);  // one bend point, two segment middles
  drag(handles[0], at(200, 200));
  EXPECT_EQ(log.back(), "route pay 200,200");
  // A segment's middle adds a bend point there.
  drag(handles[2], at(260, 120));
  EXPECT_EQ(log.back(), "route pay 200,150 260,120");
  // A small press is a click: nothing.
  canvas.press(handles[0].x, handles[0].y, 1, {});
  canvas.release(handles[0].x + 2, handles[0].y, {});
  EXPECT_EQ(log.size(), 2u);
}

TEST_F(FixedCanvasTest, WithTheGridShownGesturesLandOnIt) {
  canvas.callbacks.dropped = [&](const std::string& id, const dd::Box& b, bool freeform) {
    log.push_back("drop " + id + " " + num(b.x) + "," + num(b.y) + (freeform ? "" : " by neighbours"));
  };
  canvas.set_grid(20);
  // A move puts the top-left corner on the nearest grid point.
  drag(at(50, 25), at(83, 52));
  EXPECT_EQ(log.back(), "drop a 40,20");
  // Option/Alt asks for a placement by the neighbours: the layout decides, so no snapping.
  canvas.press(at(50, 25).x, at(50, 25).y, 1, {});
  canvas.drag(at(83, 52).x, at(83, 52).y, {.snap = true});
  canvas.release(at(83, 52).x, at(83, 52).y, {.snap = true});
  EXPECT_EQ(log.back(), "drop a 33,27 by neighbours");
  // A resize in a fixed view puts the dragged corner on a grid point.
  click(at(50, 25));
  drag(canvas.resize_points()[2], at(147, 73));
  EXPECT_EQ(log.back(), "resize a 0,0 140x80");
  // A bend point lands on one.
  click(*canvas.label_point("pay"));
  drag(canvas.bend_points()[0], at(207, 193));
  EXPECT_EQ(log.back(), "route pay 200,200");
  // Hidden again, gestures go where they are let go.
  canvas.set_grid(0);
  drag(at(50, 25), at(83, 52));
  EXPECT_EQ(log.back(), "drop a 33,27");
}

TEST_F(FixedCanvasTest, TheBackdropHoldsTheBackgroundAndTheGrid) {
  auto full = canvas.list()->items.size();
  for (const auto& item : canvas.list()->items) {
    if (item.kind == dd::draw::Item::Path) break;
    EXPECT_NE(item.kind, dd::draw::Item::Transform);
  }
  EXPECT_TRUE(canvas.backdrop(800, 600).items.empty());  // no grid, nothing to paint
  canvas.set_grid(10);
  // The background moves from the list to the backdrop, under minor and major lines.
  EXPECT_EQ(canvas.list()->items.size(), full - 1);
  auto backdrop = canvas.backdrop(800, 600).items;
  ASSERT_EQ(backdrop.size(), 3u);
  EXPECT_TRUE(backdrop[0].owner.empty());
  EXPECT_TRUE(backdrop[0].paint.fill);
  // It is the page's background, spread over the whole area: it comes before any transform.
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const auto& seg : backdrop[0].path) {
    x0 = std::min(x0, seg.p[0].x), y0 = std::min(y0, seg.p[0].y);
    x1 = std::max(x1, seg.p[0].x), y1 = std::max(y1, seg.p[0].y);
  }
  auto area_from = canvas.to_user(0, 0), area_to = canvas.to_user(800, 600);
  EXPECT_LE(x0, std::min(0.0, area_from.x));
  EXPECT_LE(y0, std::min(0.0, area_from.y));
  EXPECT_GE(x1, std::max(400.0, area_to.x));
  EXPECT_GE(y1, std::max(50.0, area_to.y));
  // At zoom 2 the lines are a pixel (half a unit) wide, every 10 units across the whole area.
  EXPECT_DOUBLE_EQ(backdrop[1].paint.stroke_width, 0.5);
  auto from = canvas.to_user(0, 0), to = canvas.to_user(800, 600);
  auto lines = [](const dd::draw::Item& i) { return i.path.size() / 2; };
  std::size_t expected = 0;
  for (double x = std::ceil(from.x / 10) * 10; x <= to.x; x += 10) ++expected;
  for (double y = std::ceil(from.y / 10) * 10; y <= to.y; y += 10) ++expected;
  EXPECT_EQ(lines(backdrop[1]) + lines(backdrop[2]), expected);
  // Zoomed far out the minor lines go, then all of them.
  canvas.set_zoom(0.5);
  EXPECT_EQ(canvas.backdrop(800, 600).items.size(), 2u);
  canvas.set_zoom(0.1);
  EXPECT_EQ(canvas.backdrop(800, 600).items.size(), 1u);
}

TEST_F(FixedCanvasTest, ABendDraggedOntoTheLineBetweenItsNeighboursGoes) {
  auto route = session.render("v")->geometry.edges.find("pay")->points;
  ASSERT_EQ(route.size(), 3u);
  click(*canvas.label_point("pay"));
  auto handles = canvas.bend_points();
  // Halfway along the line from where the edge leaves to where it arrives, give or take a pixel.
  drag(handles[0], at((route[0].x + route[2].x) / 2, (route[0].y + route[2].y) / 2 + 1));
  EXPECT_EQ(log.back(), "route pay");
}

}  // namespace

TEST_F(FixedCanvasTest, TheRotateHandleTurnsAndSnapsToFifteenDegrees) {
  click(at(50, 25));
  auto h = canvas.rotate_point();
  ASSERT_TRUE(h);
  // The handle, carried `degrees` clockwise about the centre of `a` (50, 25).
  auto carried = [&](double degrees) {
    auto u = canvas.to_user(h->x, h->y);
    auto q = dd::rotate_point({u.x, u.y}, {50, 25}, degrees);
    return at(q.x, q.y);
  };
  drag(*h, carried(90));
  EXPECT_EQ(log.back(), "rotate a 90");
  // 3° off 45 snaps to it; 7° off does not.
  drag(*h, carried(48));
  EXPECT_EQ(log.back(), "rotate a 45");
  drag(*h, carried(-38));
  EXPECT_EQ(log.back(), "rotate a -38");
}

constexpr const char* kTurnedSource = R"(model {
  a = process "A"
  b = process "B"
}

view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 50
  pin b at 300, 0 size 100, 50
  rotate a 90
}
)";

TEST(TurnedCanvas, HitsResizesAndHandlesFollowTheTurn) {
  dd::editor::Session session{kTurnedSource};
  Canvas canvas;
  auto r = session.render("v");
  ASSERT_TRUE(r);
  canvas.show(*r);
  EXPECT_EQ(canvas.rotation_of("a"), 90);
  // Turned upright, `a` covers x 25..75 and y -25..75: (50, 70) is on it, (90, 25) is not.
  EXPECT_EQ(canvas.node_at({50, 70}, false), "a");
  EXPECT_FALSE(canvas.node_at({90, 25}, false));
  std::vector<std::string> log;
  canvas.callbacks.resized = [&](const std::string& id, const dd::Box& b) {
    log.push_back(id + " " + std::to_string(std::lround(b.x)) + "," + std::to_string(std::lround(b.y)) + " " +
                  std::to_string(std::lround(b.width)) + "x" + std::to_string(std::lround(b.height)));
  };
  auto at = [&](double x, double y) { return canvas.to_view(x, y); };
  canvas.press(at(50, 25).x, at(50, 25).y, 1, {});
  canvas.release(at(50, 25).x, at(50, 25).y, {});
  auto corners = canvas.resize_points();
  ASSERT_EQ(corners.size(), 4u);
  // The box's own bottom-right corner is drawn at the bottom left once turned a quarter.
  auto br = canvas.to_user(corners[2].x, corners[2].y);
  EXPECT_NEAR(br.x, 25, 1e-6);
  EXPECT_NEAR(br.y, 75, 1e-6);
  // Dragging it 20 down makes the box 20 longer (its own width); its top-left corner, drawn at the
  // top right, stays where it is.
  auto c = corners[2];
  auto to = at(25, 95);
  canvas.press(c.x, c.y, 1, {});
  for (int i = 1; i <= 5; ++i) canvas.drag(c.x + (to.x - c.x) * i / 5, c.y + (to.y - c.y) * i / 5, {});
  canvas.release(to.x, to.y, {});
  ASSERT_EQ(log.size(), 1u);
  EXPECT_EQ(log.back(), "a -10,10 120x50");
  // With Shift the height follows in proportion, and the same corner stays put on screen.
  canvas.press(c.x, c.y, 1, {.shift = true});
  for (int i = 1; i <= 5; ++i)
    canvas.drag(c.x + (to.x - c.x) * i / 5, c.y + (to.y - c.y) * i / 5, {.shift = true});
  canvas.release(to.x, to.y, {.shift = true});
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log.back(), "a -15,5 120x60");
  // The connect handle turns with the node (off its own right edge, now below it), well clear of
  // the corner grips and the rotate handle.
  auto h = canvas.handle_point();
  auto turn = canvas.rotate_point();
  ASSERT_TRUE(h && turn);
  auto hu = canvas.to_user(h->x, h->y);
  EXPECT_NEAR(hu.x, 50, 1e-6);
  EXPECT_NEAR(hu.y, 85, 1e-6);
  auto tu = canvas.to_user(turn->x, turn->y);
  for (const auto& c : canvas.resize_points()) {
    auto cu = canvas.to_user(c.x, c.y);
    EXPECT_GT(std::hypot(cu.x - hu.x, cu.y - hu.y), 20);
    EXPECT_GT(std::hypot(cu.x - tu.x, cu.y - tu.y), 15);
  }
}

TEST_F(CanvasTest, OnlyAFiniteStepShowsTheGrid) {
  for (double step : {-1.0, 0.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
    canvas.set_grid(10);
    canvas.set_grid(step);
    EXPECT_EQ(canvas.grid(), 0) << step;
  }
  canvas.set_grid(2.5);
  EXPECT_EQ(canvas.grid(), 2.5);
}

TEST_F(CanvasTest, ALaidOutResizeOnAFineGridKeepsTheSmallestSizeInWholeSteps) {
  // A step of 3 does not divide the smallest size (10): the smallest box is four steps.
  std::vector<dd::Box> sizes;
  canvas.callbacks.resized = [&](const std::string&, const dd::Box& b) { sizes.push_back(b); };
  canvas.set_grid(3);
  click(center("shop"));
  auto c = canvas.resize_points()[2];
  double unit = 1 / canvas.units_per_pixel();
  drag(c, {c.x - 1000 * unit, c.y - 1000 * unit});
  ASSERT_EQ(sizes.size(), 1u);
  EXPECT_EQ(sizes[0].width, 12);
  EXPECT_EQ(sizes[0].height, 12);
}

TEST_F(CanvasTest, AShiftResizeInALaidOutViewKeepsTheRatioNotGridSteps) {
  std::vector<dd::Box> sizes;
  canvas.callbacks.resized = [&](const std::string&, const dd::Box& b) { sizes.push_back(b); };
  canvas.set_grid(7);
  click(center("shop"));
  auto corners = canvas.resize_points();
  auto tl = canvas.to_user(corners[0].x, corners[0].y);
  auto br = canvas.to_user(corners[2].x, corners[2].y);
  double w = br.x - tl.x, h = br.y - tl.y;
  double unit = 1 / canvas.units_per_pixel();
  drag(corners[2], {corners[2].x + 0.5 * w * unit, corners[2].y}, {.shift = true});
  ASSERT_EQ(sizes.size(), 1u);
  EXPECT_NEAR(sizes[0].width, 1.5 * w, 1e-6);
  EXPECT_NEAR(sizes[0].height, 1.5 * h, 1e-6);
}

TEST_F(FixedCanvasTest, ASmallResizeFromAnOffGridCornerEndsOnTheGrid) {
  // `b` was pinned before the grid showed, off its points; shrunk as far as it goes, its dragged
  // corner stays on a grid point at least the smallest size away.
  dd::editor::Session off{R"(model {
  a = process "A"
}

view v {
  include *
  layout fixed
  pin a at 3, 3 size 100, 50
}
)"};
  auto r = off.render("v");
  ASSERT_TRUE(r);
  canvas.show(*r);
  canvas.set_grid(5);
  click(at(50, 25));
  drag(canvas.resize_points()[2], at(4, 4));
  EXPECT_EQ(log.back(), "resize a 3,3 12x12");
}

TEST_F(FixedCanvasTest, TheListKeepsItsBackgroundWhileTheGridIsHidden) {
  auto full = canvas.list()->items.size();
  canvas.set_grid(10);
  EXPECT_EQ(canvas.list()->items.size(), full - 1);
  canvas.set_grid(0);
  EXPECT_EQ(canvas.list()->items.size(), full);
  // A new render while the grid shows leaves the background out too.
  canvas.set_grid(10);
  canvas.show(*session.render("v"));
  EXPECT_EQ(canvas.list()->items.size(), full - 1);
  canvas.show(*session.render("v"));
  canvas.set_grid(0);
  canvas.show(*session.render("v"));
  EXPECT_EQ(canvas.list()->items.size(), full);
}

TEST_F(FixedCanvasTest, FarFromTheOriginTheGridIsExactOrNotDrawn) {
  auto far = [&](const std::string& x) {
    dd::editor::Session session{R"(model {
  a = process "A"
}

view v {
  include *
  layout fixed
  pin a at )" + x + R"(, 0 size 100, 50
}
)"};
    auto r = session.render("v");
    if (!r) {
      ADD_FAILURE() << "render failed for x = " << x;  // a lambda returning a value cannot ASSERT
      return std::vector<double>{};
    }
    canvas.show(*r);
    canvas.set_grid(10);
    // Where the vertical lines are: they run down the area, so their ends differ in y (far out,
    // a horizontal line's ends can share an x).
    std::vector<double> at;
    for (const auto& item : canvas.backdrop(800, 600).items)
      if (item.paint.stroke && !item.paint.fill)  // a line, not the background
        for (std::size_t i = 0; i + 1 < item.path.size(); i += 2)
          if (item.path[i].p[0].y != item.path[i + 1].p[0].y) at.push_back(item.path[i].p[0].x);
    return at;
  };
  // Within 2^53 steps of the origin every vertical line is drawn once.
  auto near = far("1000000000000000");
  ASSERT_FALSE(near.empty());
  std::sort(near.begin(), near.end());
  EXPECT_EQ(std::adjacent_find(near.begin(), near.end()), near.end());
  // Past that a double cannot tell neighbouring lines apart (and `x += step` used to never end):
  // no vertical lines.
  EXPECT_TRUE(far("-100000000000000000000").empty());
}

TEST(CanvasBackdrop, OnADarkBackgroundTheLinesAreLight) {
  dd::editor::Session session{R"(model {
  a = process "A"
}

theme dark

view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 50
}
)"};
  Canvas canvas;
  canvas.show(*session.render("v"));
  canvas.set_grid(10);
  auto items = canvas.backdrop(800, 600).items;
  ASSERT_GE(items.size(), 2u);
  const auto& ink = *items.back().paint.stroke;
  EXPECT_EQ(ink.r, 1);
  EXPECT_EQ(ink.g, 1);
  EXPECT_EQ(ink.b, 1);
}

// ── Selecting several items and moving them together ──

TEST_F(CanvasTest, CommandClickTogglesNodesAndLines) {
  click(center("shop"));
  click(center("db"), {.toggle = true});
  auto l = canvas.label_point("user->shop");
  ASSERT_TRUE(l);
  click(*l, {.toggle = true});
  click(*l, {.shift = true});  // a second time: out again, not in twice
  click(center("shop"), {.toggle = true});
  EXPECT_EQ(log, (std::vector<std::string>{"select shop", "select shop db", "select shop db user->shop",
                                           "select shop db", "select db"}));
}

TEST_F(CanvasTest, SelectAllTakesEveryNodeAndLine) {
  EXPECT_TRUE(canvas.select_all());
  const auto& s = canvas.selection();
  EXPECT_EQ(s.elements.size(), 3u);
  EXPECT_EQ(s.relationships, (std::vector<std::string>{"user->shop"}));
  EXPECT_TRUE(canvas.delete_selection());
  EXPECT_EQ(log.back(), "delete 3");
}

TEST_F(CanvasTest, ClickingEmptyCanvasStillClearsTheSelection) {
  click(center("shop"));
  click({1, 1});
  click({1, 1}, {.shift = true});  // with Shift, a click on nothing keeps what is selected
  EXPECT_EQ(log, (std::vector<std::string>{"select shop", "select"}));
}

TEST_F(FixedCanvasTest, ABandSelectsWhatItWhollyEncloses) {
  std::vector<std::string> selected;
  canvas.callbacks.selected = [&](const dd::editor::Selection& s) {
    std::string line;
    for (const auto& e : s.elements) line += e + " ";
    for (const auto& e : s.relationships) line += e + " ";
    selected.push_back(line);
  };
  // Around a only: b, and the line that ends on b, stay out.
  drag(at(-10, -10), at(150, 100));
  EXPECT_EQ(selected.back(), "a ");
  // Around everything, the line's bend point included.
  drag(at(-10, -10), at(450, 200));
  EXPECT_EQ(selected.back(), "a b pay ");
  // A band drawn up and to the left works the same; one over only part of b takes nothing.
  drag(at(150, 100), at(-10, -10));
  EXPECT_EQ(selected.back(), "a ");
  drag(at(350, -10), at(450, 100), {.shift = true});
  EXPECT_EQ(selected.back(), "a ");
  // With Shift (or Command) it adds.
  drag(at(290, -10), at(450, 100), {.toggle = true});
  EXPECT_EQ(selected.back(), "a b ");
}

TEST_F(FixedCanvasTest, ABandShowsWhatItWouldSelectWhileDragging) {
  auto a = at(-10, -10), b = at(450, 200);
  canvas.press(a.x, a.y, 1, {});
  canvas.drag(b.x, b.y, {});
  // The band, an outline on each node and the highlighted line; no handles.
  EXPECT_EQ(canvas.overlay().items.size(), 4u);
  EXPECT_TRUE(canvas.selection().elements.empty());  // reported only on release
  canvas.release(b.x, b.y, {});
  EXPECT_EQ(canvas.selection().elements.size(), 2u);
}

TEST_F(FixedCanvasTest, DraggingOneOfSeveralSelectedNodesMovesThemAll) {
  canvas.callbacks.moved = [&](const std::vector<std::string>& ids, const dd::Point& offset) {
    std::string line = "move";
    for (const auto& id : ids) line += " " + id;
    log.push_back(line + " " + num(offset.x) + "," + num(offset.y));
  };
  click(at(50, 25));
  canvas.press(at(350, 25).x, at(350, 25).y, 1, {.shift = true});
  canvas.release(at(350, 25).x, at(350, 25).y, {.shift = true});
  ASSERT_EQ(canvas.selection().elements.size(), 2u);
  auto from = at(50, 25), to = at(80, 45);
  canvas.press(from.x, from.y, 1, {});
  canvas.drag(to.x, to.y, {});
  // Two outlines, two ghosts and the label.
  EXPECT_EQ(canvas.overlay().items.size(), 5u);
  canvas.release(to.x, to.y, {});
  EXPECT_EQ(log.back(), "move a b 30,20");
  // Option does not ask a group to be placed by its neighbours: it still lands where let go.
  drag(at(50, 25), at(60, 25), {.snap = true});
  EXPECT_EQ(log.back(), "move a b 10,0");
  // With the grid, the grabbed node's corner lands on it and the others keep their places by it.
  canvas.set_grid(25);
  drag(at(50, 25), at(65, 25));
  EXPECT_EQ(log.back(), "move a b 25,0");
}

TEST_F(FixedCanvasTest, AGroupMoveThatEndsWhereItStartedWritesNothing) {
  int moves = 0;
  canvas.callbacks.moved = [&](const std::vector<std::string>&, const dd::Point&) { ++moves; };
  canvas.select_all();
  canvas.set_grid(50);
  drag(at(50, 25), at(57, 25));  // past the threshold, but the corner snaps back to 0, 0
  EXPECT_EQ(moves, 0);
}

TEST_F(FixedCanvasTest, DraggingANodeOutsideTheSelectionMovesItAlone) {
  int moves = 0;
  canvas.callbacks.moved = [&](const std::vector<std::string>&, const dd::Point&) { ++moves; };
  click(at(50, 25));
  drag(at(350, 25), at(380, 25));  // b was not selected: the press selects it alone, and it drops
  EXPECT_EQ(moves, 0);
  EXPECT_EQ(log.back(), "drop b");
}

TEST_F(CanvasTest, AClickOnOneOfSeveralSelectedSelectsItAlone) {
  click(center("shop"));
  click(center("db"), {.shift = true});
  auto p = center("db");
  canvas.press(p.x, p.y, 1, {});
  EXPECT_EQ(canvas.selection().elements.size(), 2u);  // still both while the button is down: a drag moves both
  canvas.release(p.x, p.y, {});
  EXPECT_EQ(log.back(), "select db");
}
