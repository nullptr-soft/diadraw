// Import infrastructure: identifiers, HTML text, codecs and the XML reader. The importers
// themselves were checked against the TypeScript importers over the corpus (see src/README.md).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "diadraw/diadraw.h"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/io/bpmn.hpp"
#include "diadraw/io/codec.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/io/plantuml.hpp"
#include "diadraw/io/structurizr.hpp"
#include "diadraw/io/xml.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/model/views.hpp"
#include "diadraw/render/display_list.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::io;

TEST(Identifiers, FollowTheDslRules) {
  EXPECT_EQ(to_identifier("Order service"), "Order_service");
  EXPECT_EQ(to_identifier("Café Ünïcödé"), "Cafe_Unicode");
  EXPECT_EQ(to_identifier("42 things"), "n42_things");
  EXPECT_EQ(to_identifier("layout"), "layout_");
  EXPECT_EQ(to_identifier("***"), "node");
  EXPECT_EQ(to_identifier("", "main"), "main");
  EXPECT_EQ(to_identifier("ﬁle"), "file");  // a compatibility ligature
}

TEST(Labels, PlainTextAndHtml) {
  EXPECT_EQ(plain_text("<b>Bold</b><br/>and &amp; &#x41;&#66;  more "), "Bold and & AB more");
  EXPECT_EQ(html_to_text("<div>One</div><div>Two&nbsp;&nbsp;three</div><br><br><br><br>End"), "One\nTwo three\n\nEnd");
}

TEST(Codec, Base64LikeAtob) {
  EXPECT_EQ(base64_decode("aGVs bG8=\n"), std::optional<std::string>("hello"));
  EXPECT_EQ(base64_decode("aGVsbG8"), std::optional<std::string>("hello"));
  EXPECT_FALSE(base64_decode("a").has_value());
  EXPECT_FALSE(base64_decode("a$==").has_value());
}

TEST(Codec, InflateRawAndZlib) {
  // "hello" compressed with zlib (78 9c ...) and as raw deflate (the same without the wrapper).
  const std::string zlib("\x78\x9c\xcb\x48\xcd\xc9\xc9\x07\x00\x06\x2c\x02\x15", 13);
  EXPECT_EQ(inflate_zlib(zlib), std::optional<std::string>("hello"));
  EXPECT_EQ(inflate_raw(zlib.substr(2, zlib.size() - 6)), std::optional<std::string>("hello"));
  EXPECT_FALSE(inflate_raw("not deflate").has_value());
  EXPECT_EQ(gunzip("plain"), std::optional<std::string>("plain"));
}

TEST(Codec, DecodeUriComponent) {
  EXPECT_EQ(decode_uri_component("%3Cmx%20a%3D%22%C3%A9%22%3E"), std::optional<std::string>("<mx a=\"\xC3\xA9\">"));
  EXPECT_FALSE(decode_uri_component("%E0%A4%A").has_value());
  EXPECT_FALSE(decode_uri_component("%C0%80").has_value());  // overlong
  EXPECT_EQ(safe_decode_uri("100%"), "100%");
}

TEST(Codec, PercentEncode) {
  EXPECT_EQ(percent_encode("macOS 15.1"), "macOS%2015.1");
  EXPECT_EQ(percent_encode("Linux (GTK)"), "Linux%20%28GTK%29");
  EXPECT_EQ(percent_encode("a&b=c?d#e+f/g"), "a%26b%3Dc%3Fd%23e%2Bf%2Fg");
  EXPECT_EQ(percent_encode("-._~AZaz09"), "-._~AZaz09");
  EXPECT_EQ(percent_encode("\xC3\x9C"), "%C3%9C");
  EXPECT_EQ(percent_encode("\xFF"), "%FF");
  EXPECT_EQ(percent_encode(std::string_view("a\0b", 3)), "a%00b");
  EXPECT_EQ(percent_encode(""), "");
  for (std::string_view s : {"plain", "100% & more", "Ünïcode ✓ (x)", "a\nb\tc"})
    EXPECT_EQ(decode_uri_component(percent_encode(s)), std::optional<std::string>(s));
}

TEST(Xml, ReadsElementsAttributesAndTextForgivingly) {
  auto doc = parse_xml(
      "<?xml version='1.0'?><!-- c --><root a=\"1 &amp; 2\" b='x' a=\"3\">"
      "text &lt;here&gt;<child/><![CDATA[<raw>]]>< stray<other x=\"y\">in</other></root>");
  ASSERT_EQ(doc->children.size(), 1u);
  const XmlNode& root = *doc->children[0];
  EXPECT_EQ(root.name, "root");
  EXPECT_EQ(*root.attr("a"), "3");  // a repeated attribute takes the last value…
  EXPECT_EQ(root.attrs.front().first, "a");  // …in its first place
  EXPECT_EQ(*root.attr("b"), "x");
  EXPECT_EQ(root.text, "text <here><raw> stray");
  ASSERT_EQ(root.children.size(), 2u);
  EXPECT_EQ(root.children[1]->text, "in");
}

TEST(Drawio, ImportsAPlainFileWithGeometry) {
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="a" value="Start" style="ellipse;fillColor=#dae8fc;" vertex="1" parent="1">
      <mxGeometry x="10" y="20" width="80" height="40" as="geometry"/></mxCell>
    <mxCell id="b" value="&lt;b&gt;Check&lt;/b&gt;" style="rhombus;html=1;" vertex="1" parent="1">
      <mxGeometry x="200" y="10" width="80" height="80" as="geometry"/></mxCell>
    <mxCell id="e" value="go" style="endArrow=open;dashed=1;" edge="1" parent="1" source="a" target="b">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="150" y="40"/></Array></mxGeometry></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  EXPECT_TRUE(r.report.dropped.empty());
  ASSERT_EQ(r.model.elements.size(), 2u);
  const Element& b = *r.model.elements.find("Check");
  EXPECT_EQ(b.kind, "decision");
  EXPECT_EQ(b.shape, std::optional<std::string>("rhombus"));
  ASSERT_EQ(r.model.relationships.size(), 1u);
  const Relationship& e = *r.model.relationships.begin();
  EXPECT_EQ(e.label, std::optional<std::string>("go"));
  EXPECT_EQ(e.line, LineStyle::Dashed);
  const View& v = *r.model.views.find("Main");
  EXPECT_EQ(v.layout.algorithm, "fixed");
  ASSERT_EQ(v.hints.size(), 3u);
  EXPECT_EQ(std::get<PinAtHint>(v.hints[0]).x, 10);
  EXPECT_EQ(std::get<RouteHint>(v.hints[2]).points, (std::vector<Point2>{{150, 40}}));
}

TEST(Drawio, DecodesNamedEntitiesInLabels) {
  EXPECT_EQ(io::decode_entities("&laquo;a&raquo; &rarr; &rArr; &copy; &mdash; &bogus;"),
            "«a» → ⇒ © — &bogus;");
  EXPECT_EQ(io::html_to_text("&laquo;Annotation&raquo;<br/><b>Component</b>"), "«Annotation»\nComponent");
}

TEST(Drawio, ReadsTheFontStyleOfEachLineOfAnHtmlLabel) {
  EXPECT_EQ(io::html_line_font_styles("&laquo;A&raquo;<br/><b>B</b>", 0), (std::vector<int>{0, 1}));
  // A line is bold only when all of its text is; the cell's fontStyle runs under every line.
  EXPECT_EQ(io::html_line_font_styles("<b>A</b> b<br><i>c</i>", 4), (std::vector<int>{4, 6}));
  // Blank lines collapse as html_to_text collapses them.
  EXPECT_EQ(io::html_line_font_styles("<br><u>x</u><br><br><br>y<br>", 0), (std::vector<int>{4, 0, 0}));
  EXPECT_EQ(io::html_line_font_styles("<strong>a <em>b</em></strong>", 0), (std::vector<int>{1}));
  // A stray </b> closes nothing; a '<' without a '>' is text.
  EXPECT_EQ(io::html_line_font_styles("</b>a<br><b>b", 0), (std::vector<int>{0, 1}));
  EXPECT_EQ(io::html_line_font_styles("a<br><b>c</b> <d <e", 0), (std::vector<int>{0, 0}));
  // html_to_text keeps "<br class>" on one line; this reading would not, so it gives up.
  EXPECT_TRUE(io::html_line_font_styles("a<br class=\"x\"><b>b</b>", 0).empty());
}

TEST(Drawio, ReadsAMalformedFontStyleAsDrawioDoes) {
  for (const char* font_style : {"abc", "1e300", "-1e300", "NaN"}) {
    std::string xml = std::string(R"(<mxfile><diagram name="Main"><mxGraphModel><root>
      <mxCell id="0"/><mxCell id="1" parent="0"/>
      <mxCell id="a" vertex="1" parent="1" value="x&lt;br&gt;&lt;b&gt;y&lt;/b&gt;" style="html=1;fontStyle=)") +
                      font_style + R"(;"><mxGeometry x="0" y="0" width="80" height="40" as="geometry"/></mxCell>
    </root></mxGraphModel></diagram></mxfile>)";
    ImportResult r = import_drawio(xml);
    const Element* a = r.model.elements.begin() == r.model.elements.end() ? nullptr : &*r.model.elements.begin();
    ASSERT_NE(a, nullptr) << font_style;
    ASSERT_TRUE(a->style && a->style->find("lineFontStyles")) << font_style;
    EXPECT_EQ(*a->style->find("lineFontStyles"), "0,1") << font_style;
  }
}

TEST(Drawio, ReadsStrikeThroughAsFontStyleEight) {
  EXPECT_EQ(io::html_line_font_styles("<strike>a</strike><br><s>b</s><br><del>c</del> <b>d</b>", 0),
            (std::vector<int>{8, 8, 0}));
  EXPECT_EQ(io::html_line_font_styles("<b><strike>a</strike></b>", 2), (std::vector<int>{11}));
}

