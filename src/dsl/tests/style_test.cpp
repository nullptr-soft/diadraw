// `style` blocks, built-in and user-defined themes: syntax, lowering, print, JSON, rendering and completion.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/library/library.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/lsp/features.hpp"
#include "diadraw/model/json.hpp"
#include "diadraw/render/display_list.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;

namespace {

const char* const kStyled = R"(theme dark

style * {
  fontSize 14
}

style container {
  fill "#438dd5"
  stroke "#3c7fc0"
}

style tag "external" {
  fill "#999999"
  dashed true
}

style relationship tag "async" {
  dashed 1
  endArrow open
}

model {
  user = person "User"
  shop = system "Shop" {
    web = container "Web"
    db = container "DB" tag "external"
  }
  pay = system "Payments" tag "external"
  user -> shop.web "browses"
  shop.web -> pay "charges" tag "async"
}

view all {
  include *
  layout fixed
  pin user at 0, 0
  pin shop at 0, 120
  pin pay at 300, 120
}

view light of shop {
  include *
  layout fixed
  theme default
  pin web at 0, 0
  pin db at 200, 0
}
)";

std::vector<std::string> messages(const dsl::ParseResult& r) {
  std::vector<std::string> out;
  for (const auto& d : r.diagnostics) out.push_back(d.message);
  return out;
}

std::string render(const Model& model, std::string_view view_id) {
  auto view = dd::test::ok(resolve_view(model, view_id));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  return render_svg(view, geometry);
}

using dd::test::item_svg;

bool has(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::vector<std::string> labels(const std::vector<lsp::Completion>& items) {
  std::vector<std::string> out;
  for (const auto& c : items) out.push_back(c.label);
  return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

TEST(Styles, LowersStyleBlocksAndThemes) {
  auto r = dsl::parse(kStyled);
  EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
  const Model& m = r.model;
  EXPECT_EQ(m.theme, "dark");
  ASSERT_EQ(m.styles.size(), 4u);
  EXPECT_EQ(m.styles[0].target, StyleTarget::Elements);
  EXPECT_FALSE(m.styles[0].kind);
  EXPECT_EQ(*m.styles[0].style.find("fontSize"), "14");
  EXPECT_EQ(m.styles[1].kind, "container");
  EXPECT_EQ(*m.styles[1].style.find("fill"), "#438dd5");
  EXPECT_EQ(m.styles[2].tag, "external");
  EXPECT_EQ(*m.styles[2].style.find("dashed"), "1");  // `true` is stored as draw.io's 1
  EXPECT_EQ(m.styles[3].target, StyleTarget::Relationships);
  EXPECT_EQ(m.styles[3].tag, "async");
  EXPECT_EQ(*m.styles[3].style.find("endArrow"), "open");
  EXPECT_FALSE(m.views.find("all")->theme);
  EXPECT_EQ(m.views.find("light")->theme, "default");
  ASSERT_EQ(r.source_map.styles.size(), 4u);
  EXPECT_TRUE(r.source_map.theme);
  EXPECT_TRUE(r.source_map.views.at("light").theme);
}

TEST(Styles, CombinesKindAndTagSelectors) {
  auto r = dsl::parse("style container tag \"legacy\" { stroke \"#f00\" }\nmodel { a = container \"A\" tag \"legacy\" }\n");
  EXPECT_TRUE(r.diagnostics.empty());
  ASSERT_EQ(r.model.styles.size(), 1u);
  EXPECT_EQ(r.model.styles[0].kind, "container");
  EXPECT_EQ(r.model.styles[0].tag, "legacy");
  EXPECT_EQ(dsl::print_style_rule(r.model.styles[0]), "style container tag \"legacy\" {\n  stroke \"#f00\"\n}");
}

TEST(Styles, ReportsUnknownThemesAndRepeatedThemes) {
  auto r = dsl::parse("theme neon\ntheme dark\nmodel { a = process }\nview v { include * theme sepia }\n");
  auto m = messages(r);
  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[0], "Unknown theme \"neon\" (themes: default, dark, mono)");
  EXPECT_EQ(m[1], "The theme is already set; the last `theme` wins");
  EXPECT_EQ(m[2], "Unknown theme \"sepia\" (themes: default, dark, mono)");
  EXPECT_EQ(r.diagnostics[0].severity, dsl::Severity::Warning);
  EXPECT_EQ(r.model.theme, "dark");
}

TEST(Styles, RecoversFromErrorsInsideAStyleBlock) {
  auto r = dsl::parse("style person {\n  fill\n  stroke \"#000\"\n}\nmodel { a = person }\n");
  ASSERT_FALSE(r.diagnostics.empty());
  EXPECT_EQ(r.diagnostics[0].line, 3);
  EXPECT_TRUE(r.model.elements.contains("a"));
  ASSERT_EQ(r.model.styles.size(), 1u);
}

TEST(Styles, ThemeAndStyleStayOrdinaryNamesInTheModel) {
  auto r = dsl::parse("model {\n  theme = component \"Theme\"\n  style = component \"Style\"\n  theme -> style\n}\n");
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_TRUE(r.model.elements.contains("theme"));
  EXPECT_TRUE(r.model.elements.contains("style"));
  EXPECT_TRUE(r.model.styles.empty());
}

TEST(Styles, PrintsCanonicallyAndRoundTrips) {
  auto r = dsl::parse(kStyled);
  std::string printed = dsl::print(r.model);
  EXPECT_EQ(printed.rfind("theme dark\n\nstyle * {\n  fontSize 14\n}\n\nstyle container {\n", 0), 0u) << printed;
  EXPECT_TRUE(has(printed, "style tag \"external\" {\n  fill \"#999999\"\n  dashed 1\n}"));
  EXPECT_TRUE(has(printed, "style relationship tag \"async\" {\n  dashed 1\n  endArrow open\n}"));
  EXPECT_TRUE(has(printed, "  layout fixed direction down\n  theme default\n"));
  auto again = dsl::parse(printed);
  EXPECT_TRUE(again.diagnostics.empty());
  EXPECT_TRUE(again.model == r.model);
  EXPECT_EQ(dsl::print(again.model), printed);
  EXPECT_EQ(dsl::print_style_value("12.5"), "12.5");
  EXPECT_EQ(dsl::print_style_value("open"), "open");
  EXPECT_EQ(dsl::print_style_value("6 4"), "\"6 4\"");
  EXPECT_EQ(dsl::print_style_value(""), "\"\"");
}

TEST(Styles, SurviveJson) {
  auto r = dsl::parse(kStyled);
  Json j = to_json(r.model);
  ASSERT_TRUE(j.get("styles"));
  EXPECT_EQ(j.get("theme")->as_string(), "dark");
  auto back = dd::test::ok(model_from_json(j));
  EXPECT_TRUE(back == r.model);
  // Documents without styles keep their JSON shape.
  Json plain = to_json(dsl::parse("model { a = process }").model);
  EXPECT_FALSE(plain.get("styles"));
  EXPECT_FALSE(plain.get("theme"));
}

TEST(Styles, RulesApplyByKindThenTagAndInlineStylesWin) {
  auto r = dsl::parse(R"(style * { stroke "#111111" }
style process { fill "#222222" stroke "#333333" }
style tag "hot" { fill "#ff0000" }
model {
  a = process "A"
  b = process "B" tag "hot"
  c = process "C" tag "hot" style "fill=#00ff00"
  d = decision "D"
}
)");
  const auto& rules = r.model.styles;
  auto style_of = [&](const char* id) { return rule_style(rules, *r.model.elements.find(id)); };
  EXPECT_EQ(*style_of("a").find("fill"), "#222222");
  EXPECT_EQ(*style_of("a").find("stroke"), "#333333");  // the kind beats `*`
  EXPECT_EQ(*style_of("b").find("fill"), "#ff0000");    // the tag beats the kind
  EXPECT_EQ(*style_of("d").find("stroke"), "#111111");
  EXPECT_FALSE(style_of("d").find("fill"));

  std::string svg = render(with_default_view(r.model), kDefaultViewId);
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "a"), "fill=\"#222222\" stroke=\"#333333\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "b"), "fill=\"#ff0000\" stroke=\"#333333\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "c"), "fill=\"#00ff00\" stroke=\"#333333\""));
  // A style block keeps the theme's look for what it does not set: text colour and bold labels.
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "d"), "fill=\"#fef9c3\" stroke=\"#111111\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "d"), "font-weight=\"600\""));
}

