// The library SVG sanitizer (`sanitize_svg`, `sanitize_svg_body`) against the ways markup gets
// past a regex cleaner, and library drawings from bundles nobody sanitised.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/library/builtin.hpp"
#include "diadraw/library/library.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;
using namespace dd::library;

namespace {

std::string clean(const std::string& body) { return sanitize_svg_body(body); }

/// Nothing in `out` that a browser would run or fetch, and every tag in the one form the sanitizer
/// writes: double-quoted, escaped values, no attribute named on….
void expect_inert(const std::string& out) {
  std::string lower = js::to_lower(out);
  for (const char* bad : {"<script", "javascript:", "vbscript:", "<style", "<foreignobject", "<set", "<animate",
                          "<iframe", "<img", "<feimage", "http:", "https:", "image-set", "cross-fade", "<!--", "<!doctype", "<![cdata["})
    EXPECT_EQ(lower.find(bad), std::string::npos) << bad << " in " << out;
  static const js::Regex kTag("<[^>]*>", "g");
  static const js::Regex kCanonical(R"~(^<(/?[A-Za-z]+|[A-Za-z]+( [a-zA-Z:-]+="[^"<>]*")*/?)>$)~");
  static const js::Regex kHandler(R"~(\son[a-z]*=")~", "i");
  for (const auto& m : kTag.match_all(out)) {
    EXPECT_TRUE(kCanonical.test(m.str(0))) << m.str(0) << " in " << out;
    EXPECT_FALSE(kHandler.test(m.str(0))) << m.str(0) << " in " << out;
  }
}