TEST(Drawio, ReadsTheFontStyleOfEachRunOfAnHtmlLabel) {
  using io::FontRun;
  // fig6: one line with a struck-through, a bold, an italic and an underlined word.
  EXPECT_EQ(io::html_font_runs("Text <strike>Text</strike> Text Text <b>Text</b> Text Text <i>Text</i> Text Text "
                               "Text <u>Text</u> Text&nbsp;",
                               0),
            (std::vector<FontRun>{{5, 0}, {4, 8}, {11, 0}, {4, 1}, {11, 0}, {4, 2}, {16, 0}, {4, 4}, {5, 0}}));
  // A run of blanks takes the style of its first blank; a line break that of the run before it.
  EXPECT_EQ(io::html_font_runs("<u>a&nbsp; </u> b<br><b>c</b>", 0),
            (std::vector<FontRun>{{2, 4}, {2, 0}, {1, 1}}));
  // Code points, not bytes: "é" is one.
  EXPECT_EQ(io::html_font_runs("<b>é</b>x", 0), (std::vector<FontRun>{{1, 1}, {1, 0}}));
  EXPECT_TRUE(io::html_font_runs("a<br class=\"x\"><b>b</b>", 0).empty());
}

TEST(Drawio, ImportsTextStylesWithinALine) {
  // fig6.drawio: a label with styled words, and three blank labels in styled <div>s.
  const char* xml = R"(<mxfile><diagram name="Page-1"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="b1" parent="1" vertex="1" style="text;html=1;whiteSpace=wrap;strokeColor=none;fillColor=none;align=center;verticalAlign=middle;rounded=0;fontColor=#000000;" value="&lt;div style=&quot;background-color: rgb(18, 19, 20); font-family: Menlo, Monaco, &amp;quot;Courier New&amp;quot;, monospace; line-height: 18px; white-space: pre;&quot;&gt;&lt;div&gt;&lt;br&gt;&lt;/div&gt;&lt;/div&gt;"><mxGeometry height="30" width="60" x="240" y="160" as="geometry"/></mxCell>
    <mxCell id="b2" parent="1" vertex="1" style="text;html=1;whiteSpace=wrap;strokeColor=none;fillColor=none;align=center;verticalAlign=middle;rounded=0;" value="&lt;div style=&quot;color: rgb(187, 190, 191); white-space: pre;&quot;&gt;&lt;br&gt;&lt;/div&gt;"><mxGeometry height="60" width="550" x="-17" y="220" as="geometry"/></mxCell>
    <mxCell id="t" parent="1" vertex="1" style="text;html=1;whiteSpace=wrap;strokeColor=none;fillColor=none;align=center;verticalAlign=middle;rounded=0;" value="Text &lt;strike&gt;Text&lt;/strike&gt; Text Text &lt;b&gt;Text&lt;/b&gt; Text Text &lt;i&gt;Text&lt;/i&gt; Text Text Text &lt;u&gt;Text&lt;/u&gt; Text&amp;nbsp;"><mxGeometry height="75" width="120" x="140" y="223" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const Element* t = nullptr;
  for (const auto& e : r.model.elements)
    if (e.name.rfind("Text", 0) == 0) t = &e;
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->name, "Text Text Text Text Text Text Text Text Text Text Text Text Text");
  ASSERT_TRUE(t->style && t->style->find("fontRuns"));
  EXPECT_EQ(*t->style->find("fontRuns"), "5:0,4:8,11:0,4:1,11:0,4:2,16:0,4:4,5:0");
  EXPECT_FALSE(t->style->find("lineFontStyles"));

  auto view = dd::test::ok(resolve_view(r.model, "Page_1"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  const std::string node = dd::test::item_svg(svg, "dd-node", t->id);
  EXPECT_NE(node.find("<tspan text-decoration=\"line-through\">Text</tspan>"), std::string::npos) << node;
  EXPECT_NE(node.find("<tspan font-weight=\"600\">Text</tspan>"), std::string::npos) << node;
  EXPECT_NE(node.find("<tspan font-style=\"italic\">Text</tspan>"), std::string::npos) << node;
  EXPECT_NE(node.find("<tspan text-decoration=\"underline\">Text</tspan>"), std::string::npos) << node;
  // The blank labels stay blank: no box behind them, no text.
  for (const auto& e : r.model.elements) {
    if (&e == t) continue;
    EXPECT_EQ(e.name, "");
    const std::string blank = dd::test::item_svg(svg, "dd-node", e.id);
    EXPECT_EQ(blank.find("<text"), std::string::npos) << blank;
    EXPECT_EQ(blank.find("fill=\"#121314\""), std::string::npos) << blank;
  }

  // The native canvases and the PDF get each run in its own style.
  auto list = draw::display_list_from_svg(svg);
  ASSERT_TRUE(list);
  std::vector<draw::TextSpan> spans;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Label && item.owner == t->id)
      for (const auto& sp : item.text.spans) spans.push_back(sp);
  auto styled = [&](auto has) {
    return std::count_if(spans.begin(), spans.end(), [&](const draw::TextSpan& sp) { return has(sp); });
  };
  EXPECT_EQ(styled([](const draw::TextSpan& sp) { return sp.strike; }), 1);
  EXPECT_EQ(styled([](const draw::TextSpan& sp) { return sp.bold; }), 1);
  EXPECT_EQ(styled([](const draw::TextSpan& sp) { return sp.italic; }), 1);
  EXPECT_EQ(styled([](const draw::TextSpan& sp) { return sp.underline; }), 1);
}

TEST(Drawio, DrawsFontRunsOnlyWhenTheyFitTheName) {
  auto r = dsl::parse(R"(model {
  a = process "ab cd\nef" style "fontRuns=1:1,2:0,2:12,1:0,2:2"
  b = process "edited" style "fontRuns=1:1,2:0"
  c = process "bad" style "fontRuns=x:1,2:0"
  d = process "big" style "fontRuns=1e300:1"
}
view v {
  include *
  layout fixed
  pin a at 0, 0 size 200, 60
  pin b at 0, 100 size 200, 60
  pin c at 0, 200 size 200, 60
  pin d at 0, 300 size 200, 60
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  auto node = [&](const std::string& id) { return dd::test::item_svg(svg, "dd-node", id); };
  EXPECT_NE(node("a").find("<tspan font-weight=\"600\">a</tspan><tspan>b </tspan><tspan "
                           "text-decoration=\"underline line-through\">cd</tspan></text>"),
            std::string::npos)
      << node("a");
  // The second line is all one style: no runs.
  EXPECT_NE(node("a").find("font-style=\"italic\" fill=\"#000000\">ef</text>"), std::string::npos) << node("a");
  for (const char* id : {"b", "c", "d"}) EXPECT_EQ(node(id).find("<tspan"), std::string::npos) << node(id);

  auto list = draw::display_list_from_svg(svg);
  ASSERT_TRUE(list);
  const draw::Text* first = nullptr;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Label && item.owner == "a" && !first) first = &item.text;
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->content, "ab cd");
  ASSERT_EQ(first->spans.size(), 3U);
  EXPECT_TRUE(first->spans[0].bold);
  EXPECT_EQ(first->spans[1].content, "b ");
  EXPECT_TRUE(first->spans[2].underline && first->spans[2].strike);
}

TEST(Drawio, KeepsAngleBracketsInLabelsThatAreNotHtml) {
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="a" vertex="1" parent="1" value="+ pair: Pair&lt;S, T&gt;" style="text;align=left;">
      <mxGeometry x="0" y="0" width="160" height="20" as="geometry"/></mxCell>
    <mxCell id="b" vertex="1" parent="1" value="+ f: Function&lt;U, T&gt;" style="text;align=left;">
      <mxGeometry x="0" y="40" width="160" height="20" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  std::vector<std::string> names;
  for (const auto& e : r.model.elements) names.push_back(e.name);
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<std::string>{"+ f: Function<U, T>", "+ pair: Pair<S, T>"}));
}

TEST(Drawio, DrawsAStruckThroughLabel) {
  auto r = dsl::parse(R"(model {
  a = process "gone" style "fontStyle=12"
}
view v {
  include *
  layout fixed
  pin a at 0, 0 size 120, 60
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  const std::string node = dd::test::item_svg(svg, "dd-node", "a");
  EXPECT_NE(node.find("text-decoration=\"underline line-through\""), std::string::npos) << node;
  auto list = draw::display_list_from_svg(svg);
  ASSERT_TRUE(list);
  bool struck = false;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Label && item.owner == "a")
      struck = item.text.strike && item.text.underline && item.text.spans.empty();
  EXPECT_TRUE(struck);
}

TEST(Drawio, TrimsTheRunsOfATextAsItsText) {
  // Blanks at either end go, from the runs too; a run cut by the trim keeps its style.
  auto list = draw::display_list_from_svg(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="40"><text x="0" y="20" fill="#000">)"
      R"(<tspan> </tspan><tspan font-weight="600"> a</tspan><tspan>b</tspan>)"
      R"(<tspan text-decoration="line-through">c </tspan><tspan>  </tspan></text></svg>)");
  ASSERT_TRUE(list);
  const draw::Text* text = nullptr;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Label) text = &item.text;
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->content, "abc");
  ASSERT_EQ(text->spans.size(), 3U);
  EXPECT_EQ(text->spans[0].content, "a");
  EXPECT_TRUE(text->spans[0].bold);
  EXPECT_EQ(text->spans[1].content, "b");
  EXPECT_EQ(text->spans[2].content, "c");
  EXPECT_TRUE(text->spans[2].strike);
  // Runs all in one style are the text's own style.
  auto plain = draw::display_list_from_svg(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="40"><text x="0" y="20" fill="#000">)"
      R"(<tspan font-style="italic">a </tspan><tspan font-style="italic">b</tspan></text></svg>)");
  ASSERT_TRUE(plain);
  const draw::Text* one = nullptr;
  for (const auto& item : plain->items)
    if (item.kind == draw::Item::Label) one = &item.text;
  ASSERT_NE(one, nullptr);
  EXPECT_TRUE(one->italic);
  EXPECT_TRUE(one->spans.empty());
  EXPECT_EQ(one->content, "a b");
}