TEST(Styles, RendersThemesAndRelationshipRules) {
  auto r = dsl::parse(kStyled);
  std::string dark = render(r.model, "all");
  EXPECT_TRUE(has(dark, "class=\"dd-background\""));
  EXPECT_TRUE(has(dark, "fill=\"#0f172a\"/>"));                                      // dark background
  EXPECT_TRUE(has(item_svg(dark, "dd-node", "user"), "fill=\"#1e40af\""));           // dark person
  EXPECT_TRUE(has(item_svg(dark, "dd-node", "pay"), "fill=\"#999999\""));            // tag rule
  EXPECT_TRUE(has(item_svg(dark, "dd-node", "pay"), "stroke-dasharray=\"6 4\""));
  EXPECT_TRUE(has(item_svg(dark, "dd-node", "pay"), "font-size=\"14\""));            // `*` rule
  std::string async = item_svg(dark, "dd-edge", "shop.web-&gt;pay");
  EXPECT_TRUE(has(async, "stroke=\"#94a3b8\""));                                      // dark edge colour
  EXPECT_TRUE(has(async, "stroke-dasharray=\"6 4\""));
  EXPECT_TRUE(has(async, "marker-end=\"url(#dd-m-open-f-94a3b8)\""));
  EXPECT_FALSE(has(item_svg(dark, "dd-edge", "user-&gt;shop.web"), "stroke-dasharray"));

  // The view's own theme wins over the document's.
  std::string light = render(r.model, "light");
  EXPECT_TRUE(has(light, "fill=\"#ffffff\"/>"));
  EXPECT_TRUE(has(item_svg(light, "dd-node", "shop.web"), "fill=\"#438dd5\" stroke=\"#3c7fc0\""));
}

TEST(Styles, RelationshipEndsHaveTheirOwnShapes) {
  auto r = dsl::parse(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
  d = process "D"
  a -> b style "startArrow=ERmandOne;endArrow=ERzeroToMany"
  a -> c style "endArrow=cross;startArrow=halfCircle"
  a -> d style "endArrow=box;endFill=0"
}
view v {
  include *
  layout fixed
  pin a at 0, 0
  pin b at 300, 0
  pin c at 300, 150
  pin d at 300, 300
}
)");
  std::string svg = render(r.model, "v");
  // Crow's foot ends: the two-symbol ones get a box twice as long.
  EXPECT_TRUE(has(svg, "<marker id=\"dd-m-ERmandOne-f-000000-s\" viewBox=\"0 0 10 10\" refX=\"9\""));
  EXPECT_TRUE(has(svg, "<marker id=\"dd-m-ERzeroToMany-f-000000\" viewBox=\"0 0 20 10\" refX=\"19\" refY=\"5\" "
                       "markerWidth=\"14\" markerHeight=\"7\" orient=\"auto\"><circle"));
  EXPECT_TRUE(has(svg, "<path d=\"M19,0 L12,5 L19,10\""));
  EXPECT_TRUE(has(svg, "<marker id=\"dd-m-cross-f-000000\""));
  EXPECT_TRUE(has(svg, "<marker id=\"dd-m-halfCircle-f-000000-s\""));
  // A hollow square is filled with the theme's surface.
  EXPECT_TRUE(has(svg, "<path d=\"M1,1 H9 V9 H1 z\" fill=\"#ffffff\" stroke=\"#000000\"/>"));
}

