// Values a document controls (style keys, theme keys, library kind colours) end up in SVG
// attributes. Whatever they hold, the rendered SVG keeps them as attribute values: no markup, no
// event handler and no other attribute can come out of one. Hosts put the SVG into a page (the web
// canvas sets it as markup), so a value that broke out would run script there.

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/io/drawio_stencil.hpp"
#include "diadraw/io/xml.hpp"
#include "diadraw/render/svg.hpp"
#include "helpers.hpp"

using namespace dd;

namespace {

/// Breaks out of a double-quoted attribute, adds a handler and opens a script element. No `;`, so
/// it stays one value in an inline `style "k=v;k=v"`.
const std::string kPayload = R"dd(#f00"/><script>alert(1)</script><g onload="alert(2))dd";
/// The payload as a DSL string literal.
const std::string kQuoted = R"dd("#f00\"/><script>alert(1)</script><g onload=\"alert(2)")dd";

using dd::test::contains;
using dd::test::render;

Model parse_ok(const std::string& text) {
  auto r = dsl::parse(text);
  for (const auto& d : r.diagnostics) ADD_FAILURE() << d.message;
  return r.model;
}

/// Replaces every `$P` in a DSL template with the payload as a string literal, and every `$I` with
/// it escaped for the inside of one (an inline `style "k=v"`).
std::string with_payload(std::string text) {
  const std::string inner = kQuoted.substr(1, kQuoted.size() - 2);
  for (const auto& [mark, with] : {std::pair{"$P", kQuoted}, std::pair{"$I", inner}})
    for (auto at = text.find(mark); at != std::string::npos; at = text.find(mark, at + with.size()))
      text.replace(at, 2, with);
  return text;
}

/// Parses `svg` and checks that it holds nothing but the markup the renderer writes: no script
/// element, no event handler attribute, and no value escaped twice (a value escaped once holds no
/// entity once parsed). Returns how many attribute values hold the payload, which shows the values
/// arrived intact, escaped rather than dropped.
int checked_payload_attributes(const std::string& svg) {
  EXPECT_FALSE(contains(svg, "<script")) << svg;
  EXPECT_FALSE(contains(svg, "onload=\"")) << svg;
  auto doc = io::parse_xml(svg);
  int intact = 0;
  io::walk_xml(*doc, [&](const io::XmlNode& node) {
    EXPECT_NE(node.name, "script");
    for (const auto& [name, value] : node.attrs) {
      EXPECT_FALSE(name.starts_with("on")) << name << " on <" << node.name << ">";
      for (const char* entity : {"&amp;", "&lt;", "&gt;", "&quot;"})
        EXPECT_FALSE(contains(value, entity)) << name << "=\"" << value << "\" is escaped twice";
      if (contains(value, kPayload)) ++intact;
    }
  });
  return intact;
}

}  // namespace

TEST(SvgEscaping, InlineElementStyles) {
  // Base shapes, a person, a styled container and a sideways swimlane all draw from Paint.
  auto model = parse_ok(with_payload(R"dd(model {
  a = system "A" style "fill=$I;stroke=$I;fontColor=$I;dashed=1;dashPattern=$I"
  p = person "P" style "fill=$I;stroke=$I;fontColor=$I"
  c = container "C" style "fill=$I;stroke=$I;fontColor=$I;shape=swimlane" {
    inner = component "Inner" style "fill=$I;fontColor=$I;shape=cylinder"
  }
  pool = process "Pool" style "shape=swimlane;horizontal=0;fill=$I;stroke=$I;fontColor=$I" {
    step = process "Step"
  }
}
view all { include * })dd"));
  EXPECT_GE(checked_payload_attributes(render(model, "all")), 10);
}

TEST(SvgEscaping, RelationshipStylesAndTheirEnds) {
  auto model = parse_ok(with_payload(R"dd(model {
  a = system "A"
  b = system "B"
  c = system "C"
  a -> b "calls" style "stroke=$I;fontColor=$I;dashed=1;dashPattern=$I;startArrow=diamond;endArrow=oval;double=1"
  b -> c "next" style "stroke=$I;endArrow=ERzeroToMany;startArrow=circlePlus;startFill=0"
}
view all { include * })dd"));
  std::string svg = render(model, "all");
  EXPECT_GE(checked_payload_attributes(svg), 6);
  // Every end a line points at is a marker the SVG defines.
  int ends = 0;
  const std::string ref = "marker-end=\"url(#";
  for (auto at = svg.find(ref); at != std::string::npos; at = svg.find(ref, at + 1)) {
    const auto from = at + ref.size();
    const std::string id = svg.substr(from, svg.find(')', from) - from);
    EXPECT_TRUE(contains(svg, "<marker id=\"" + id + "\"")) << id;
    ++ends;
  }
  EXPECT_GE(ends, 2);
}