TEST(Drawio, KeepsTheHeaderLabelOfTablesAndLifelinesThatHoldOthers) {
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="t" value="Customer" vertex="1" parent="1" style="shape=table;startSize=30;container=1;html=1;">
      <mxGeometry x="0" y="40" width="180" height="120" as="geometry"/></mxCell>
    <mxCell id="r" value="" vertex="1" parent="t" style="shape=tableRow;html=1;">
      <mxGeometry y="30" width="180" height="30" as="geometry"/></mxCell>
    <mxCell id="l" value="obj:Lifeline" vertex="1" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;html=1;container=1;">
      <mxGeometry x="300" y="40" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="b" value="" vertex="1" parent="l" style="html=1;points=[];">
      <mxGeometry x="45" y="80" width="10" height="80" as="geometry"/></mxCell>
    <mxCell id="g" value="Group" vertex="1" parent="1" style="rounded=0;html=1;">
      <mxGeometry x="500" y="40" width="200" height="200" as="geometry"/></mxCell>
    <mxCell id="c" value="child" vertex="1" parent="g" style="html=1;">
      <mxGeometry x="20" y="20" width="60" height="40" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  auto valign = [&](const std::string& id) {
    const Element* e = r.model.elements.find(id);
    const std::string* v = e && e->style ? e->style->find("verticalAlign") : nullptr;
    return v ? *v : std::string("<none>");
  };
  EXPECT_EQ(valign("Customer"), "<none>");
  EXPECT_EQ(valign("obj_Lifeline"), "<none>");
  EXPECT_EQ(valign("Group"), "middle");
}

namespace {

// draw.io's fig3: UML shapes with their draw.io looks.
const char* kUmlShapes = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
  <mxCell id="0"/><mxCell id="1" parent="0"/>
  <mxCell id="block" parent="1" value="Block" vertex="1"
    style="verticalAlign=top;align=left;spacingTop=8;spacingLeft=2;spacingRight=12;shape=cube;size=10;direction=south;fontStyle=4;html=1;whiteSpace=wrap;">
    <mxGeometry height="120" width="180" x="50" y="70" as="geometry"/></mxCell>
  <mxCell id="comp" parent="1" value="&amp;laquo;Annotation&amp;raquo;&lt;br/&gt;&lt;b&gt;Component&lt;/b&gt;" vertex="1"
    style="html=1;dropTarget=0;whiteSpace=wrap;">
    <mxGeometry height="90" width="180" x="280" y="70" as="geometry"/></mxCell>
  <mxCell id="icon" parent="comp" style="shape=module;jettyWidth=8;jettyHeight=4;" value="" vertex="1">
    <mxGeometry height="20" relative="1" width="20" x="1" as="geometry"><mxPoint x="-27" y="7" as="offset"/></mxGeometry>
  </mxCell>
  <mxCell id="mod" parent="1" value="Module" vertex="1"
    style="shape=module;align=left;spacingLeft=20;align=center;verticalAlign=top;whiteSpace=wrap;html=1;">
    <mxGeometry height="50" width="100" x="280" y="190" as="geometry"/></mxCell>
  <mxCell id="pkg" parent="1" value="package" vertex="1"
    style="shape=folder;fontStyle=1;spacingTop=10;tabWidth=40;tabHeight=14;tabPosition=left;html=1;whiteSpace=wrap;">
    <mxGeometry height="80" width="130" x="540" y="70" as="geometry"/></mxCell>
  <mxCell id="note" parent="1" value="Comment1 body" vertex="1"
    style="shape=note2;boundedLbl=1;whiteSpace=wrap;html=1;size=25;verticalAlign=top;align=center;fillColor=#d5e8d4;strokeColor=#82b366;">
    <mxGeometry height="90" width="180" x="70" y="290" as="geometry"/></mxCell>
</root></mxGraphModel></diagram></mxfile>)";

/// The `y` of the text whose content is `content` in `svg`, or NaN.
double text_y(const std::string& svg, const std::string& content) {
  auto end = svg.find(">" + content + "</text>");
  if (end == std::string::npos) return std::nan("");
  auto start = svg.rfind("<text ", end);
  auto y = svg.find(" y=\"", start);
  return std::stod(svg.substr(y + 4));
}

}  // namespace