TEST(Styles, EndsThatAlreadyHadAShapeAreDrawnAsBefore) {
  // Values with a shape before these ends were added, and the approximations kept for draw.io's
  // other names, write exactly what they wrote.
  auto marker = [](const std::string& value) {
    Style style{{"endArrow", value}, {"stroke", "#000000"}};
    std::string svg = line_sample_svg(style, 60, 20);
    auto at = svg.find("<marker");
    return svg.substr(at, svg.find("</marker>") - at);
  };
  EXPECT_EQ(marker("classic"), "<marker id=\"dd-m-classic-f-000000\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
                               "markerWidth=\"7\" markerHeight=\"7\" orient=\"auto\"><path d=\"M0,0 L10,5 L0,10 z\" "
                               "fill=\"#000000\" stroke=\"#000000\"/>");
  EXPECT_EQ(marker("open"), "<marker id=\"dd-m-open-f-000000\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
                            "markerWidth=\"7\" markerHeight=\"7\" orient=\"auto\"><path d=\"M1,1 L9,5 L1,9\" "
                            "fill=\"none\" stroke=\"#000000\" stroke-width=\"1.4\"/>");
  EXPECT_EQ(marker("dash"), "<marker id=\"dd-m-dash-f-000000\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
                            "markerWidth=\"7\" markerHeight=\"7\" orient=\"auto\"><path d=\"M2,9 L6,1\" "
                            "fill=\"none\" stroke=\"#000000\" stroke-width=\"1.4\"/>");
  EXPECT_TRUE(has(marker("diamond"), "><path d=\"M0,5 L5,1 L10,5 L5,9 z\" fill=\"#000000\" stroke=\"#000000\"/>"));
  EXPECT_TRUE(has(marker("oval"), "><circle cx=\"5\" cy=\"5\" r=\"4\" fill=\"#000000\" stroke=\"#000000\"/>"));
  {
    // Hollow, and at the start.
    Style style{{"startArrow", "block"}, {"startFill", "0"}, {"endArrow", "none"}, {"stroke", "#000000"}};
    EXPECT_TRUE(has(line_sample_svg(style, 60, 20),
                    "<marker id=\"dd-m-block-o-000000-s\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" "
                    "markerHeight=\"7\" orient=\"auto-start-reverse\"><path d=\"M0,0 L10,5 L0,10 z\" fill=\"#ffffff\" "
                    "stroke=\"#000000\"/></marker>"));
  }
  EXPECT_TRUE(has(marker("circle"), "dd-m-oval-f-"));
  EXPECT_TRUE(has(marker("ERunknown"), "dd-m-open-f-"));
  EXPECT_TRUE(has(marker("diamondThin"), "dd-m-diamond-f-"));
  EXPECT_TRUE(has(marker("whatever"), "dd-m-classic-f-"));
}

TEST(Styles, RelationshipLineStyles) {
  auto r = dsl::parse(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
  d = process "D"
  e = process "E"
  a -> b style "curved=1"
  a -> c style "rounded=1"
  a -> d style "double=1;strokeWidth=2"
  a -> e style "dashed=1;dashPattern=1 3"
}
view v {
  include *
  layout fixed
  pin a at 0, 0
  pin b at 300, 100
  pin c at 300, 200
  pin d at 300, 0
  pin e at 300, 300
  route "a->b" via 150, 30 150, 130
  route "a->c" via 150, 30 150, 230
}
)");
  std::string svg = render(r.model, "v");
  std::string curved = item_svg(svg, "dd-edge", "a-&gt;b");
  EXPECT_TRUE(has(curved, " Q150,30 150,80 Q150,130 ")) << curved;
  std::string rounded = item_svg(svg, "dd-edge", "a-&gt;c");
  EXPECT_TRUE(has(rounded, " Q150,30 150,40 L150,220 Q150,230 ")) << rounded;
  // Two lines with the background between them; the ends keep the plain width's size.
  std::string twin = item_svg(svg, "dd-edge", "a-&gt;d");
  EXPECT_TRUE(has(twin, "stroke=\"#000000\" stroke-width=\"6\"/>")) << twin;
  EXPECT_TRUE(has(twin, "stroke=\"#ffffff\" stroke-width=\"2\"/>")) << twin;
  EXPECT_TRUE(has(twin, "stroke=\"none\" stroke-width=\"2\" marker-end=")) << twin;
  EXPECT_TRUE(has(item_svg(svg, "dd-edge", "a-&gt;e"), "stroke-dasharray=\"1 3\""));
}

TEST(Styles, ElementOutlinesTakeADashPattern) {
  auto r = dsl::parse(R"(model {
  a = process "A" style "dashed=1;dashPattern=1 3"
  b = process "B" style "dashed=1"
  c = process "C" style "dashPattern=1 3"
}
view v {
  include *
  box frame around a b style "dashPattern=8 3 1 3"
}
)");
  std::string svg = render(r.model, "v");
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "a"), "stroke-dasharray=\"1 3\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "b"), "stroke-dasharray=\"6 4\""));
  // A pattern on a solid outline does not dash it.
  EXPECT_FALSE(has(item_svg(svg, "dd-node", "c"), "stroke-dasharray"));
  // A box is dashed unless it says otherwise, so its pattern shows.
  EXPECT_TRUE(has(svg, "stroke-dasharray=\"8 3 1 3\"")) << svg;
}

TEST(Styles, LineStylesSurviveOddRoutes) {
  auto r = dsl::parse(R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
  a -> b style "rounded=1"
  a -> c style "curved=1;rounded=1"
}
view v {
  include *
  layout fixed
  pin a at 0, 0
  pin b at 300, 200
  pin c at 300, 0
  route "a->b" via 150, 30 150, 30 150, 230
}
)");
  std::string svg = render(r.model, "v");
  // A bend point given twice is a corner of no length: drawn as a plain point, never NaN.
  std::string twice = item_svg(svg, "dd-edge", "a-&gt;b");
  EXPECT_FALSE(has(twice, "NaN")) << twice;
  EXPECT_TRUE(has(twice, " L150,30 ")) << twice;
  // A straight line has no corner to curve or round.
  std::string straight = item_svg(svg, "dd-edge", "a-&gt;c");
  EXPECT_FALSE(has(straight, " Q")) << straight;
}

TEST(Styles, NativeDrawingsShowStartMarkersOnCurves) {
  // The first segment is a curve whose first control point is the vertex itself, after a stray
  // move: the start marker still turns along the line.
  auto list = draw::display_list_from_svg(
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="60" height="20" viewBox="0 0 60 20"><defs>)svg"
      R"svg(<marker id="m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">)svg"
      R"svg(<path d="M0,0 L10,5 L0,10 z" fill="#000"/></marker></defs>)svg"
      R"svg(<path d="M0,0 M10,10 Q10,10 50,10" fill="none" stroke="#000" marker-start="url(#m)"/></svg>)svg");
  ASSERT_TRUE(list);
  std::vector<draw::Matrix> turns;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Transform) turns.push_back(item.matrix);
  ASSERT_EQ(turns.size(), 1u);
  EXPECT_NEAR(turns[0].a, -0.7, 1e-9);  // half a turn (at the 7/10 scale): pointing left, out of the line
  EXPECT_NEAR(turns[0].b, 0, 1e-9);
}