// Each one got through, or could get through, a cleaner made of regular expressions.
const std::vector<std::string>& bypasses() {
  static const std::vector<std::string> cases = {
      // Event handlers without quotes, without a value, in upper case, after a slash.
      R"~(<rect onclick=alert(1) width="5"/>)~",
      R"~(<rect onmouseover=alert`1` x=1 />)~",
      R"~(<rect ONLOAD="alert(1)"/>)~",
      R"~(<rect onload/>)~",
      R"~(<rect/onclick=alert(1)>)~",
      R"~(<rect fill="red"onclick="alert(1)"/>)~",
      R"~(<rect	onclick	=	"alert(1)"/>)~",
      "<rect\nonfocus=alert(1)\n/>",
      // Script and friends in every spelling.
      R"~(<script>alert(1)</script>)~",
      R"~(<SCRIPT>alert(1)</SCRIPT >)~",
      R"~(<script xlink:href="data:,alert(1)"/>)~",
      R"~(<script><![CDATA[alert(1)]]></script>)~",
      R"~(<scr<script>ipt>alert(1)</script>)~",
      R"~(<script>alert(1)</script <rect/>)~",
      R"~(<foreignObject><iframe src="javascript:alert(1)"/></foreignObject>)~",
      R"~(<foreignObject><foreignObject></foreignObject><img src=x onerror=alert(1)></foreignObject>)~",
      R"~(<style>rect{background:url(http://evil/x)}</style>)~",
      // Animation that rewrites an attribute into a URL.
      R"~(<a><set attributeName="href" to="javascript:alert(1)"/><rect/></a>)~",
      R"~(<animate attributeName="href" values="javascript:alert(1)"/>)~",
      R"~(<use href="#x"><set attributeName="href" to="javascript:alert(1)"/></use>)~",
      // URLs: scripts, other documents, entity-encoded schemes, unquoted values.
      R"~(<a href="javascript:alert(1)"><rect/></a>)~",
      R"~(<a xlink:href=javascript:alert(1)><rect/></a>)~",
      R"~(<use href="jav&#x61;script:alert(1)"/>)~",
      R"~(<use href="&#106;avascript:alert(1)"/>)~",
      R"~(<use href=" javascript:alert(1)"/>)~",
      R"~(<use href="http://evil/sprite.svg#x"/>)~",
      R"~(<use xlink:href=//evil/sprite.svg#x />)~",
      R"~(<image href="data:image/svg+xml;base64,PHN2ZyBvbmxvYWQ9YWxlcnQoMSk+"/>)~",
      R"~(<image href="http://evil/track.png"/>)~",
      R"~(<feImage href="http://evil/x.png"/>)~",
      R"~(<rect fill="url(http://evil/x)"/>)~",
      R"~(<rect fill="URL( 'http://evil/x' )"/>)~",
      R"~(<rect fill="u\72l(http://evil/x)"/>)~",
      R"~(<rect style="fill:url(http://evil/x);stroke:red"/>)~",
      R"~(<rect style="background-image:url(http://evil/x)"/>)~",
      R"~(<rect style="fill:red;behavior:url(x.htc)"/>)~",
      R"~(<rect style="fill:'red;}*{background:url(http://evil)"/>)~",
      // CSS image functions take a quoted string as a URL.
      R"~(<rect style="mask:image-set('https://evil.example/x.png' 1x)"/>)~",
      R"~(<rect style="mask:-webkit-image-set('https://evil.example/x.png' 1x)"/>)~",
      R"~(<rect mask="image-set('https://evil.example/x.png' 1x)"/>)~",
      R"~(<rect style="mask:cross-fade('https://evil.example/x.png' 50%, red)"/>)~",
      R"~(<rect fill="image(&quot;https://evil.example/x.png&quot;)"/>)~",
      R"~(<rect style="mask:image-set(/*;font-family:*/'https://evil.example/x.png' 1x)"/>)~",
      R"~(<rect style="mask:cross-fade(/*;font:*/'https://evil.example/x.png' 50%, red)"/>)~",
      R"~(<rect style="mask:image-set(;font-family:'https://evil.example/x.png' 1x)"/>)~",
      // Breaking out of a quoted value or a text run.
      R"~(<rect fill='a"onclick="alert(1)'/>)~",
      R"~(<title></title><script>alert(1)</script><title></title>)~",
      R"~(<title>&lt;img src=x onerror=alert(1)&gt;</title>)~",
      R"~(<title><img src=x onerror=alert(1)></title>)~",
      R"~(<text>a < b <script>alert(1)</script></text>)~",
      R"~(<rect id="x" fill="red"/><!--><script>alert(1)</script>-->)~",
      R"~(<!-- --!><script>alert(1)</script> -->)~",
      R"~(<?xml-stylesheet href="http://evil/x.css"?><rect/>)~",
      R"~(<rect/></svg><script>alert(1)</script>)~",
      R"~(<svg:script>alert(1)</svg:script>)~",
      R"~(<html:iframe src="javascript:alert(1)"/>)~",
  };
  return cases;
}

}  // namespace

TEST(SvgSanitize, BypassesComeOutInert) {
  for (const auto& svg : bypasses()) {
    SCOPED_TRACE(svg);
    expect_inert(clean(svg));
    auto art = sanitize_svg(R"~(<svg viewBox="0 0 10 10">)~" + svg + "</svg>", "lib-a-");
    ASSERT_TRUE(art);
    expect_inert(art->body);
  }
}

TEST(SvgSanitize, KeepsWhatDrawingsMayUse) {
  EXPECT_EQ(clean(R"~(<rect onclick=alert(1) width="5"/>)~"), R"~(<rect width="5"/>)~");
  EXPECT_EQ(clean(R"~(<rect fill="red"onclick="alert(1)"/>)~"), R"~(<rect fill="red"/>)~");
  EXPECT_EQ(clean(R"~(<a href="javascript:alert(1)"><rect/></a>)~"), "<rect/>");
  EXPECT_EQ(clean(R"~(<rect fill="url(http://evil/x)" stroke="url( '#g' )"/>)~"),
            R"~(<rect fill="none" stroke="url(#g)"/>)~");
  EXPECT_EQ(clean(R"~(<rect style="fill:url(http://evil/x);stroke:red;background:blue"/>)~"),
            R"~(<rect style="fill:none;stroke:red"/>)~");
  EXPECT_EQ(clean(R"~(<rect style="background:blue"/>)~"), "<rect/>");
  EXPECT_EQ(clean(R"~(<text font-family='a"b'/>)~"), R"~(<text font-family="a&quot;b"/>)~");
  EXPECT_EQ(clean(R"~(<rect fill='a"b'/>)~"), "<rect/>");
  EXPECT_EQ(clean(R"~(<title>Hi<img src=x onerror=alert(1)/></title>)~"), "<title>Hi</title>");
  EXPECT_EQ(clean(R"~(<text>a < b &amp; c</text>)~"), "<text>a &lt; b &amp; c</text>");
  EXPECT_EQ(clean(R"~(<text><![CDATA[<b>]]></text>)~"), "<text>&lt;b&gt;</text>");
  EXPECT_EQ(clean(R"~(<image href="data:image/png;base64,AAAA" width="4"/>)~"),
            R"~(<image href="data:image/png;base64,AAAA" width="4"/>)~");
  EXPECT_EQ(clean(R"~(<use href="http://evil/s.svg#x" x="1"/>)~"), R"~(<use x="1"/>)~");
  EXPECT_EQ(clean(R"~(<use xlink:href="#a" href="#b"/>)~"), R"~(<use href="#a"/>)~");
  EXPECT_EQ(clean(R"~(<text style="font-family:'Segoe UI'" font-family="'Inter', sans-serif">a</text>)~"),
            R"~(<text style="font-family:'Segoe UI'" font-family="'Inter', sans-serif">a</text>)~");
  EXPECT_EQ(clean("<text>\xff\xfe&#xFFFE;\xEF\xBF\xBF</text>"),
            "<text>\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD</text>");
  EXPECT_EQ(clean(R"~(<g><rect/></g></svg><rect/>)~"), "<g><rect/></g>");
  EXPECT_EQ(clean("<g><rect x=1 y=2></g>"), R"~(<g><rect x="1" y="2"/></g>)~");
  EXPECT_EQ(clean(R"~(<linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient>)~"),
            R"~(<linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient>)~");
}

TEST(SvgSanitize, ExpandsNoDtdEntities) {
  auto art = sanitize_svg(R"~(<!DOCTYPE svg [<!ENTITY x "<script>alert(1)</script>"> ]>)~"
                          R"~(<svg viewBox="0 0 1 1"><text>&x;&#60;&#x3e;&#0;</text></svg>)~",
                          "");
  ASSERT_TRUE(art);
  EXPECT_EQ(art->body, "<text>&amp;x;&lt;&gt;\xEF\xBF\xBD</text>");
}

TEST(SvgSanitize, PrefixesIdsAndTheirReferences) {
  auto art = sanitize_svg(
      R"~(<svg width="10" height="10"><defs><linearGradient id=g /><clipPath id='c'/></defs>)~"
      R"~(<rect fill="url(#g)" style="clip-path:url(#c)" mask="url(#other)"/><use xlink:href="#g"/></svg>)~",
      "p-");
  ASSERT_TRUE(art);
  EXPECT_EQ(art->view_box, "0 0 10 10");
  EXPECT_EQ(art->body,
            R"~(<defs><linearGradient id="p-g"/><clipPath id="p-c"/></defs>)~"
            R"~(<rect fill="url(#p-g)" style="clip-path:url(#p-c)" mask="none"/><use href="#p-g"/>)~");
}

TEST(SvgSanitize, ReadsTheSizeFromAnyQuoting) {
  auto art = sanitize_svg("<svg width=24 height='12' onload=alert(1)><rect/></svg>", "");
  ASSERT_TRUE(art);
  EXPECT_EQ(art->view_box, "0 0 24 12");
  EXPECT_EQ(art->body, "<rect/>");
  EXPECT_FALSE(sanitize_svg("<rect/>", ""));
  EXPECT_FALSE(sanitize_svg("<svg><rect/></svg>", ""));
}

TEST(SvgSanitize, DropsWhatIsNestedTooDeep) {
  std::string deep;
  for (int i = 0; i < 100000; ++i) deep += "<g>";
  std::string out = clean(deep + "<rect/>");
  EXPECT_LT(out.size(), 2000u);
  EXPECT_EQ(out.find("<rect"), std::string::npos);
}

TEST(SvgSanitize, IsIdempotent) {
  for (const auto& svg : bypasses()) {
    std::string once = clean(svg);
    EXPECT_EQ(clean(once), once) << svg;
  }
}

TEST(SvgSanitize, LeavesBuiltinDrawingsAlone) {
  int drawings = 0;
  for (const auto& bundle : builtin_bundles().as_array()) {
    const Json* shapes = bundle.get("shapes");
    if (!shapes || !shapes->is_object()) continue;
    for (const auto& [id, shape] : shapes->as_object()) {
      const Json* svg = shape.get("svg");
      if (!svg || !svg->is_string()) continue;
      ++drawings;
      EXPECT_EQ(clean(svg->as_string()), svg->as_string()) << id;
    }
  }
  EXPECT_GT(drawings, 50);
}

// A bundle that skipped `lib build`: what `lib add`, the cache or the web app's addLibrary hand over.
TEST(SvgSanitize, DrawsUnsanitisedBundlesSafely) {
  auto bundle = Json::parse(R"~({"format":"diadraw-library","formatVersion":1,"name":"evil","version":"1.0.0",
    "license":"MIT",
    "shapes":{
      "bad":{"svg":"<rect onclick=alert(1) width=\"4\"/><script>alert(1)</script>","viewBox":"0 0 10 10"}},
    "composites":{
      "pair":{"width":20,"height":10,"parts":[
        {"shape":"bad","x":"0\" onload=\"alert(1)","y":0,"width":10,"height":10}]}}})~");
  ASSERT_TRUE(bundle);
  auto r = resolve_uses({{"evil", "^1", "evil"}}, {memory_source({std::make_shared<const Json>(*bundle)})});
  ASSERT_TRUE(r.unresolved.empty());
  auto bad = r.set.art("evil.bad");
  ASSERT_TRUE(bad && bad->shape.body);
  EXPECT_EQ(*bad->shape.body, R"~(<rect width="4"/>)~");
  auto pair = r.set.art("evil.pair");
  ASSERT_TRUE(pair && pair->shape.body);
  expect_inert(*pair->shape.body);
  EXPECT_NE(pair->shape.body->find(R"~(x="0&quot; onload=&quot;alert(1)")~"), std::string::npos) << *pair->shape.body;
}

// Ids from a bundle are prefixed when drawn, so they cannot take the page's own (markers, other
// drawings); and a library's colours cannot match across markup when the renderer swaps them.
TEST(SvgSanitize, PrefixesIdsAndChecksColoursWhenDrawn) {
  auto bundle = Json::parse(R"~({"format":"diadraw-library","formatVersion":1,"name":"evil","version":"1.0.0",
    "license":"MIT","colors":{"fill":["AAA\" d=\"","#ABCDEF","red"]},
    "shapes":{"bad":{"svg":"<marker id=\"arrow\"/><rect fill=\"AAA\" d=\"M0 onclick=alert(1) x\" marker-end=\"url(#arrow)\"/><use href=\"#page\" fill=\"url(#page)\"/>",
                     "viewBox":"0 0 10 10"}}})~");
  ASSERT_TRUE(bundle);
  auto r = resolve_uses({{"evil", "^1", "ev"}}, {memory_source({std::make_shared<const Json>(*bundle)})});
  auto bad = r.set.art("ev.bad");
  ASSERT_TRUE(bad && bad->shape.body);
  EXPECT_EQ(*bad->shape.body,
            R"~(<marker id="lib-ev-bad-arrow"/><rect fill="AAA" d="M0 onclick=alert(1) x" marker-end="url(#lib-ev-bad-arrow)"/>)~"
            R"~(<use fill="none"/>)~");
  EXPECT_EQ(bad->shape.fill_colors, (std::vector<std::string>{"#ABCDEF", "red"}));
}

// Inputs that made the tokenizer quadratic. Each is linear now and finishes in milliseconds; a
// quadratic loop takes minutes here under the sanitizers.
TEST(SvgSanitize, StaysLinearOnHostileInput) {
  constexpr int kN = 200000;
  auto start = std::chrono::steady_clock::now();
  std::string attrs = "<rect";
  for (int i = 0; i < kN; ++i) attrs += " a" + std::to_string(i) + "=\"1\"";
  EXPECT_EQ(clean(attrs + " width=\"1\"/>"), "<rect/>");  // past the attribute cap

  std::string unmatched;
  for (int i = 0; i < kN; ++i) unmatched += "<g>";
  for (int i = 0; i < kN; ++i) unmatched += "</q>";
  EXPECT_EQ(clean(unmatched).find("</q"), std::string::npos);

  EXPECT_EQ(clean("<text>" + std::string(1 << 20, '&') + "</text>").size(), 13u + 5 * (1u << 20));

  std::string strays;
  for (int i = 0; i < kN; ++i) strays += "< ";
  EXPECT_EQ(clean("<text>" + strays + "</text>").size(), 13u + 5 * kN);
  // About 2 s under the sanitizers; a quadratic loop takes minutes.
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(60));
}