TEST(Drawio, ImportsUmlShapesWithTheirDrawioLooks) {
  ImportResult r = import_drawio(kUmlShapes);
  EXPECT_TRUE(r.report.dropped.empty());
  ASSERT_TRUE(r.report.unknown_styles);
  for (const auto& [key, count] : *r.report.unknown_styles) ADD_FAILURE() << "unknown style key " << key;
  auto style = [&](const std::string& id, const std::string& key) -> std::string {
    const Element* e = r.model.elements.find(id);
    if (!e || !e->style) return "<none>";
    const std::string* v = e->style->find(key);
    return v ? *v : "<none>";
  };
  // The HTML label: entities decoded, the line break kept, the bold second line noted, and the
  // label centred although the box holds the icon.
  const Element& comp = *r.model.elements.find("Annotation");
  EXPECT_EQ(comp.name, "«Annotation»\nComponent");
  EXPECT_EQ(style("Annotation", "lineFontStyles"), "0,1");
  EXPECT_EQ(style("Annotation", "verticalAlign"), "middle");
  // The icon is a module, at its spot on the box's top right corner.
  const Element& icon = *r.model.elements.find("Annotation.module");
  EXPECT_EQ(icon.shape, std::optional<std::string>("module"));
  EXPECT_EQ(style("Annotation.module", "jettyWidth"), "8");
  // It is an adornment: anchored on the box's top right corner, with a size but no pin of its own.
  EXPECT_EQ(style("Annotation.module", "anchor"), "1,0");
  EXPECT_EQ(style("Annotation.module", "anchorOffset"), "-27,7");
  const View& main_view = *r.model.views.find("Main");
  for (const auto& h : main_view.hints)
    if (const auto* pin = std::get_if<PinAtHint>(&h)) {
      EXPECT_NE(pin->element, "Annotation.module");
    }
  EXPECT_NE(std::find(main_view.hints.begin(), main_view.hints.end(), LayoutHint{SizeHint{"Annotation.module", 20, 20}}),
            main_view.hints.end());
  EXPECT_EQ(style("Block", "direction"), "south");
  EXPECT_EQ(style("Block", "size"), "10");
  EXPECT_EQ(style("Block", "spacingTop"), "8");
  EXPECT_EQ(style("package", "tabPosition"), "left");
  EXPECT_EQ(style("Comment1_body", "size"), "25");
  EXPECT_EQ(style("Comment1_body", "boundedLbl"), "1");

  auto view = dd::test::ok(resolve_view(r.model, "Main"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const Box* at = geometry.nodes.find("Annotation.module");
  ASSERT_NE(at, nullptr);
  EXPECT_EQ(at->x, 433);
  EXPECT_EQ(at->y, 77);

  const std::string svg = render_svg(view, geometry);
  auto node = [&](const std::string& id) { return dd::test::item_svg(svg, "dd-node", id); };
  // The cube turned south: its depth at the top right, 10 deep; the label below the depth.
  const std::string block = node("Block");
  EXPECT_NE(block.find("<path d=\"M230,70 L60,70 L50,80 L50,190 L220,190 L230,180 Z\""), std::string::npos) << block;
  EXPECT_GT(text_y(block, "Block"), 80);
  // The component: the first line plain, the second bold, both in the middle of the box.
  const std::string component = node("Annotation");
  ASSERT_TRUE(dd::test::contains(component, ">«Annotation»</text>")) << component;
  const auto first = component.rfind("<text ", component.find(">«Annotation»</text>"));
  EXPECT_EQ(component.substr(first, component.find("</text>", first) - first).find("font-weight"), std::string::npos);
  EXPECT_TRUE(dd::test::contains(component, "font-weight=\"600\" fill=\"#000000\">Component</text>")) << component;
  EXPECT_GT(text_y(component, "Component"), 115);
  EXPECT_LT(text_y(component, "«Annotation»"), 115);
  // Modules: two jetties out of the left edge, the body half a jetty in.
  EXPECT_TRUE(dd::test::contains(node("Module"), "<path d=\"M290,190 L380,190 L380,240 L290,240 L290,230 L280,230 "
                                                 "L280,220 L290,220 L290,210 L280,210 L280,200 L290,200 Z\""))
      << node("Module");
  EXPECT_TRUE(dd::test::contains(node("Annotation.module"), "<path d=\"M437,77 L453,77 L453,97 L437,97"))
      << node("Annotation.module");
  // Exported, it is draw.io's relative geometry again, and comes back as the same adornment.
  const std::string drawio = export_drawio(r.model, {DrawioPage{"Main", geometry.nodes, geometry.edges}});
  EXPECT_TRUE(dd::test::contains(drawio, "<mxGeometry x=\"1\" y=\"0\" width=\"20\" height=\"20\" relative=\"1\" "
                                         "as=\"geometry\"><mxPoint x=\"-27\" y=\"7\" as=\"offset\"/></mxGeometry>"))
      << drawio;
  ImportResult again = import_drawio(drawio);
  const Element* icon_again = again.model.elements.find("Annotation.module");
  ASSERT_NE(icon_again, nullptr);
  ASSERT_TRUE(icon_again->style && icon_again->style->find("anchorOffset"));
  EXPECT_EQ(*icon_again->style->find("anchorOffset"), "-27,7");
  // The package: a square tab on the left.
  EXPECT_TRUE(dd::test::contains(node("package"), "<path d=\"M540,84 H540 V70 H580 V84 H670 V150 H540 Z\""))
      << node("package");
  // The note: its corner folded 25 down, the fold drawn, the label under it.
  const std::string note = node("Comment1_body");
  EXPECT_TRUE(dd::test::contains(note, "<path d=\"M70,290 H225 L250,315 V380 H70 Z\"")) << note;
  EXPECT_TRUE(dd::test::contains(note, "<path d=\"M225,290 V315 H250\"")) << note;
  EXPECT_GT(text_y(note, "Comment1 body"), 315);
}

TEST(Drawio, FillsInDrawiosDefaultsForCubesNotesAndFolders) {
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="c" value="cube" vertex="1" parent="1" style="shape=cube;"><mxGeometry x="0" y="0" width="100" height="80" as="geometry"/></mxCell>
    <mxCell id="n" value="note" vertex="1" parent="1" style="shape=note;"><mxGeometry x="200" y="0" width="100" height="80" as="geometry"/></mxCell>
    <mxCell id="f" value="folder" vertex="1" parent="1" style="shape=folder;"><mxGeometry x="400" y="0" width="100" height="80" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  auto style = [&](const std::string& id, const std::string& key) -> std::string {
    const Element* e = r.model.elements.find(id);
    const std::string* v = e && e->style ? e->style->find(key) : nullptr;
    return v ? *v : "<none>";
  };
  EXPECT_EQ(style("cube", "direction"), "east");
  EXPECT_EQ(style("cube", "size"), "20");
  EXPECT_EQ(style("note", "size"), "30");
  EXPECT_EQ(style("folder", "tabWidth"), "60");
  EXPECT_EQ(style("folder", "tabHeight"), "20");
  EXPECT_EQ(style("folder", "tabPosition"), "right");
  auto view = dd::test::ok(resolve_view(r.model, "Main"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  // draw.io's cube has its depth at the top left; its folder has the tab on the right.
  EXPECT_TRUE(dd::test::contains(dd::test::item_svg(svg, "dd-node", "cube"),
                                 "<path d=\"M0,0 L80,0 L100,20 L100,80 L20,80 L0,60 Z\""))
      << svg;
  EXPECT_TRUE(dd::test::contains(dd::test::item_svg(svg, "dd-node", "folder"),
                                 "<path d=\"M400,20 H440 V0 H500 V20 H500 V80 H400 Z\""))
      << svg;
}

TEST(Drawio, DrawsShapeKeysThatMakeNoSenseTheDefaultWay) {
  // Negative sizes and an unknown direction leave DiaDraw's own shapes; a name edited to another
  // number of lines takes the element's font style.
  auto r = dsl::parse(R"(model {
  c = process "C" shape cube style "size=-5;direction=up"
  f = process "F" shape folder style "tabWidth=-1;tabHeight=NaN"
  m = process "M" shape module style "jettyWidth=-3;jettyHeight=abc"
  n = process "one\ntwo\nthree" style "lineFontStyles=0,1"
}
view v {
  include *
  layout fixed
  pin c at 0, 0 size 100, 60
  pin f at 200, 0 size 100, 60
  pin m at 400, 0 size 100, 60
  pin n at 0, 200 size 100, 80
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  auto node = [&](const std::string& id) { return dd::test::item_svg(svg, "dd-node", id); };
  EXPECT_TRUE(dd::test::contains(node("c"), "<polygon points=\"0,9 9,0 100,0 100,51 91,60 0,60\"")) << node("c");
  EXPECT_TRUE(dd::test::contains(node("f"), " l-6,-8 ")) << node("f");
  // The default jetties, 20 × 10.
  EXPECT_TRUE(dd::test::contains(node("m"), "<path d=\"M410,0 L500,0 L500,60 L410,60 L410,40 L400,40")) << node("m");
  EXPECT_FALSE(dd::test::contains(node("n"), "font-weight")) << node("n");
}

TEST(Drawio, ExportsModulesAndShapeKeysWrittenInTheDsl) {
  auto r = dsl::parse(R"(model {
  m = process "M" shape module style "jettyWidth=8;jettyHeight=4"
  c = process "C" shape cube style "direction=north;size=12;boundedLbl=1"
}
view v {
  include *
  layout fixed
  pin m at 0, 0 size 100, 50
  pin c at 200, 0 size 100, 50
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  // North: the cube's depth at the bottom left.
  EXPECT_TRUE(dd::test::contains(dd::test::item_svg(svg, "dd-node", "c"),
                                 "<path d=\"M200,50 L288,50 L300,38 L300,0 L212,0 L200,12 Z\""))
      << svg;
  const std::string drawio = export_drawio(r.model, {DrawioPage{"v", geometry.nodes, geometry.edges}});
  EXPECT_TRUE(dd::test::contains(drawio, "shape=module;whiteSpace=wrap;html=1;jettyWidth=8;jettyHeight=4;")) << drawio;
  EXPECT_TRUE(dd::test::contains(drawio, "direction=north")) << drawio;
  ImportResult again = import_drawio(drawio);
  const Element* m = again.model.elements.find("M");
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(m->shape, std::optional<std::string>("module"));
}

TEST(Drawio, ImportsAShapesDashPatternButNotAnEdges) {
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="a" value="A" style="rounded=0;dashed=1;dashPattern=1 3;" vertex="1" parent="1">
      <mxGeometry x="10" y="20" width="80" height="40" as="geometry"/></mxCell>
    <mxCell id="b" value="B" style="rounded=0;" vertex="1" parent="1">
      <mxGeometry x="200" y="20" width="80" height="40" as="geometry"/></mxCell>
    <mxCell id="e" style="endArrow=open;dashed=1;dashPattern=1 3;" edge="1" parent="1" source="a" target="b">
      <mxGeometry relative="1" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const Element& a = *r.model.elements.find("A");
  ASSERT_TRUE(a.style);
  ASSERT_NE(a.style->find("dashed"), nullptr);
  EXPECT_EQ(*a.style->find("dashed"), "1");
  ASSERT_NE(a.style->find("dashPattern"), nullptr);
  EXPECT_EQ(*a.style->find("dashPattern"), "1 3");
  ASSERT_EQ(r.model.relationships.size(), 1u);
  const Relationship& e = *r.model.relationships.begin();
  EXPECT_EQ(e.line, LineStyle::Dashed);
  EXPECT_TRUE(!e.style || !e.style->find("dashPattern"));
}

TEST(Drawio, ImportsAUmlFrameWithItsNameInATab) {
  // draw.io's fig1: the fill colours the name tab; the frame itself stays unfilled.
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="f" parent="1" value="frame" vertex="1"
      style="shape=umlFrame;whiteSpace=wrap;html=1;pointerEvents=0;fillColor=#ffcc99;strokeColor=#36393d;">
      <mxGeometry height="200" width="320" x="80" y="120" as="geometry"/></mxCell>
    <mxCell id="g" parent="1" value="sd a much longer name" vertex="1" style="shape=umlFrame;">
      <mxGeometry height="100" width="300" x="80" y="400" as="geometry"/></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const Element& f = *r.model.elements.find("frame");
  EXPECT_EQ(f.shape, std::optional<std::string>("frame"));
  ASSERT_TRUE(f.style && f.style->find("fill"));
  EXPECT_EQ(*f.style->find("fill"), "#ffcc99");

  auto view = dd::test::ok(resolve_view(r.model, "Main"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  auto node = [&](const std::string& id) { return dd::test::item_svg(svg, "dd-node", id); };
  const std::string frame = node("frame");
  // An unfilled outline that takes clicks in its empty parts, then the 60 × 30 tab with its corner
  // cut.
  EXPECT_NE(frame.find("<rect x=\"80\" y=\"120\" width=\"320\" height=\"200\" fill=\"none\" stroke=\"#36393d\""),
            std::string::npos)
      << frame;
  EXPECT_NE(frame.find("pointer-events=\"all\""), std::string::npos);
  EXPECT_NE(frame.find("<path d=\"M80,120 H140 V135 L130,150 H80 Z\" fill=\"#ffcc99\""), std::string::npos) << frame;
  // The name sits in the tab, not in the middle of the frame.
  EXPECT_NE(frame.find(">frame</text>"), std::string::npos);
  EXPECT_EQ(frame.find("y=\"220\""), std::string::npos) << frame;
  // A longer name widens the tab instead of spilling out of it.
  const std::string longer = node("sd_a_much_longer_name");
  EXPECT_EQ(longer.find(" H140 "), std::string::npos) << longer;
  EXPECT_NE(longer.find("<path d=\"M80,400 H"), std::string::npos) << longer;
}

TEST(Drawio, DrawsFramesWrittenInTheDslAndExportsThemAsUmlFrames) {
  auto r = dsl::parse(R"(model {
  sd = process "sd" shape frame
  loose = process "Loose" shape frame
  a = process "A"
  b = process "B"
  sys = system "Billing platform\nwith a long name" shape frame {
    api = container "API"
  }
  a -> b "calls"
}
view v {
  include sd loose a b sys sys.api
  layout fixed
  pin sd at 0, 0 size 400, 200
  pin a at 40, 60
  pin b at 240, 60
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  // A leaf frame is drawn behind the nodes and the edges inside it, so they keep their clicks.
  const auto frame_at = svg.find("data-id=\"sd\"");
  ASSERT_NE(frame_at, std::string::npos);
  EXPECT_LT(frame_at, svg.find("data-id=\"a\""));
  EXPECT_LT(frame_at, svg.find("class=\"dd-edge\""));
  EXPECT_NE(dd::test::item_svg(svg, "dd-node", "sd").find("<path d=\"M0,0 H60 V15 L50,30 H0 Z\""), std::string::npos)
      << svg;
  // A frame is measured large enough to hold things.
  const Box* loose = geometry.nodes.find("loose");
  ASSERT_NE(loose, nullptr);
  EXPECT_EQ(loose->width, 200);
  EXPECT_EQ(loose->height, 120);
  const Box* sys = geometry.nodes.find("sys");
  ASSERT_NE(sys, nullptr);
  // A container frame keeps its name on one line in a tab no deeper than the room above its
  // children.
  const std::string container = dd::test::item_svg(svg, "dd-node", "sys");
  EXPECT_NE(container.find(">Billing platform with a long name</text>"), std::string::npos) << container;
  const Box* api = geometry.nodes.find("sys.api");
  ASSERT_NE(api, nullptr);
  EXPECT_GE(api->y, sys->y + 30);
  // The tab fits the name although the child leaves the frame narrower.
  EXPECT_EQ(sys->x, 224);
  EXPECT_EQ(sys->y, 208);
  EXPECT_EQ(sys->width, 152);
  EXPECT_NE(container.find("<path d=\"M224,208 H455 V223 L445,238 H224 Z\""), std::string::npos) << container;
  // Past the frame the tab takes no clicks, as on the native canvases, and the picture's bounds
  // take it in, so exports do not cut it off.
  EXPECT_NE(container.find("H224 Z\" fill=\"#1d4ed8\" stroke=\"#1e40af\" stroke-width=\"1.5\" pointer-events=\"none\"/>"),
            std::string::npos)
      << container;
  EXPECT_NE(svg.find("viewBox=\"-20 -20 495 "), std::string::npos) << svg.substr(0, 200);
  EXPECT_NE(container.find("<g pointer-events=\"none\"><text"), std::string::npos) << container;

  // draw.io gets them back as UML frames.
  const std::string drawio = export_drawio(r.model, {DrawioPage{"v", geometry.nodes, geometry.edges}});
  EXPECT_NE(drawio.find("shape=umlFrame"), std::string::npos) << drawio;
  ImportResult again = import_drawio(drawio);
  const Element* sd = again.model.elements.find("sd");
  ASSERT_NE(sd, nullptr);
  EXPECT_EQ(sd->shape, std::optional<std::string>("frame"));
}

namespace {

// draw.io's fig5: a UML sequence diagram. Lifelines (one an actor) hold activation bars; the
// messages use sequenceEdgeStyle, with fixed ends (entryDy=5), a stale waypoint, a self-call whose
// points are in its lifeline's coordinates, and labels placed along the line.
const char* kSequence = R"(<mxfile><diagram name="Page-1"><mxGraphModel>
<root>
        <mxCell id="0" />
        <mxCell id="1" parent="0" />
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-1" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;whiteSpace=wrap;html=1;container=1;dropTarget=0;collapsible=0;recursiveResize=0;outlineConnect=0;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};" value=":Object" vertex="1">
          <mxGeometry height="300" width="100" x="170" y="80" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-7" parent="P3U-mCGFSPQ3Qx7CZUNc-1" style="html=1;points=[[0,0,0,0,5],[0,1,0,0,-5],[1,0,0,0,5],[1,1,0,0,-5]];perimeter=orthogonalPerimeter;outlineConnect=0;targetShapes=umlLifeline;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};" value="" vertex="1">
          <mxGeometry height="220" width="10" x="45" y="70" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-10" parent="P3U-mCGFSPQ3Qx7CZUNc-1" style="html=1;points=[[0,0,0,0,5],[0,1,0,0,-5],[1,0,0,0,5],[1,1,0,0,-5]];perimeter=orthogonalPerimeter;outlineConnect=0;targetShapes=umlLifeline;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};" value="" vertex="1">
          <mxGeometry height="40" width="10" x="55" y="170" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-11" edge="1" parent="P3U-mCGFSPQ3Qx7CZUNc-1" source="P3U-mCGFSPQ3Qx7CZUNc-7" style="html=1;align=left;spacingLeft=2;endArrow=block;edgeStyle=sequenceEdgeStyle;libavoidRouting=0;curved=0;rounded=0;defaultLabelBackgroundColor=page;labelPadding=0 4;" target="P3U-mCGFSPQ3Qx7CZUNc-10" value="self call">
          <mxGeometry relative="1" y="5" as="geometry">
            <mxPoint as="offset" />
            <Array as="points">
              <mxPoint x="85.00000000000011" y="149.9999999999999" />
              <mxPoint x="85.00000000000011" y="179.9999999999999" />
            </Array>
            <mxPoint x="70" y="150" as="sourcePoint" />
          </mxGeometry>
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-2" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;whiteSpace=wrap;html=1;container=1;dropTarget=0;collapsible=0;recursiveResize=0;outlineConnect=0;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};participant=umlActor;" value="" vertex="1">
          <mxGeometry height="300" width="20" x="90" y="80" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-3" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;whiteSpace=wrap;html=1;container=1;dropTarget=0;collapsible=0;recursiveResize=0;outlineConnect=0;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};" value=":Object" vertex="1">
          <mxGeometry height="310" width="100" x="330" y="80" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-12" parent="P3U-mCGFSPQ3Qx7CZUNc-3" style="html=1;points=[[0,0,0,0,5],[0,1,0,0,-5],[1,0,0,0,5],[1,1,0,0,-5]];perimeter=orthogonalPerimeter;outlineConnect=0;targetShapes=umlLifeline;portConstraint=eastwest;newEdgeStyle={&quot;edgeStyle&quot;:&quot;sequenceEdgeStyle&quot;,&quot;libavoidRouting&quot;:0,&quot;curved&quot;:0,&quot;rounded&quot;:0};" value="" vertex="1">
          <mxGeometry height="50" width="10" x="45" y="230" as="geometry" />
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-8" edge="1" parent="1" source="P3U-mCGFSPQ3Qx7CZUNc-2" style="html=1;verticalAlign=bottom;endArrow=block;edgeStyle=sequenceEdgeStyle;libavoidRouting=0;curved=0;rounded=0;entryX=0;entryY=0;entryDx=0;entryDy=5;defaultLabelBackgroundColor=page;labelPadding=0 4;" target="P3U-mCGFSPQ3Qx7CZUNc-7" value="dispatch">
          <mxGeometry relative="1" x="0.1861" as="geometry">
            <Array as="points">
              <mxPoint x="167.5" y="155" />
            </Array>
            <mxPoint x="120" y="155" as="sourcePoint" />
          </mxGeometry>
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-9" edge="1" parent="1" source="P3U-mCGFSPQ3Qx7CZUNc-7" style="html=1;verticalAlign=bottom;endArrow=open;dashed=1;endSize=8;edgeStyle=sequenceEdgeStyle;libavoidRouting=0;curved=0;rounded=0;exitX=0;exitY=1;exitDx=0;exitDy=-5;defaultLabelBackgroundColor=page;labelPadding=0 4;" target="P3U-mCGFSPQ3Qx7CZUNc-2" value="return">
          <mxGeometry relative="1" x="-0.1688" as="geometry">
            <Array as="points">
              <mxPoint x="167.5" y="225" />
            </Array>
            <mxPoint x="120" y="225" as="targetPoint" />
          </mxGeometry>
        </mxCell>
        <mxCell id="P3U-mCGFSPQ3Qx7CZUNc-13" edge="1" parent="1" source="P3U-mCGFSPQ3Qx7CZUNc-7" style="html=1;verticalAlign=bottom;startArrow=oval;endArrow=block;startSize=8;edgeStyle=sequenceEdgeStyle;libavoidRouting=0;curved=0;rounded=0;entryX=0;entryY=0;entryDx=0;entryDy=5;defaultLabelBackgroundColor=page;labelPadding=0 4;" target="P3U-mCGFSPQ3Qx7CZUNc-12" value="dispatch">
          <mxGeometry relative="1" x="0.44" as="geometry">
            <Array as="points">
              <mxPoint x="332.5000000000001" y="314.9999999999999" />
            </Array>
            <mxPoint x="290" y="315" as="sourcePoint" />
          </mxGeometry>
        </mxCell>
      </root>
</mxGraphModel></diagram></mxfile>)";

const RouteHint* route_of(const View& view, const std::string& relationship) {
  for (const auto& h : view.hints)
    if (const auto* r = std::get_if<RouteHint>(&h); r && r->relationship == relationship) return r;
  return nullptr;
}

}  // namespace

TEST(Drawio, ImportsLifelinesWithAHeadAndADashedLine) {
  ImportResult r = import_drawio(kSequence);
  const Element* object = r.model.elements.find("Object");
  const Element* actor = r.model.elements.find("umlLifeline");
  ASSERT_TRUE(object && actor);
  EXPECT_EQ(object->shape, std::optional<std::string>("lifeline"));
  EXPECT_EQ(actor->shape, std::optional<std::string>("lifeline"));
  ASSERT_TRUE(actor->style && actor->style->find("participant"));
  EXPECT_EQ(*actor->style->find("participant"), "umlActor");

  auto view = dd::test::ok(resolve_view(r.model, "Page_1"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  const std::string lifeline = dd::test::item_svg(svg, "dd-node", "Object");
  // The 40 deep head with the name in it, and a dashed line from under it to the bottom.
  EXPECT_NE(lifeline.find("<path d=\"M220,120 V380\" fill=\"none\""), std::string::npos) << lifeline;
  EXPECT_NE(lifeline.find("stroke-dasharray=\"3 3\""), std::string::npos) << lifeline;
  EXPECT_NE(lifeline.find("<rect x=\"170\" y=\"80\" width=\"100\" height=\"40\""), std::string::npos) << lifeline;
  EXPECT_NE(lifeline.find(">:Object</text>"), std::string::npos) << lifeline;
  EXPECT_LT(std::abs(text_y(lifeline, ":Object") - 100), 8) << lifeline;
  // The actor's head is a stick figure, not a box.
  const std::string figure = dd::test::item_svg(svg, "dd-node", "umlLifeline");
  EXPECT_NE(figure.find("<circle"), std::string::npos) << figure;
  EXPECT_NE(figure.find("<path d=\"M100,120 V380\""), std::string::npos) << figure;
  EXPECT_EQ(figure.find("height=\"40\""), std::string::npos) << figure;
  // Lifelines go behind their activation bars, and the native canvases read the dashes.
  EXPECT_LT(svg.find("data-id=\"Object\""), svg.find("data-id=\"Object.shape_\""));
  auto list = draw::display_list_from_svg(svg);
  ASSERT_TRUE(list);
  bool dashed_line = false;
  for (const auto& item : list->items)
    if (item.kind == draw::Item::Path && item.owner == "Object" && !item.paint.dash.empty()) dashed_line = true;
  EXPECT_TRUE(dashed_line);
}

TEST(Drawio, RoutesSequenceMessagesAsDrawioDoes) {
  ImportResult r = import_drawio(kSequence);
  const View& v = *r.model.views.find("Page_1");
  // Fractions of the bars and lifelines, so the messages stay on them when they move.
  const RouteHint* call = route_of(v, "umlLifeline->Object.shape_");
  ASSERT_NE(call, nullptr);
  EXPECT_TRUE(call->points.empty());
  EXPECT_EQ(call->exit, (Point2{0.5, 0.25}));
  EXPECT_EQ(call->entry, (Point2{0, 0.0227}));

  auto view = dd::test::ok(resolve_view(r.model, "Page_1"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  auto points = [&](const std::string& id) {
    const EdgeRoute* route = geometry.edges.find(id);
    std::vector<std::pair<double, double>> out;
    if (route)
      for (const auto& p : route->points) out.emplace_back(std::round(p.x * 10) / 10, std::round(p.y * 10) / 10);
    return out;
  };
  using Points = std::vector<std::pair<double, double>>;
  // (The ends are kept as fractions to four places, so they land within a hundredth of a pixel.)
  // From the actor's line to the bar's fixed point 5 below its top, straight across.
  EXPECT_EQ(points("umlLifeline->Object.shape_"), (Points{{100, 155}, {215, 155}}));
  // The return leaves 5 above the bar's bottom; its stale waypoint (y 225) does not count.
  EXPECT_EQ(points("Object.shape_->umlLifeline"), (Points{{215, 365}, {100, 365}}));
  // To the other lifeline's bar, from the side of this one facing it.
  EXPECT_EQ(points("Object.shape_->Object2.shape_"), (Points{{225, 315}, {375, 315}}));
  // The self-call: a loop out to the right, its waypoints moved by its lifeline's position.
  EXPECT_EQ(points("Object.shape_->Object.shape_2"), (Points{{225, 230}, {255, 230}, {255, 260}, {235, 260}}));

  // Labels where draw.io puts them: above the line (verticalAlign=bottom), at their spot along it.
  const EdgeRoute* dispatch = geometry.edges.find("umlLifeline->Object.shape_");
  ASSERT_TRUE(dispatch && dispatch->label);
  EXPECT_NEAR(dispatch->label->x + dispatch->label->width / 2, 168.2, 0.1);
  EXPECT_NEAR(dispatch->label->y + dispatch->label->height, 153, 0.01);
  const EdgeRoute* back = geometry.edges.find("Object.shape_->umlLifeline");
  ASSERT_TRUE(back && back->label);
  EXPECT_NEAR(back->label->x + back->label->width / 2, 167.2, 0.1);
  // "self call" starts right of the loop's middle leg (align=left, 5 px off the line).
  const EdgeRoute* self = geometry.edges.find("Object.shape_->Object.shape_2");
  ASSERT_TRUE(self && self->label);
  EXPECT_EQ(self->label->x, 262);
  EXPECT_NEAR(self->label->y + self->label->height / 2, 240, 0.01);

  // The oval start of the second dispatch is drawn.
  const std::string svg = render_svg(view, geometry);
  const std::string second = dd::test::item_svg(svg, "dd-edge", "Object.shape_-&gt;Object2.shape_");
  EXPECT_NE(second.find("marker-start=\"url(#dd-m-oval-f-"), std::string::npos) << second;
}

TEST(Drawio, RoutesSequenceMessagesWithoutFixedEnds) {
  // Both ends float: the waypoint gives the y, kept where both ends reach. Without one it is
  // halfway between the centres. More than one waypoint is a hand-drawn route, kept as it is.
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="a" value="A" vertex="1" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;container=1;">
      <mxGeometry x="0" y="0" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="b" value="B" vertex="1" parent="1" style="shape=umlLifeline;perimeter=lifelinePerimeter;container=1;size=60;">
      <mxGeometry x="200" y="0" width="100" height="200" as="geometry"/></mxCell>
    <mxCell id="m1" value="one" edge="1" parent="1" source="a" target="b" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="150" y="100"/></Array></mxGeometry></mxCell>
    <mxCell id="m2" value="two" edge="1" parent="1" source="a" target="b" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="150" y="250"/></Array></mxGeometry></mxCell>
    <mxCell id="m3" value="three" edge="1" parent="1" source="b" target="a" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"/></mxCell>
    <mxCell id="m4" value="four" edge="1" parent="1" source="a" target="b" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="120" y="80"/><mxPoint x="180" y="90"/></Array></mxGeometry></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const View& v = *r.model.views.find("Main");
  const RouteHint* one = route_of(v, "A->B");
  ASSERT_NE(one, nullptr);
  EXPECT_EQ(one->exit, (Point2{0.5, 0.3333}));
  EXPECT_EQ(one->entry, (Point2{0.5, 0.5}));
  // Below B's bottom: clamped to it.
  const RouteHint* two = route_of(v, "A->B#2");
  ASSERT_NE(two, nullptr);
  EXPECT_EQ(two->exit, (Point2{0.5, 0.6667}));
  EXPECT_EQ(two->entry, (Point2{0.5, 1}));
  // Halfway between the centres (150 and 100).
  const RouteHint* three = route_of(v, "B->A");
  ASSERT_NE(three, nullptr);
  EXPECT_EQ(three->exit, (Point2{0.5, 0.625}));
  EXPECT_EQ(three->entry, (Point2{0.5, 0.4167}));
  const RouteHint* four = route_of(v, "A->B#3");
  ASSERT_NE(four, nullptr);
  EXPECT_EQ(four->points, (std::vector<Point2>{{120, 80}, {180, 90}}));
  EXPECT_FALSE(four->exit);
}

TEST(Drawio, RoutesPinnedSelfAndOutOfReachMessages) {
  // Bars on lifeline A (x 0) and B (x 200); the routes come back as fractions of the bars.
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="A" value="A" vertex="1" parent="1" style="shape=umlLifeline;container=1;">
      <mxGeometry x="0" y="0" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="B" value="B" vertex="1" parent="1" style="shape=umlLifeline;container=1;">
      <mxGeometry x="200" y="0" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="a1" value="a1" vertex="1" parent="A" style="perimeter=orthogonalPerimeter;">
      <mxGeometry x="45" y="50" width="10" height="100" as="geometry"/></mxCell>
    <mxCell id="a2" value="a2" vertex="1" parent="A" style="perimeter=orthogonalPerimeter;">
      <mxGeometry x="55" y="90" width="10" height="30" as="geometry"/></mxCell>
    <mxCell id="c1" value="c1" vertex="1" parent="A" style="perimeter=orthogonalPerimeter;">
      <mxGeometry x="45" y="20" width="10" height="20" as="geometry"/></mxCell>
    <mxCell id="b1" value="b1" vertex="1" parent="B" style="perimeter=orthogonalPerimeter;">
      <mxGeometry x="45" y="150" width="10" height="50" as="geometry"/></mxCell>
    <mxCell id="both" value="both" edge="1" parent="1" source="a1" target="b1"
      style="edgeStyle=sequenceEdgeStyle;exitX=1;exitY=0;exitDy=10;entryX=0;entryY=0;entryDy=10;">
      <mxGeometry x="0.5" y="1e300" relative="1" as="geometry"/></mxCell>
    <mxCell id="reach" value="reach" edge="1" parent="1" source="a1" target="b1"
      style="edgeStyle=sequenceEdgeStyle;entryX=0;entryY=1;">
      <mxGeometry relative="1" as="geometry"/></mxCell>
    <mxCell id="apart" value="apart" edge="1" parent="1" source="c1" target="b1" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"/></mxCell>
    <mxCell id="self" value="self" edge="1" parent="1" source="a1" target="a2" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"/></mxCell>
    <mxCell id="pinned" value="pinned" edge="1" parent="A" source="a1" target="a2"
      style="edgeStyle=sequenceEdgeStyle;entryX=1;entryY=0;entryDy=5;">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="90" y="70"/></Array></mxGeometry></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const View& v = *r.model.views.find("Main");
  auto route = [&](const std::string& drawio_id) -> const RouteHint* {
    for (const auto& rel : r.model.relationships)
      if (const std::string* id = rel.properties.find("drawio.id"); id && *id == drawio_id) return route_of(v, rel.id);
    return nullptr;
  };
  auto check = [&](const std::string& id, std::vector<Point2> via, Point2 exit, Point2 entry) {
    const RouteHint* h = route(id);
    ASSERT_NE(h, nullptr) << id;
    EXPECT_EQ(h->points, via) << id;
    EXPECT_EQ(h->exit, exit) << id;
    EXPECT_EQ(h->entry, entry) << id;
  };
  // Both ends pinned: the pins are the message.
  check("both", {}, {1, 0.1}, {0, 0.2});
  // A pin out of the other end's reach: a straight line from that end's nearest point.
  check("reach", {}, {1, 1}, {0, 1});
  // Ends that share no y: a straight line between them.
  check("apart", {}, {1, 1}, {0, 0});
  // A self-call without waypoints leaves at the bar's middle and comes back 30 lower, 30 out.
  check("self", {{85, 100}, {85, 130}}, {1, 0.5}, {1, 1});
  // A pinned target gives the loop's return; the waypoint (in A's coordinates) its way out.
  check("pinned", {{90, 70}, {90, 95}}, {1, 0.2}, {1, 0.1667});

  // A huge label y is bounded on import, so the spot along the line survives.
  const Relationship* both = nullptr;
  for (const auto& rel : r.model.relationships)
    if (const std::string* id = rel.properties.find("drawio.id"); id && *id == "both") both = &rel;
  ASSERT_TRUE(both && both->style);
  auto anchor = label_anchor_of(both->style);
  ASSERT_TRUE(anchor);
  EXPECT_EQ(anchor->along, 0.5);
  EXPECT_EQ(anchor->away, 1e6);
}

TEST(Drawio, ReadsABadLifelineSizeAsDrawiosDefault) {
  // A negative or unreadable head size is 40 for the messages as for the drawing.
  const char* xml = R"(<mxfile><diagram name="Main"><mxGraphModel><root>
    <mxCell id="0"/><mxCell id="1" parent="0"/>
    <mxCell id="l" value="L" vertex="1" parent="1" style="shape=umlLifeline;size=-5;">
      <mxGeometry x="0" y="0" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="m" value="M" vertex="1" parent="1" style="shape=umlLifeline;size=abc;">
      <mxGeometry x="200" y="0" width="100" height="300" as="geometry"/></mxCell>
    <mxCell id="e" value="early" edge="1" parent="1" source="l" target="m" style="edgeStyle=sequenceEdgeStyle;">
      <mxGeometry relative="1" as="geometry"><Array as="points"><mxPoint x="150" y="10"/></Array></mxGeometry></mxCell>
  </root></mxGraphModel></diagram></mxfile>)";
  ImportResult r = import_drawio(xml);
  const RouteHint* h = route_of(*r.model.views.find("Main"), "L->M");
  ASSERT_NE(h, nullptr);
  EXPECT_EQ(h->exit, (Point2{0.5, 0.1333}));
  EXPECT_EQ(h->entry, (Point2{0.5, 0.1333}));
  auto view = dd::test::ok(resolve_view(r.model, "Main"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  EXPECT_NE(dd::test::item_svg(svg, "dd-node", "L").find("width=\"100\" height=\"40\""), std::string::npos) << svg;
  EXPECT_NE(dd::test::item_svg(svg, "dd-node", "M").find("width=\"100\" height=\"40\""), std::string::npos) << svg;
}

TEST(Drawio, DrawsADashedLifelineWithOneDashPattern) {
  // dashed=1 dashes the head and the line through the paint; the line takes no second pattern,
  // which would make the SVG invalid.
  auto r = dsl::parse(R"(model {
  a = process "A" shape lifeline style "dashed=1"
  b = process "B" shape lifeline style "lifelineDashed=0"
}
view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 300
  pin b at 200, 0 size 100, 300
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string svg = render_svg(view, geometry);
  const std::string a = dd::test::item_svg(svg, "dd-node", "a");
  const auto line = a.find("<path d=\"M50,40 V300\"");
  ASSERT_NE(line, std::string::npos) << a;
  const std::string path = a.substr(line, a.find("/>", line) - line);
  const auto first = path.find("stroke-dasharray=");
  ASSERT_NE(first, std::string::npos) << path;
  EXPECT_EQ(path.find("stroke-dasharray=", first + 1), std::string::npos) << path;
  // lifelineDashed=0 draws the line solid.
  const std::string b = dd::test::item_svg(svg, "dd-node", "b");
  EXPECT_NE(b.find("<path d=\"M250,40 V300\""), std::string::npos) << b;
  EXPECT_EQ(b.find("stroke-dasharray"), std::string::npos) << b;
}

TEST(Drawio, ExportsLifelinesAndMessagesBackToDrawio) {
  // DSL lifelines go out as umlLifeline; a straight message keeps its y in one waypoint and its
  // label's spot in its geometry, so draw.io draws it where it was.
  auto r = dsl::parse(R"(model {
  a = process "A" shape lifeline style "participant=umlActor"
  b = process "B" shape lifeline
  a -> b "call" style "edgeStyle=sequenceEdgeStyle;verticalAlign=bottom;labelAt=0.25,3;labelOffset=1,2"
}
view v {
  include *
  layout fixed
  pin a at 0, 0 size 100, 300
  pin b at 200, 0 size 100, 300
  route "a->b" from 0.5, 0.5 to 0.5, 0.5
}
)");
  ASSERT_TRUE(r.diagnostics.empty()) << r.diagnostics.front().message;
  auto view = dd::test::ok(resolve_view(r.model, "v"));
  auto geometry = dd::test::ok(layout(view, create_measure()));
  const std::string drawio = export_drawio(r.model, {DrawioPage{"v", geometry.nodes, geometry.edges}});
  EXPECT_NE(drawio.find("shape=umlLifeline;perimeter=lifelinePerimeter;"), std::string::npos) << drawio;
  EXPECT_NE(drawio.find("participant=umlActor;"), std::string::npos) << drawio;
  EXPECT_NE(drawio.find("verticalAlign=bottom;"), std::string::npos) << drawio;
  EXPECT_NE(drawio.find("<mxGeometry x=\"0.25\" y=\"3\" relative=\"1\" as=\"geometry\"><Array as=\"points\">"
                        "<mxPoint x=\"150\" y=\"150\"/></Array><mxPoint x=\"1\" y=\"2\" as=\"offset\"/></mxGeometry>"),
            std::string::npos)
      << drawio;
  ImportResult again = import_drawio(drawio);
  const Relationship* call = again.model.relationships.find("A->B");
  ASSERT_TRUE(call && call->style);
  ASSERT_TRUE(call->style->find("labelAt") && call->style->find("labelOffset"));
  EXPECT_EQ(*call->style->find("labelAt"), "0.25,3");
  EXPECT_EQ(*call->style->find("labelOffset"), "1,2");
  const RouteHint* back = route_of(*again.model.views.find("v"), "A->B");
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->exit, (Point2{0.5, 0.5}));
  EXPECT_EQ(back->entry, (Point2{0.5, 0.5}));
}

TEST(Import, DetectsFormats) {
  EXPECT_EQ(detect_format("a.mmd", ""), std::optional<Format>(Format::Mermaid));
  EXPECT_EQ(detect_format("a.drawio.png", ""), std::optional<Format>(Format::Drawio));
  EXPECT_EQ(detect_format("a.txt", "<mxfile>"), std::optional<Format>(Format::Drawio));
  EXPECT_EQ(detect_format("a.txt", "\n  flowchart LR\nA-->B"), std::optional<Format>(Format::Mermaid));
  EXPECT_EQ(detect_format("a.txt", "@startuml"), std::optional<Format>(Format::PlantUml));
  EXPECT_FALSE(detect_format("a.txt", "hello").has_value());
}

TEST(Bpmn, ImportsAProcessWithDiagramInterchange) {
  const char* xml = R"(<?xml version="1.0"?>
<bpmn:definitions xmlns:bpmn="http://www.omg.org/spec/BPMN/20100524/MODEL" xmlns:camunda="http://camunda.org/schema/1.0/bpmn" id="d">
  <bpmn:process id="p" isExecutable="true">
    <bpmn:startEvent id="s"><bpmn:timerEventDefinition id="t"><bpmn:timeDuration>PT5M</bpmn:timeDuration></bpmn:timerEventDefinition></bpmn:startEvent>
    <bpmn:userTask id="u" name="Review  order" camunda:assignee="me"/>
    <bpmn:sequenceFlow id="f" sourceRef="s" targetRef="u"/>
  </bpmn:process>
  <bpmndi:BPMNDiagram id="D" xmlns:bpmndi="http://www.omg.org/spec/BPMN/20100524/DI"><bpmndi:BPMNPlane bpmnElement="p">
    <bpmndi:BPMNShape bpmnElement="s"><dc:Bounds x="10" y="20" width="36" height="36"/></bpmndi:BPMNShape>
    <bpmndi:BPMNShape bpmnElement="u"><dc:Bounds x="100" y="0" width="100" height="80"/></bpmndi:BPMNShape>
    <bpmndi:BPMNEdge bpmnElement="f"><di:waypoint x="46" y="38"/><di:waypoint x="100" y="40"/></bpmndi:BPMNEdge>
  </bpmndi:BPMNPlane></bpmndi:BPMNDiagram>
</bpmn:definitions>)";
  ImportResult r = import_bpmn(xml);
  EXPECT_TRUE(r.report.dropped.empty());
  ASSERT_EQ(r.model.elements.size(), 2u);
  const Element& s = *r.model.elements.find("s");
  EXPECT_EQ(s.shape, std::optional<std::string>("bpmn.start_event_timer"));
  EXPECT_EQ(*s.properties.find("bpmn.timer"), "timeDuration=PT5M");
  const Element& u = *r.model.elements.find("u");
  EXPECT_EQ(u.name, "Review order");
  EXPECT_EQ(u.shape, std::optional<std::string>("bpmn.user_task"));
  EXPECT_EQ(*u.properties.find("bpmn.attr.camunda:assignee"), "me");
  EXPECT_EQ(*u.properties.find("bpmn.xmlns.camunda"), "http://camunda.org/schema/1.0/bpmn");
  ASSERT_EQ(r.model.relationships.size(), 1u);
  const View& v = *r.model.views.find("p");
  EXPECT_EQ(v.layout.algorithm, "fixed");
  ASSERT_EQ(v.hints.size(), 3u);
  EXPECT_EQ(std::get<RouteHint>(v.hints[2]).exit, std::optional<Point2>(Point2{1, 0.5}));
}

TEST(Bpmn, RejectsOtherXml) {
  ImportResult r = import_bpmn("<root/>");
  ASSERT_EQ(r.report.dropped.size(), 1u);
  EXPECT_EQ(r.report.dropped[0].construct, std::optional<std::string>("definitions"));
}

TEST(Bpmn, ExportsWhatItImportsFromFixedGeometry) {
  const char* xml = R"(<bpmn:definitions xmlns:bpmn="http://www.omg.org/spec/BPMN/20100524/MODEL">
  <bpmn:process id="p">
    <bpmn:startEvent id="s"/><bpmn:exclusiveGateway id="g" default="f2"/><bpmn:endEvent id="e"/>
    <bpmn:sequenceFlow id="f1" sourceRef="s" targetRef="g"/>
    <bpmn:sequenceFlow id="f2" sourceRef="g" targetRef="e"/>
  </bpmn:process>
  <bpmndi:BPMNDiagram id="D"><bpmndi:BPMNPlane bpmnElement="p">
    <bpmndi:BPMNShape bpmnElement="s"><dc:Bounds x="0" y="0" width="36" height="36"/></bpmndi:BPMNShape>
    <bpmndi:BPMNShape bpmnElement="g"><dc:Bounds x="100" y="0" width="50" height="50"/></bpmndi:BPMNShape>
    <bpmndi:BPMNShape bpmnElement="e"><dc:Bounds x="200" y="0" width="36" height="36"/></bpmndi:BPMNShape>
    <bpmndi:BPMNEdge bpmnElement="f1"><di:waypoint x="36" y="18"/><di:waypoint x="100" y="25"/></bpmndi:BPMNEdge>
  </bpmndi:BPMNPlane></bpmndi:BPMNDiagram>
</bpmn:definitions>)";
  ImportResult first = import_bpmn(xml);
  std::vector<BpmnPage> pages = fixed_pages(first.model);
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_EQ(pages[0].nodes.size(), 3u);
  EXPECT_EQ(pages[0].edges.size(), 1u);
  std::string out = export_bpmn(first.model, pages);
  EXPECT_NE(out.find("<bpmn:exclusiveGateway id=\"g\" default=\"f2\">"), std::string::npos);
  EXPECT_NE(out.find(std::string("exporterVersion=\"") + dd_version() + "\""), std::string::npos);
  EXPECT_NE(out.find("<dc:Bounds x=\"100\" y=\"0\" width=\"50\" height=\"50\"/>"), std::string::npos);
  ImportResult second = import_bpmn(out);
  EXPECT_TRUE(second.report.dropped.empty());
  EXPECT_EQ(second.model.elements.size(), 3u);
  EXPECT_EQ(second.model.relationships.size(), 2u);
}

TEST(Structurizr, ImportsDslWithViewsStylesAndIncludes) {
  StructurizrOptions options;
  options.resolve = [](const std::string& path, const std::optional<std::string>&)
      -> std::optional<std::vector<IncludedFile>> {
    if (path != "people.dsl") return std::nullopt;
    return std::vector<IncludedFile>{{"people.dsl", "user = person \"User\" \"Someone\" \"External\""}};
  };
  const char* dsl = R"(workspace "Shop" {
  !const TECH "Java"
  model {
    !include people.dsl
    shop = softwareSystem "Shop" {
      api = container "API" "Serves" "${TECH}"
      db = container "DB" "" "" "Database"
    }
    user -> api "Uses" /* inline */ "HTTPS"
    api -> db "Reads"
  }
  views {
    container shop "Containers" {
      include *
      autoLayout lr 300 150
    }
    dynamic shop "Flow" {
      user -> api "Request"
      { api -> db "Query" }
    }
    styles {
      element "Database" {
        shape cylinder
        background #ff0000
      }
    }
  }
})";
  ImportResult r = import_structurizr(dsl, options);
  EXPECT_TRUE(r.report.dropped.empty()) << r.report.dropped[0].message;
  ASSERT_EQ(r.model.elements.size(), 4u);
  const Element& api = *r.model.elements.find("shop.api");
  EXPECT_EQ(api.technology, std::optional<std::string>("Java"));
  const Element& db = *r.model.elements.find("shop.db");
  EXPECT_EQ(db.shape, std::optional<std::string>("cylinder"));
  EXPECT_EQ(*db.style->find("fill"), "#ff0000");
  EXPECT_EQ(r.model.relationships.size(), 2u);
  const View& containers = *r.model.views.find("Containers");
  EXPECT_EQ(containers.layout.direction, Direction::Right);
  EXPECT_EQ(containers.layout.spacing, std::optional<double>(50));
  const View& flow = *r.model.views.find("Flow");
  EXPECT_EQ(flow.layout.algorithm, "sequence");
  ASSERT_EQ(flow.sequence->size(), 2u);
  EXPECT_TRUE(std::holds_alternative<SequenceFrame>((*flow.sequence)[1]));
}

TEST(Structurizr, ImportsWorkspaceJsonWithPositions) {
  const char* json = R"({"name":"W","model":{"people":[{"id":"1","name":"User","tags":"Element,Person",
    "relationships":[{"id":"3","sourceId":"1","destinationId":"2","description":"Uses"}]}],
    "softwareSystems":[{"id":"2","name":"System","tags":"Element,Software System"}]},
    "views":{"systemLandscapeViews":[{"key":"Landscape","elements":[{"id":"1","x":10,"y":20},{"id":"2","x":500,"y":20}],
    "relationships":[{"id":"3","vertices":[{"x":300,"y":100}]}]}]}})";
  ImportResult r = import_structurizr(json);
  ASSERT_EQ(r.model.elements.size(), 2u);
  const View& v = *r.model.views.find("Landscape");
  EXPECT_EQ(v.layout.algorithm, "fixed");
  ASSERT_EQ(v.hints.size(), 3u);
  EXPECT_EQ(std::get<PinAtHint>(v.hints[0]).width, std::optional<double>(400));
  EXPECT_EQ(std::get<RouteHint>(v.hints[2]).points, (std::vector<Point2>{{300, 100}}));
}