TEST(Styles, NativeDrawingsDrawASharedMarkerForEachLineItEnds) {
  // One marker ends two lines of different diagram items and widths: it is read once, but each
  // copy belongs to its own line, at that line's place and scale.
  auto list = draw::display_list_from_svg(
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="40" viewBox="0 0 100 40"><defs>)svg"
      R"svg(<marker id="m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">)svg"
      R"svg(<path d="M0,0 L10,5 L0,10 z" fill="#000"/></marker></defs>)svg"
      R"svg(<g class="dd-edge" data-id="a"><path d="M0,10 L50,10" stroke="#000" marker-end="url(#m)"/></g>)svg"
      R"svg(<g class="dd-edge" data-id="b"><path d="M0,30 L90,30" stroke="#000" stroke-width="2" )svg"
      R"svg(marker-end="url(#m)"/></g></svg>)svg");
  ASSERT_TRUE(list);
  std::vector<std::pair<std::string, draw::Matrix>> turns;
  std::vector<std::string> heads;
  for (const auto& item : list->items) {
    if (item.kind == draw::Item::Transform) turns.emplace_back(item.owner, item.matrix);
    if (item.kind == draw::Item::Path && item.paint.fill && !item.paint.stroke) heads.push_back(item.owner);
  }
  ASSERT_EQ(turns.size(), 2u);
  EXPECT_EQ(turns[0].first, "a");
  EXPECT_EQ(turns[1].first, "b");
  EXPECT_NEAR(turns[0].second.a, 0.7, 1e-9);
  EXPECT_NEAR(turns[1].second.a, 1.4, 1e-9);  // twice the stroke width, twice the size
  // The vertex less refX/refY (9, 5) at each scale.
  EXPECT_NEAR(turns[0].second.e, 50 - 9 * 0.7, 1e-9);
  EXPECT_NEAR(turns[0].second.f, 10 - 5 * 0.7, 1e-9);
  EXPECT_NEAR(turns[1].second.e, 90 - 9 * 1.4, 1e-9);
  EXPECT_NEAR(turns[1].second.f, 30 - 5 * 1.4, 1e-9);
  EXPECT_EQ(heads, (std::vector<std::string>{"a", "b"}));
}

TEST(Styles, NativeDrawingsStopAtAMarkerThatEndsItsOwnContent) {
  // A marker whose own path ends in it again is drawn once, instead of recursing for ever.
  auto list = draw::display_list_from_svg(
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="60" height="20" viewBox="0 0 60 20"><defs>)svg"
      R"svg(<marker id="m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">)svg"
      R"svg(<path d="M0,0 L10,5" stroke="#000" marker-end="url(#m)"/></marker></defs>)svg"
      R"svg(<path d="M0,10 L50,10" stroke="#000" marker-end="url(#m)"/></svg>)svg");
  ASSERT_TRUE(list);
  std::size_t paths = 0;
  for (const auto& item : list->items) paths += item.kind == draw::Item::Path;
  EXPECT_EQ(paths, 2u);  // the line and the marker's own path; the marker inside it draws nothing
}

TEST(Styles, NativeDrawingsShowStartMarkersToo) {
  Style style{{"startArrow", "block"}, {"endArrow", "block"}, {"stroke", "#000000"}};
  auto list = draw::display_list_from_svg(line_sample_svg(style, 100, 20));
  ASSERT_TRUE(list);
  // Each end is drawn in a transform turned along the line: the start one half a turn round.
  std::vector<draw::Matrix> turns;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Transform) turns.push_back(item.matrix);
  ASSERT_EQ(turns.size(), 2u);
  EXPECT_LT(turns[0].a, 0);  // the start points left, out of the line
  EXPECT_GT(turns[1].a, 0);  // the end points right
  EXPECT_NEAR(turns[0].e, 11.875, 0.01);  // the vertex at x=4, less refX scaled
  EXPECT_NEAR(turns[1].e, 88.125, 0.01);
}

TEST(Styles, RestyleContainerBoundaries) {
  auto r = dsl::parse(R"(style system { stroke "#ff00ff" fontColor "#00ffff" dashed false }
model {
  s = system "S" { a = container "A" }
}
view v { include s s.a }
)");
  std::string svg = render(r.model, "v");
  std::string boundary = item_svg(svg, "dd-node", "s");
  EXPECT_TRUE(has(boundary, "fill=\"none\" stroke=\"#ff00ff\" stroke-width=\"1.25\"/>"));
  EXPECT_TRUE(has(boundary, "fill=\"#00ffff\""));
}

TEST(Styles, OnlyKnownThemesArePickedAndUnknownOnesFallBack) {
  auto names = theme_names();
  EXPECT_EQ(names, (std::vector<std::string>{"default", "dark", "mono"}));
  EXPECT_EQ(find_theme("dark")->background, "#0f172a");
  EXPECT_FALSE(find_theme("neon"));
  ResolvedView v;
  v.theme = "neon";
  EXPECT_EQ(theme_for(v).name, "default");
  v.theme = "mono";
  EXPECT_EQ(theme_for(v).name, "mono");
}

TEST(Styles, CompletesThemesSelectorsKeysAndValues) {
  auto at_end = [](const std::string& text) {
    auto r = dsl::parse(text);
    return labels(lsp::completions(text, text.size(), r, nullptr));
  };
  auto at_marker = [](std::string text) {
    auto at = text.find('|');
    text.erase(at, 1);
    auto r = dsl::parse(text);
    return labels(lsp::completions(text, at, r, nullptr));
  };
  EXPECT_EQ(at_end("theme "), (std::vector<std::string>{"default", "dark", "mono"}));
  auto top = at_end("model { a = process }\n");
  EXPECT_TRUE(contains(top, "style"));
  EXPECT_TRUE(contains(top, "theme"));
  auto selectors = at_end("style ");
  EXPECT_TRUE(contains(selectors, "relationship"));
  EXPECT_TRUE(contains(selectors, "container"));
  EXPECT_TRUE(contains(selectors, "*"));
  auto element_keys = at_marker("style person {\n  |\n}\n");
  EXPECT_TRUE(contains(element_keys, "fill"));
  EXPECT_FALSE(contains(element_keys, "endArrow"));
  auto edge_keys = at_marker("style relationship {\n  |\n}\n");
  EXPECT_TRUE(contains(edge_keys, "endArrow"));
  EXPECT_FALSE(contains(edge_keys, "fill"));
  EXPECT_TRUE(contains(at_marker("style relationship {\n  endArrow |\n}\n"), "diamond"));
  EXPECT_TRUE(contains(at_marker("style relationship {\n  endArrow |\n}\n"), "ERzeroToMany"));
  EXPECT_TRUE(contains(edge_keys, "curved"));
  EXPECT_TRUE(contains(edge_keys, "double"));
  EXPECT_TRUE(contains(at_marker("style person {\n  dashed |\n}\n"), "true"));
  EXPECT_TRUE(contains(at_marker("model { a = process }\nview v {\n  |\n}\n"), "theme"));
  EXPECT_TRUE(contains(at_marker("model { a = process }\nview v {\n  theme |\n}\n"), "dark"));
}