TEST(SvgEscaping, StyleBlocks) {
  auto model = parse_ok(with_payload(R"dd(style * { fill $P stroke $P fontColor $P }
style container { fill $P stroke $P fontColor $P strokeWidth 2 }
style relationship { stroke $P fontColor $P }
model {
  s = system "S" {
    web = container "Web"
    api = container "API"
  }
  s.web -> s.api "uses"
  group "Group" { x = component "X" }
}
view all { include * })dd"));
  EXPECT_GE(checked_payload_attributes(render(model, "all")), 6);
}

TEST(SvgEscaping, DeclaredThemes) {
  auto model = parse_ok(with_payload(R"dd(theme evil {
  background $P
  fontFamily $P
  edge $P
  edgeText $P
  boundary $P
  boundaryText $P
  surface $P
  dividerFill $P
  labelText $P
  node default { fill $P stroke $P text $P }
  node person { fill $P stroke $P text $P }
  node note { fill $P stroke $P text $P }
  node external { fill $P }
}
theme evil
model {
  user = person "User"
  s = system "S" {
    web = container "Web"
    api = container "API"
  }
  ext = system "Ext" tag "external"
  user -> s.web "visits" style "endArrow=block;endFill=0"
  s.web -> s.api "calls"
  s.api -> ext "pays"
}
view all {
  include *
  note n "A note" below user
  box b "A box" around s.web s.api
  text "A caption"
}
view seq {
  include user s.web s.api
  layout sequence
  sequence {
    user -> s.web "Log in"
    activate s.web
    alt "valid" {
      s.web --> user "Welcome"
      note over s.web s.api "token issued"
    }
    deactivate s.web
    divider "Later"
    delay "5 minutes"
    ref over user s.web "Logout"
  }
})dd"));
  EXPECT_GE(checked_payload_attributes(render(model, "all")), 10);
  EXPECT_GE(checked_payload_attributes(render(model, "seq")), 8);
}

TEST(SvgEscaping, OrdinaryValuesAreEscapedOnce) {
  auto model = parse_ok(R"dd(theme brand {
  fontFamily "Ann & \"Bob\" Sans"
}
theme brand
model { a = system "A" style "fill=#123456;stroke=#abcdef" }
view all { include * })dd");
  std::string svg = render(model, "all");
  EXPECT_TRUE(contains(svg, "font-family=\"Ann &amp; &quot;Bob&quot; Sans\"")) << svg;
  EXPECT_TRUE(contains(svg, "fill=\"#123456\" stroke=\"#abcdef\"")) << svg;
  EXPECT_EQ(checked_payload_attributes(svg), 0);
}

TEST(SvgEscaping, Decorations) {
  auto model = parse_ok(with_payload(R"dd(model { a = system "A" }
view all {
  include *
  note n "Note" below a style "fill=$I;stroke=$I;fontColor=$I;dashed=1;dashPattern=$I"
  text "Text" style "fill=$I;stroke=$I;fontColor=$I"
  box b "Box" around a style "fill=$I;stroke=$I;fontColor=$I;dashPattern=$I"
})dd"));
  EXPECT_GE(checked_payload_attributes(render(model, "all")), 8);
}

TEST(SvgEscaping, HostThemesAndLibraryKinds) {
  // A host's own theme (a settings panel, a library theme) and library kinds' colours.
  Theme theme = default_theme();
  theme.background = kPayload;
  theme.edge = kPayload;
  theme.font_family = kPayload;
  theme.nodes["default"] = {kPayload, kPayload, kPayload};
  RenderOptions options;
  options.theme = &theme;
  options.kinds = [](const std::string& kind) -> std::optional<KindArt> {
    if (kind != "acme.box") return std::nullopt;
    KindArt art;
    art.fill = kPayload;
    art.stroke = kPayload;
    art.text = kPayload;
    art.stereotype = kPayload;
    return art;
  };
  // The document has no `use` for the library: the parser warns, and the host resolves the kind.
  auto model = dsl::parse(R"dd(model {
  a = acme.box "A"
  b = system "B"
  a -> b "uses"
}
view all { include * })dd").model;
  EXPECT_GE(checked_payload_attributes(render(model, "all", options)), 6);
  options.theme = nullptr;
  EXPECT_GE(checked_payload_attributes(render(model, "all", options)), 3);
}

TEST(SvgEscaping, LineSamples) {
  Style style;
  style.set("stroke", kPayload);
  style.set("dashed", "1");
  style.set("dashPattern", kPayload);
  style.set("endArrow", "diamond");
  Theme theme = default_theme();
  theme.background = kPayload;
  theme.surface = kPayload;
  std::string svg = line_sample_svg(style, 60, 20, theme);
  EXPECT_GE(checked_payload_attributes(svg), 3);
}

TEST(SvgEscaping, DrawioStencils) {
  // A stencil library's colours, dash pattern, line join and cap become attributes of the shape's
  // drawing.
  auto set = io::convert_stencil_set(R"dd(<shapes name="mxgraph.Evil">
    <shape name="Box" w="20" h="10"><foreground>
      <fillcolor color="#f00&quot;/&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;g onload=&quot;alert(2)"/>
      <strokecolor color="#f00&quot;/&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;g onload=&quot;alert(2)"/>
      <dashed dashed="1"/>
      <dashpattern pattern="#f00&quot;/&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;g onload=&quot;alert(2)"/>
      <linejoin join="#f00&quot;/&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;g onload=&quot;alert(2)"/>
      <linecap cap="#f00&quot;/&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;g onload=&quot;alert(2)"/>
      <rect x="0" y="0" w="20" h="10"/><fillstroke/>
    </foreground></shape>
  </shapes>)dd",
                                          "evil.xml");
  ASSERT_EQ(set.shapes.size(), 1u);
  EXPECT_EQ(checked_payload_attributes("<svg>" + set.shapes[0].body + "</svg>"), 5);
}