TEST(PlantUml, ClassDiagramWithPreprocessor) {
  const char* src = R"(@startuml
!define ENTITY(name) class name <<entity>>
!$prefix = "Shop"
ENTITY(Order)
class Customer {
  +name: String
  +orders(): List
}
Customer "1" --> "*" Order : $prefix places
@enduml)";
  ImportResult r = import_plantuml(src);
  EXPECT_TRUE(r.report.dropped.empty()) << r.report.dropped[0].message;
  ASSERT_EQ(r.model.elements.size(), 2u);
  const Element& order = *r.model.elements.find("Order");
  EXPECT_EQ(*order.properties.find("stereotype"), "entity");
  const Element& customer = *r.model.elements.find("Customer");
  EXPECT_EQ(*customer.properties.find("uml.methods"), "+orders(): List");
  ASSERT_EQ(r.model.relationships.size(), 1u);
  const Relationship& rel = *r.model.relationships.begin();
  EXPECT_EQ(rel.label, std::optional<std::string>("Shop places"));
  EXPECT_EQ(*rel.properties.find("uml.targetMultiplicity"), "*");
}

TEST(PlantUml, SequenceWithFrames) {
  const char* src = R"(@startuml
actor User
participant "Web App" as web
User -> web ++ : login
alt ok
  web --> User : token
else failed
  web --> User : error
end
return done
@enduml)";
  ImportResult r = import_plantuml(src);
  const View& v = *r.model.views.begin();
  EXPECT_EQ(v.layout.algorithm, "sequence");
  ASSERT_TRUE(v.sequence);
  ASSERT_GE(v.sequence->size(), 3u);
  const auto& frame = std::get<SequenceFrame>((*v.sequence)[2]);
  EXPECT_EQ(frame.frame, FrameKind::Alt);
  ASSERT_EQ(frame.sections.size(), 2u);
  EXPECT_EQ(frame.sections[1].label, std::optional<std::string>("failed"));
  EXPECT_EQ(r.model.elements.find("web")->name, "Web App");
}