// ── User-defined themes ──────────────────────────────────────────────────────────────────────────

namespace {

const char* const kBrand = R"(theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"
  fontSize 14
  node person {
    fill "#08427b"
    text "#ffffff"
  }
  node external {
    fill "#444444"
  }
}

theme print extends brand {
  background "#ffffff"
}

theme brand

model {
  user = person "User"
  ext = system "Ext" tag "external"
  user -> ext "calls"
}

view v {
  include *
  layout fixed
  pin user at 0, 0
  pin ext at 0, 150
}

view paper {
  include *
  layout fixed
  theme print
  pin user at 0, 0
  pin ext at 0, 150
}
)";

/// A library that ships a theme, as `lib build` would bundle it.
library::LibrarySet brand_library() {
  auto bundle = Json::parse(R"({"format":"diadraw-library","formatVersion":1,"name":"acme-brand","version":"1.0.0",
    "license":"MIT","themes":{
      "base":{"extends":"mono","edge":"#123456"},
      "corporate":{"extends":"base","background":"#fafafa","nodes":{"system":{"fill":"#aa0000","text":"#ffffff"}}}}})");
  return library::LibrarySet({{{"acme-brand", "^1", "acme"}, std::make_shared<const Json>(*bundle)}});
}

}  // namespace

TEST(Themes, DeclaresThemesAndPicksThem) {
  auto r = dsl::parse(kBrand);
  EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
  ASSERT_EQ(r.model.themes.size(), 2u);
  const ThemeDef& brand = r.model.themes[0];
  EXPECT_EQ(brand.name, "brand");
  EXPECT_EQ(brand.base, "dark");
  EXPECT_EQ(*brand.values.find("background"), "#101418");
  EXPECT_EQ(*brand.values.find("fontSize"), "14");
  ASSERT_EQ(brand.nodes.size(), 2u);
  EXPECT_EQ(brand.nodes[0].first, "person");
  EXPECT_EQ(*brand.nodes[0].second.find("fill"), "#08427b");
  EXPECT_EQ(r.model.theme, "brand");
  ASSERT_EQ(r.source_map.themes.size(), 2u);
  EXPECT_EQ(r.source_map.themes[0].nodes.size(), 2u);

  auto t = resolve_theme("print", r.model.themes);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->name, "print");
  EXPECT_EQ(t->background, "#ffffff");               // its own
  EXPECT_EQ(t->edge, "#9aa5b1");                     // from brand
  EXPECT_EQ(t->edge_text, "#cbd5e1");                // from dark
  EXPECT_EQ(t->nodes.at("person").fill, "#08427b");
  EXPECT_EQ(t->nodes.at("person").stroke, "#60a5fa");  // the rest of dark's person
  EXPECT_EQ(t->external.fill, "#444444");
  EXPECT_EQ(t->font_size, 14);
  EXPECT_FALSE(resolve_theme("nope", r.model.themes));
}

TEST(Themes, RenderDeclaredThemesAndMeasureInTheirFontSize) {
  auto r = dsl::parse(kBrand);
  std::string svg = render(r.model, "v");
  EXPECT_TRUE(has(svg, "fill=\"#101418\"/>"));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "user"), "fill=\"#08427b\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "ext"), "fill=\"#444444\""));
  EXPECT_TRUE(has(item_svg(svg, "dd-edge", "user-&gt;ext"), "stroke=\"#9aa5b1\""));
  std::string paper = render(r.model, "paper");
  EXPECT_TRUE(has(paper, "fill=\"#ffffff\"/>"));

  // The editor session lays labels out in the theme's font size.
  editor::Session session(kBrand);
  auto rendered = dd::test::ok(session.render("v"));
  EXPECT_TRUE(has(rendered.scene.content, "font-size=\"14\""));
}

TEST(Themes, PrintsAndRoundTripsDeclarations) {
  auto r = dsl::parse(kBrand);
  std::string printed = dsl::print(r.model);
  EXPECT_EQ(printed.rfind("theme brand extends dark {\n  background \"#101418\"\n  edge \"#9aa5b1\"\n  fontSize 14\n"
                          "  node person {\n    fill \"#08427b\"\n    text \"#ffffff\"\n  }\n",
                          0),
            0u)
      << printed;
  EXPECT_TRUE(has(printed, "}\n\ntheme brand\n\nmodel {"));
  auto again = dsl::parse(printed);
  EXPECT_TRUE(again.diagnostics.empty());
  EXPECT_TRUE(again.model == r.model);
  EXPECT_EQ(dsl::print(again.model), printed);
  auto back = dd::test::ok(model_from_json(to_json(r.model)));
  EXPECT_TRUE(back == r.model);
}

TEST(Themes, ReportsBadDeclarations) {
  auto r = dsl::parse(R"(theme dark { background "#000" }
theme a extends b { background "#111" }
theme b extends a { edgeColour "#222" node person { glow "#333" } }
theme b { }
theme c extends nowhere { }
theme x.y
)");
  auto m = messages(r);
  EXPECT_TRUE(contains(m, "\"dark\" is a built-in theme; give yours another name"));
  EXPECT_TRUE(contains(m, "Theme \"a\" extends itself"));
  EXPECT_TRUE(contains(m, "Theme \"b\" extends itself"));
  EXPECT_TRUE(contains(m, "Unknown theme key \"edgeColour\""));
  EXPECT_TRUE(contains(m, "Unknown theme node key \"glow\""));
  EXPECT_TRUE(contains(m, "Theme \"b\" is already declared"));
  EXPECT_TRUE(contains(m, "Unknown theme \"nowhere\" (themes: default, dark, mono, a, b, c)"));
  EXPECT_TRUE(contains(m, "Unknown theme \"x.y\" (themes: default, dark, mono, a, b, c)"));
  // A cycle never hangs the renderer: the view falls back to the default theme.
  ResolvedView v;
  v.theme = "a";
  v.themes = r.model.themes;
  EXPECT_EQ(theme_for(v).name, "default");
}

