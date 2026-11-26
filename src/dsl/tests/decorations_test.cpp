// View-level notes, free text and boxes: parse, print, JSON, layout, render, edits and completion.

#include <gtest/gtest.h>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/lsp/features.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/model/views.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const char* const kSource = R"(model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    api = container "API"
    db  = container "Orders DB" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.api "calls"
  shop.api -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  text "Checkout" style "fontSize=18;fontStyle=1"
  note consistency "Eventually consistent" below db
  note "Owned by Web" left of web
  box pci "PCI scope" around api db
  note "Remember the cache"
  note "Pinned" at 10, -90 size 120, 40
  box at 0, 0 size 50, 50 style "dashed=0"
}
)";

std::vector<std::string> messages(const ParseResult& r) {
  std::vector<std::string> out;
  for (const auto& d : r.diagnostics) out.push_back(d.message);
  return out;
}

const View& checkout(const ParseResult& r) { return *r.model.views.find("checkout"); }

bool overlaps(const Box& a, const Box& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

Geometry lay_out(const Model& model, const char* view) {
  auto resolved = test::ok(resolve_view(model, view));
  return test::ok(layout(resolved, create_measure()));
}

bool has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(Decorations, LowerIntoTheView) {
  auto r = parse(kSource);
  EXPECT_TRUE(r.diagnostics.empty()) << ::testing::PrintToString(messages(r));
  const auto& d = checkout(r).decorations;
  ASSERT_EQ(d.size(), 7u);

  EXPECT_EQ(d[0].kind, DecorationKind::Text);
  EXPECT_EQ(d[0].text, "Checkout");
  ASSERT_TRUE(d[0].style);
  EXPECT_EQ(*d[0].style->find("fontSize"), "18");

  EXPECT_EQ(d[1].kind, DecorationKind::Note);
  EXPECT_EQ(d[1].name, "consistency");
  EXPECT_EQ(d[1].placement, Placement::Below);
  EXPECT_EQ(d[1].reference, "shop.db");  // resolved in the view's scope

  EXPECT_EQ(d[2].placement, Placement::LeftOf);
  EXPECT_EQ(d[2].reference, "shop.web");
  EXPECT_FALSE(d[2].name);

  EXPECT_EQ(d[3].kind, DecorationKind::Box);
  EXPECT_EQ(d[3].name, "pci");
  EXPECT_EQ(d[3].around, (std::vector<std::string>{"shop.api", "shop.db"}));

  EXPECT_FALSE(d[4].placement);
  EXPECT_FALSE(d[4].at);

  ASSERT_TRUE(d[5].at);
  EXPECT_EQ(d[5].at->y, -90);
  EXPECT_EQ(d[5].width, 120);

  EXPECT_EQ(d[6].kind, DecorationKind::Box);
  EXPECT_FALSE(d[6].text);
  EXPECT_EQ(d[6].height, 50);

  ASSERT_EQ(r.source_map.views.at("checkout").decorations.size(), 7u);
  auto span = r.source_map.views.at("checkout").decorations[3];
  EXPECT_EQ(std::string(kSource).substr(span.from, span.to - span.from), "box pci \"PCI scope\" around api db");
}

TEST(Decorations, PrintParseRoundTrip) {
  auto r = parse(kSource);
  std::string printed = print(r.model);
  EXPECT_TRUE(has(printed, "  note consistency \"Eventually consistent\" below shop.db\n"));
  EXPECT_TRUE(has(printed, "  box pci \"PCI scope\" around shop.api shop.db\n"));
  EXPECT_TRUE(has(printed, "  note \"Pinned\" at 10, -90 size 120, 40\n"));
  auto again = parse(printed);
  EXPECT_TRUE(again.diagnostics.empty()) << ::testing::PrintToString(messages(again));
  EXPECT_TRUE(again.model == r.model);
  EXPECT_EQ(print(again.model), printed);
}

TEST(Decorations, SurviveJson) {
  auto r = parse(kSource);
  auto back = test::ok(model_from_json(to_json(r.model)));
  EXPECT_TRUE(back == r.model);
  // Views without decorations keep their JSON as it was.
  auto plain = parse("model { a = process }\nview v { include * }\n");
  EXPECT_FALSE(has(to_json(plain.model).dump(), "decorations"));
}

TEST(Decorations, ReportProblems) {
  auto r = parse(R"(model { a = process  b = process }
view v {
  include *
  note n "one" below a
  note n "two" below b
  note "three" right of nope
  box "nothing to frame"
}
)");
  auto m = messages(r);
  EXPECT_NE(std::find(m.begin(), m.end(), "Duplicate decoration \"n\" in view \"v\""), m.end());
  EXPECT_NE(std::find(m.begin(), m.end(), "Unknown element \"nope\""), m.end());
  EXPECT_EQ(r.model.views.find("v")->decorations.size(), 1u);
}

TEST(Decorations, KeywordsStayContextual) {
  // Elements may still be called note, text or box, and a list ends where a decoration starts.
  auto r = parse(R"(model { text = process  box = process  note = note "memo" }
view v {
  include text box
  note "about text" right of text
  align text box
}
)");
  EXPECT_TRUE(r.diagnostics.empty()) << ::testing::PrintToString(messages(r));
  const auto& v = *r.model.views.find("v");
  EXPECT_EQ(v.include, (std::vector<std::string>{"text", "box"}));
  ASSERT_EQ(v.decorations.size(), 1u);
  EXPECT_EQ(v.decorations[0].reference, "text");
  EXPECT_EQ(v.hints.size(), 1u);
}

TEST(Decorations, AreLaidOutBesideWhatTheyAnnotate) {
  auto r = parse(kSource);
  Geometry g = lay_out(r.model, "checkout");
  ASSERT_EQ(g.decorations.size(), 7u);
  for (std::size_t i = 0; i < g.decorations.size(); ++i) EXPECT_EQ(g.decorations[i].index, i);
  const Box& web = *g.nodes.find("shop.web");
  const Box& api = *g.nodes.find("shop.api");
  const Box& db = *g.nodes.find("shop.db");

  const auto& below = g.decorations[1];
  EXPECT_GE(below.box.y, db.y + db.height);
  EXPECT_EQ(below.connector.size(), 2u);

  const auto& left = g.decorations[2];
  EXPECT_LE(left.box.x + left.box.width, web.x);

  const Box& pci = g.decorations[3].box;
  for (const Box* b : {&api, &db}) {
    EXPECT_LT(pci.x, b->x);
    EXPECT_LT(pci.y, b->y);
    EXPECT_GT(pci.x + pci.width, b->x + b->width);
    EXPECT_GT(pci.y + pci.height, b->y + b->height);
  }

  // Unanchored: text above everything, notes to the right of everything.
  double top = std::numeric_limits<double>::infinity(), right = -top;
  for (const auto& n : g.nodes) {
    top = std::min(top, n.y);
    right = std::max(right, n.x + n.width);
  }
  EXPECT_LT(g.decorations[0].box.y + g.decorations[0].box.height, top);
  EXPECT_GT(g.decorations[4].box.x, right);

  EXPECT_EQ(g.decorations[5].box, (Box{10, -90, 120, 40}));
  EXPECT_EQ(g.decorations[6].box, (Box{0, 0, 50, 50}));

  // Notes and text never cover a node.
  for (const auto& d : g.decorations) {
    if (r.model.views.find("checkout")->decorations[d.index].kind == DecorationKind::Box) continue;
    if (d.index == 5) continue;  // placed by hand
    for (const auto& n : g.nodes) EXPECT_FALSE(overlaps(d.box, n)) << d.index;
  }
}

TEST(Decorations, AttachedNotesStepAwayFromEachOther) {
  auto r = parse(R"(model { a = process }
view v {
  include *
  note "first" right of a
  note "second" right of a
}
)");
  Geometry g = lay_out(r.model, "v");
  ASSERT_EQ(g.decorations.size(), 2u);
  EXPECT_FALSE(overlaps(g.decorations[0].box, g.decorations[1].box));
}

TEST(Decorations, WorkInSequenceAndFixedViews) {
  auto r = parse(R"(model { a = process  b = process }
view s {
  include a b
  layout sequence
  sequence { a -> b "hi" }
  note "about b" right of b
}
view f {
  include *
  layout fixed
  pin a at 0, 0
  text "caption" below a
}
)");
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_EQ(lay_out(r.model, "s").decorations.size(), 1u);
  Geometry f = lay_out(r.model, "f");
  ASSERT_EQ(f.decorations.size(), 1u);
  EXPECT_GE(f.decorations[0].box.y, f.nodes.find("a")->y + f.nodes.find("a")->height);
}

TEST(Decorations, Render) {
  auto r = parse(kSource);
  auto resolved = test::ok(resolve_view(r.model, "checkout"));
  Geometry g = test::ok(layout(resolved, create_measure()));
  std::string svg = render_svg(resolved, g);
  EXPECT_TRUE(has(svg, "class=\"dd-decoration dd-note\" data-decoration=\"1\" data-name=\"consistency\""));
  EXPECT_TRUE(has(svg, "class=\"dd-decoration dd-box\" data-decoration=\"3\" data-name=\"pci\""));
  EXPECT_TRUE(has(svg, ">PCI scope</text>"));
  EXPECT_TRUE(has(svg, ">Eventually consistent</text>"));
  EXPECT_TRUE(has(svg, "font-size=\"18\" font-weight=\"600\""));
  EXPECT_TRUE(has(svg, "class=\"dd-note-connector\""));
  // The frame takes in the text above the diagram.
  auto vb = svg.find("viewBox=\"");
  ASSERT_NE(vb, std::string::npos);
  double min_y = std::stod(svg.substr(svg.find(' ', vb + 9) + 1));
  EXPECT_LE(min_y, g.decorations[0].box.y);
  // Boxes are drawn before nodes, notes after them.
  EXPECT_LT(svg.find("dd-box"), svg.find("dd-node"));
  EXPECT_GT(svg.find("dd-note\""), svg.find("dd-node"));
}

TEST(Decorations, GoWithARemovedElement) {
  auto r = test::ok(update_source(kSource, op::RemoveElement{"shop.db"}));
  EXPECT_FALSE(has(r.source, "Eventually consistent"));
  EXPECT_FALSE(has(r.source, "PCI scope"));
  EXPECT_TRUE(has(r.source, "note \"Owned by Web\" left of web"));
  auto again = parse(r.source);
  EXPECT_TRUE(again.diagnostics.empty()) << ::testing::PrintToString(messages(again));
  // The model operation agrees with the text patch.
  auto model = test::apply_ok(parse(kSource).model, op::RemoveElement{"shop.db"}).model;
  EXPECT_EQ(model.views.find("checkout")->decorations.size(), 5u);
}

TEST(Decorations, Complete) {
  std::string text = std::string(kSource);
  auto at = [&](const std::string& insert) {
    std::string src = text;
    auto pos = src.rfind("}\n");
    src.insert(pos, insert);
    auto parsed = parse(src);
    std::vector<std::string> labels;
    for (const auto& c : lsp::completions(src, pos + insert.size(), parsed)) labels.push_back(c.label);
    return labels;
  };
  auto view_keywords = at("  ");
  EXPECT_NE(std::find(view_keywords.begin(), view_keywords.end(), "note"), view_keywords.end());
  EXPECT_NE(std::find(view_keywords.begin(), view_keywords.end(), "box"), view_keywords.end());
  auto anchors = at("  note \"x\" ");
  EXPECT_NE(std::find(anchors.begin(), anchors.end(), "right of"), anchors.end());
  auto box = at("  box \"x\" ");
  EXPECT_NE(std::find(box.begin(), box.end(), "around"), box.end());
  auto elements = at("  box \"x\" around ");
  EXPECT_NE(std::find(elements.begin(), elements.end(), "shop.web"), elements.end());
}