TEST(PlantUml, C4WithDirectionsAndIncludes) {
  const char* src = R"(@startuml
!include <C4/C4_Container>
Person(user, "User")
System_Boundary(s, "Shop") {
  Container(api, "API", "Go")
}
Rel_R(user, api, "Uses")
@enduml)";
  ImportResult r = import_plantuml(src);
  EXPECT_TRUE(r.report.dropped.empty());
  EXPECT_EQ(r.model.elements.find("s.api")->technology, std::optional<std::string>("Go"));
  const View& v = *r.model.views.begin();
  ASSERT_EQ(v.hints.size(), 1u);
  EXPECT_EQ(std::get<PinHint>(v.hints[0]).placement, Placement::RightOf);
}

TEST(Codec, HashesAndGzip) {
  auto hex = [](const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
      out.push_back("0123456789abcdef"[c >> 4]);
      out.push_back("0123456789abcdef"[c & 15]);
    }
    return out;
  };
  EXPECT_EQ(hex(sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(hex(sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(base64_encode("hello"), "aGVsbG8=");
  EXPECT_EQ(base64_encode("hi!"), "aGkh");
  EXPECT_EQ(crc32("123456789"), 0xCBF43926u);
  std::string text(5000, 'x');
  EXPECT_EQ(gunzip(gzip(text)), std::optional<std::string>(text));
}

#include "diadraw/io/drawio_stencil.hpp"

TEST(DrawioStencil, ConvertsShapesAndSkipsTheRest) {
  auto set = dd::io::convert_stencil_set(R"(<shapes name="mxgraph.Test">
    <shape name="Big Box" w="20" h="10" strokewidth="2"><foreground>
      <rect x="0" y="0" w="20" h="10"/><fillstroke/>
      <save/><strokecolor color="none"/><ellipse x="5" y="2" w="4" h="4"/><stroke/><restore/>
    </foreground></shape>
    <shape name="Big Box" w="5" h="5"><foreground><path><move x="0" y="0"/><line x="5" y="5"/></path><fill/></foreground></shape>
    <shape name="Empty" w="0" h="5"/>
    <shape name="Nothing" w="5" h="5"><foreground><text str="x"/></foreground></shape>
  </shapes>)",
                                         "test.xml");
  EXPECT_EQ(set.name, "mxgraph.test");
  ASSERT_EQ(set.shapes.size(), 2u);
  EXPECT_EQ(set.shapes[0].id, "big_box");
  EXPECT_EQ(set.shapes[1].id, "big_box_");
  EXPECT_EQ(set.shapes[0].style, "mxgraph.test.big_box");
  EXPECT_EQ(set.shapes[0].body,
            "<path d=\"M0 0 h20 v10 h-20 Z\" fill=\"#fffffe\" stroke=\"#000001\" stroke-width=\"2\"/>"
            "<path d=\"M5 4 A2 2 0 1 0 9 4 A2 2 0 1 0 5 4 Z\" fill=\"none\" stroke=\"none\"/>");
  // Only fills: a glyph, in the glyph colour.
  EXPECT_EQ(set.shapes[1].body, "<path d=\"M0 0 L5 5\" fill=\"#374152\" stroke=\"none\"/>");
  ASSERT_EQ(set.skipped.size(), 2u);
  EXPECT_EQ(set.skipped[0].reason, "no size");
  EXPECT_EQ(set.skipped[1].reason, "nothing drawn");
}