TEST(Themes, ComeFromLibraries) {
  auto lib = brand_library();
  EXPECT_EQ(lib.theme_refs(), (std::vector<std::string>{"acme.base", "acme.corporate"}));
  auto def = lib.theme("acme.corporate");
  ASSERT_TRUE(def);
  EXPECT_EQ(def->base, "acme.base");  // a sibling theme, qualified with the alias
  EXPECT_FALSE(lib.theme("acme.missing"));

  auto r = dsl::parse("use \"acme-brand@^1\" as acme\ntheme acme.corporate\nmodel { s = system \"S\" }\n");
  EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
  auto t = resolve_theme("acme.corporate", r.model.themes, lib.theme_resolver());
  ASSERT_TRUE(t);
  EXPECT_EQ(t->background, "#fafafa");
  EXPECT_EQ(t->edge, "#123456");                     // from acme.base
  EXPECT_EQ(t->edge_text, "#333333");                // from mono
  EXPECT_EQ(t->nodes.at("system").fill, "#aa0000");

  Model model = with_default_view(r.model);
  auto view = dd::test::ok(resolve_view(model, kDefaultViewId));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  RenderOptions options;
  options.themes = lib.theme_resolver();
  std::string svg = render_svg(view, geometry, options);
  EXPECT_TRUE(has(svg, "fill=\"#fafafa\"/>"));
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "s"), "fill=\"#aa0000\""));
  // Without the library the view falls back to the default theme.
  EXPECT_TRUE(has(render_svg(view, geometry), "fill=\"#ffffff\"/>"));

  // A document theme can extend a library theme.
  auto ext = dsl::parse("use \"acme-brand@^1\" as acme\ntheme mine extends acme.corporate { edge \"#00ff00\" }\n");
  EXPECT_TRUE(ext.diagnostics.empty());
  auto mine = resolve_theme("mine", ext.model.themes, lib.theme_resolver());
  ASSERT_TRUE(mine);
  EXPECT_EQ(mine->edge, "#00ff00");
  EXPECT_EQ(mine->background, "#fafafa");

  auto manifest = Json::parse(R"({"name":"acme-brand","version":"1.0.0","license":"MIT",
    "themes":{"ok":{"extends":"dark","background":"#000","nodes":{"person":{"fill":"#111"}}},
              "9bad":{},"odd":{"glow":"#fff","nodes":{"person":"red"}}}})");
  std::vector<std::string> issues;
  for (const auto& i : library::validate_manifest(*manifest)) issues.push_back((i.error ? "E " : "W ") + i.message);
  EXPECT_TRUE(contains(issues, "E Theme name \"9bad\" is not an identifier"));
  EXPECT_TRUE(contains(issues, "W Unknown theme key \"glow\""));
  EXPECT_TRUE(contains(issues, "E A theme node is an object of fill, stroke and text"));
}

TEST(Themes, CompletesDeclaredAndLibraryThemesAndThemeKeys) {
  lsp::LibraryContext libs{brand_library(), {}};
  auto at_marker = [&](std::string text) {
    auto at = text.find('|');
    text.erase(at, 1);
    auto r = dsl::parse(text);
    return labels(lsp::completions(text, at, r, &libs));
  };
  auto names = at_marker("theme brand { }\ntheme |");
  EXPECT_TRUE(contains(names, "brand"));
  EXPECT_TRUE(contains(names, "dark"));
  EXPECT_TRUE(contains(names, "acme.corporate"));
  EXPECT_TRUE(contains(at_marker("theme brand extends | { }\n"), "mono"));
  auto keys = at_marker("theme brand {\n  |\n}\n");
  EXPECT_TRUE(contains(keys, "background"));
  EXPECT_TRUE(contains(keys, "node"));
  auto kinds = at_marker("theme brand {\n  node |\n}\n");
  EXPECT_TRUE(contains(kinds, "external"));
  EXPECT_TRUE(contains(kinds, "person"));
  EXPECT_EQ(at_marker("theme brand {\n  node person {\n    |\n  }\n}\n"), (std::vector<std::string>{"fill", "stroke", "text"}));
}

// The actor's head is draw.io's umlActor proportion: a quarter of the height, at most half the width.
TEST(ShapeSvg, ActorHeadIsAQuarterOfTheHeight) {
  auto tall = shape_svg("actor", Box{0, 0, 80, 140}, "fill=\"#fff\"");
  EXPECT_NE(tall.find("r=\"17.5\""), std::string::npos) << tall;
  auto slim = shape_svg("actor", Box{0, 0, 10, 60}, "fill=\"#fff\"");
  EXPECT_NE(slim.find("r=\"2.5\""), std::string::npos) << slim;
}

// The head is an outline, whatever the element's fill, and stays a pointer target on the web canvas.
TEST(ShapeSvg, ActorHeadIsAnOutline) {
  auto svg = shape_svg("actor", Box{0, 0, 80, 140}, "fill=\"#123456\" stroke=\"#000\"");
  EXPECT_EQ(svg.find("#123456"), std::string::npos) << svg;
  EXPECT_NE(svg.find("<circle cx=\"40\" cy=\"17.5\" r=\"17.5\" fill=\"none\" stroke=\"#000\" pointer-events=\"all\"/>"),
            std::string::npos)
      << svg;
}

// A person is a stick figure that stays upright in a wide box: 0.6 as wide as tall, centred, with
// the element's fill on its head.
TEST(ShapeSvg, PersonIsAnUprightStickFigure) {
  auto svg = shape_svg("person", Box{0, 0, 120, 50}, "fill=\"#123456\" stroke=\"#000\"");
  EXPECT_NE(svg.find("<circle cx=\"60\" cy=\"6.25\" r=\"6.25\" fill=\"#123456\" stroke=\"#000\"/>"), std::string::npos)
      << svg;
  EXPECT_NE(svg.find("M45,17.5 H75"), std::string::npos) << svg;  // arms 30 wide, centred
  EXPECT_EQ(svg.find("<rect"), std::string::npos) << svg;
}

namespace {

/// The numbers after `key="` everywhere in `svg`, in order.
std::vector<double> attr_values(const std::string& svg, const std::string& key) {
  std::vector<double> out;
  const std::string needle = " " + key + "=\"";
  for (auto at = svg.find(needle); at != std::string::npos; at = svg.find(needle, at + 1))
    out.push_back(std::stod(svg.substr(at + needle.size())));
  return out;
}

/// The lowest point of a person's figure: the y of its feet, the last number of its path.
double feet_y(const std::string& node) {
  auto d_end = node.find("\" fill=\"none\"", node.find("<path d=\""));
  auto comma = node.rfind(',', d_end);
  return std::stod(node.substr(comma + 1, d_end - comma - 1));
}

constexpr const char* kPeople = R"(model {
  u = person "User" tech "Browser"
  x = person "Partner" tag "external"
  odd = person "Odd" shape foo
}
view v {
  include *
}
)";

}  // namespace

// The box holds the figure and, under it, the name and technology, all inside the box and in the
// theme's label colour, since they sit on the canvas rather than on a fill.
TEST(Styles, PersonLabelSitsUnderTheFigureInsideTheBox) {
  auto r = dsl::parse(kPeople);
  ASSERT_TRUE(r.diagnostics.empty());
  std::string svg = render(r.model, "v");
  for (const std::string id : {"u", "x", "odd"}) {
    std::string node = item_svg(svg, "dd-node", id);
    ASSERT_TRUE(has(node, "fill=\"none\" stroke=\"none\" pointer-events=\"all\"/>")) << node;  // the hit box
    double top = attr_values(node, "y").at(0);
    double bottom = top + attr_values(node, "height").at(0);
    auto text_ys = attr_values(node, "y");
    text_ys.erase(text_ys.begin());  // the hit box; the rest are the label's lines
    ASSERT_FALSE(text_ys.empty()) << node;
    EXPECT_GT(feet_y(node), top + 40) << node;                  // the figure keeps its height
    EXPECT_GT(text_ys.front() - 13 * 0.625, feet_y(node)) << node;  // the first line is under the feet
    EXPECT_LE(text_ys.back() + 13 * 0.625, bottom) << node;         // the last line is in the box
    EXPECT_TRUE(has(node, "fill=\"#1f2937\">")) << node;         // the default theme's labelText
    EXPECT_FALSE(has(node, "fill=\"#ffffff\">")) << node;
  }
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "u"), ">[Browser]<"));
}

// A style's fontColor picks the stick figure's label colour; a theme's `node person { text }` is
// for a person drawn as a filled box, so the default theme keeps white text on its navy fill.
TEST(Styles, PersonLabelColours) {
  auto r = dsl::parse("theme brand {\n  node person { text \"#00aa00\" }\n}\n"
                      "style tag \"red\" {\n  fontColor \"#ff0000\"\n}\n"
                      "model {\n  u = person \"User\"\n  w = person \"Red\" tag \"red\"\n"
                      "  b = person \"Boxed\" shape rect\n}\n"
                      "view v {\n  include *\n}\nview branded {\n  include *\n  theme brand\n}\n");
  ASSERT_TRUE(r.diagnostics.empty());
  std::string svg = render(r.model, "v");
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "u"), "fill=\"#1f2937\">User<")) << svg;
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "w"), "fill=\"#ff0000\">Red<")) << svg;
  EXPECT_TRUE(has(item_svg(svg, "dd-node", "b"), "fill=\"#ffffff\">Boxed<")) << svg;
  std::string branded = render(r.model, "branded");
  EXPECT_TRUE(has(item_svg(branded, "dd-node", "u"), "fill=\"#1f2937\">User<")) << branded;
  EXPECT_TRUE(has(item_svg(branded, "dd-node", "b"), "fill=\"#00aa00\">Boxed<")) << branded;
}

// A box too short for both keeps half of it for the figure, and the label runs past the bottom.
TEST(Styles, PersonInAShortBoxKeepsHalfForTheFigure) {
  auto r = dsl::parse("model {\n  u = person \"User\"\n}\nview v {\n  include *\n  layout fixed\n"
                      "  pin u at 0, 0 size 80, 40\n}\n");
  ASSERT_TRUE(r.diagnostics.empty());
  std::string node = item_svg(render(r.model, "v"), "dd-node", "u");
  EXPECT_NEAR(feet_y(node) - attr_values(node, "y").at(0), 20, 0.01) << node;
}

// A person whose style puts the label outside keeps the figure across the whole box.
TEST(Styles, PersonWithAnOutsideLabelFillsItsBoxWithTheFigure) {
  auto r = dsl::parse("model {\n  u = person \"User\" style \"labelPosition=right\"\n}\nview v {\n  include *\n}\n");
  ASSERT_TRUE(r.diagnostics.empty());
  std::string node = item_svg(render(r.model, "v"), "dd-node", "u");
  EXPECT_FALSE(has(node, "pointer-events=\"all\"/>")) << node;  // not the person-with-label path
  EXPECT_TRUE(has(node, ">User<")) << node;
}

// A multi-line name sizes the box by its widest line, and render keeps each line whole.
TEST(Styles, PersonBoxFitsTheWidestLineOfItsName) {
  auto r = dsl::parse("model {\n  a = person \"A\"\n  b = person \"A\\nVery long second line\"\n}\n"
                      "view v {\n  include *\n}\n");
  ASSERT_TRUE(r.diagnostics.empty());
  const Element* a = r.model.elements.find("a");
  const Element* b = r.model.elements.find("b");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  Measure m = create_measure();
  EXPECT_GT(m.node(*b).width, m.node(*a).width);
  EXPECT_LT(m.node(*b).width, std::ceil(text_width(b->name, 13) + 16));  // not the whole name
  std::string node = item_svg(render(r.model, "v"), "dd-node", "b");
  EXPECT_TRUE(has(node, ">Very long second line<")) << node;
}

// ── Descriptions ──

TEST(Descriptions, AreDrawnUnderTheNameAndTechInSmallType) {
  auto parsed = dsl::parse(R"(model {
  web = container "Web" tech "React" description """
    Lists products.

    Takes orders.
  """
  plain = container "Plain"
}
view v { include * }
)");
  ASSERT_TRUE(parsed.diagnostics.empty());
  std::string svg = render(parsed.model, "v");
  std::string web = item_svg(svg, "dd-node", "web");
  ASSERT_FALSE(web.empty());
  auto at = [&](const std::string& s) { return web.find(">" + s + "</text>"); };
  ASSERT_NE(at("Lists products."), std::string::npos) << web;
  ASSERT_NE(at("Takes orders."), std::string::npos);
  EXPECT_LT(at("[React]"), at("Lists products."));
  EXPECT_LT(at("Lists products."), at("Takes orders."));
  EXPECT_TRUE(has(web, "font-size=\"11\" fill=\"#ffffff\">Lists products.</text>")) << web;
  EXPECT_FALSE(has(item_svg(svg, "dd-node", "plain"), "font-size=\"11\""));
}

TEST(Descriptions, MakeTheBoxTallerAndWrapAtAWidthLimit) {
  Measure m = create_measure();
  Element e;
  e.id = e.name = "web";
  e.kind = "container";
  Size bare = m.node(e);
  e.description = "One line";
  Size one = m.node(e);
  EXPECT_EQ(one.width, bare.width);
  EXPECT_GT(one.height, bare.height);
  e.description = "One line\nTwo lines";
  EXPECT_NEAR(m.node(e).height - one.height, 11 * 1.25, 1);
  e.description = "A long description of what this container does, which keeps going well past the widest box";
  Size wide = m.node(e);
  EXPECT_EQ(wide.width, 240);
  EXPECT_GT(wide.height, one.height);  // it wrapped
  e.kind = "person";
  e.description = "Buys things";
  Element bare_person = e;
  bare_person.description.reset();
  EXPECT_GT(m.node(e).height, m.node(bare_person).height);
}

TEST(Descriptions, ABlankOneOrTrailingLineBreaksTakeNoRoom) {
  Measure m = create_measure();
  Element e;
  e.id = e.name = "web";
  e.kind = "container";
  Size bare = m.node(e);
  e.description = "   \n ";
  EXPECT_EQ(m.node(e).height, bare.height);
  e.description = "One line";
  Size one = m.node(e);
  e.description = "One line\n\n";
  EXPECT_EQ(m.node(e).height, one.height);
}

TEST(Descriptions, DrawingsShowThemUnderneathUnlessStyled) {
  ShapeArt art;
  art.body = "<rect width=\"48\" height=\"48\"/>";
  Measure m = create_measure(default_theme(), [art](const std::string& s) -> std::optional<ShapeArt> {
    return s == "lib.box" ? std::optional(art) : std::nullopt;
  });
  Element e;
  e.id = e.name = "a";
  e.kind = "container";
  e.shape = "lib.box";
  Size bare = m.node(e);
  e.description = "Kept for two years";
  EXPECT_GT(m.node(e).height, bare.height);
  e.style = dsl::parse_style_text("aspect=fixed");
  Element styled_bare = e;
  styled_bare.description.reset();
  EXPECT_EQ(m.node(e).height, m.node(styled_bare).height);
}

TEST(Descriptions, CirclesShowTheNameOnly) {
  auto parsed = dsl::parse("model {\n  c = process \"Hub\" shape circle description \"Routes every request\"\n}\nview v { include * }\n");
  ASSERT_TRUE(parsed.diagnostics.empty());
  Element bare = *parsed.model.elements.find("c");
  bare.description.reset();
  EXPECT_EQ(create_measure().node(*parsed.model.elements.find("c")).width, create_measure().node(bare).width);
  EXPECT_FALSE(has(render(parsed.model, "v"), "Routes every request"));
}

TEST(Descriptions, OutsideLabelsAreSizedAsBefore) {
  Measure m = create_measure();
  for (const char* style : {"", "verticalLabelPosition=bottom"}) {
    Element e;
    e.id = e.name = "a";
    e.kind = "person";
    if (*style) {
      e.kind = "container";
      e.style = dsl::parse_style_text(style);
    } else {
      e.shape = "actor";
    }
    Size bare = m.node(e);
    e.description = "Buys things in the shop every day";
    Size described = m.node(e);
    EXPECT_EQ(described.width, bare.width) << style;
    EXPECT_EQ(described.height, bare.height) << style;
  }
}

TEST(Descriptions, StayInsideTheBoxTheyAreMeasuredFor) {
  auto parsed = dsl::parse(R"(model {
  r = container "Box" tech "Go" description "Takes orders from the shop and passes them on to fulfilment"
  e = process "Oval" shape ellipse description "An ellipse with a long description that should wrap at three quarters"
  h = process "Rhombus" shape rhombus description "A rhombus with a description"
  n = decision "Is the customer eligible for an expedited refund?" tech "Rules" description "x0\nx1\nx2\nx3"
  m = process "Many" shape ellipse tech "Go" description "a\nb\nc\nd\ne\nf\ng\nh"
  q = person "One\nTwo\nThree"
  d = decision "Approve refund?" description """
    Over 100 needs a manager.
    Under 100 is automatic.
    Fraud flags always stop.
  """
  p = person "Shopper" tech "Browser" description """
    Buys things.

    Pays by card.
  """
  c = container "DB" shape cylinder description "Orders and their lines, kept for two years"
}
view v { include * }
)");
  ASSERT_TRUE(parsed.diagnostics.empty());
  auto view = dd::test::ok(resolve_view(parsed.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  std::string svg = render_svg(view, geometry);
  const std::map<std::string, std::string> outline{{"e", "ellipse"}, {"m", "ellipse"}, {"h", "rhombus"},
                                                    {"d", "rhombus"}, {"n", "rhombus"}};
  for (const char* id : {"r", "e", "h", "d", "p", "c", "n", "m", "q"}) {
    const Box& box = *geometry.nodes.find(id);
    std::string node = item_svg(svg, "dd-node", id);
    ASSERT_FALSE(node.empty()) << id;
    int texts = 0;
    for (std::size_t at = node.find("<text "); at != std::string::npos; at = node.find("<text ", at + 1)) {
      std::size_t y_at = node.find(" y=\"", at);
      std::size_t size_at = node.find("font-size=\"", at);
      ASSERT_NE(y_at, std::string::npos);
      ASSERT_NE(size_at, std::string::npos);
      y_at += 4;
      double y = std::stod(node.substr(y_at, node.find('"', y_at) - y_at));
      double size = std::stod(node.substr(size_at + 11));
      EXPECT_GE(y - size / 2, box.y) << id;
      EXPECT_LE(y + size / 2, box.y + box.height) << id << " " << node;
      // In a diamond or an ellipse, the line's ends (as wide as the text estimate) stay inside it.
      std::size_t content = node.find('>', at) + 1;
      std::string line = node.substr(content, node.find("</text>", content) - content);
      double half_w = text_width(line, size) / 2;
      double dx = half_w / (box.width / 2);
      double dy = std::max(std::abs(y - size / 2 - (box.y + box.height / 2)), std::abs(y + size / 2 - (box.y + box.height / 2))) /
                  (box.height / 2);
      auto it = outline.find(id);
      std::string shape = it == outline.end() ? "" : it->second;
      if (shape == "rhombus") EXPECT_LE(dx + dy, 1.0) << id << " " << line;
      if (shape == "ellipse") EXPECT_LE(dx * dx + dy * dy, 1.0) << id << " " << line;
      ++texts;
    }
    EXPECT_GE(texts, 2) << id;
  }
}
