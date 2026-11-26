// The inspector's pickers and visual style editor (host/field_editors.hpp): the choices a document
// offers, and the style text each control writes.

#include "host/field_editors.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/lsp/features.hpp"
#include "diadraw/render/svg.hpp"
#include "host/document.hpp"

using namespace dd;
using namespace dd::host;

namespace {

const std::string kSource = R"(model {
  shop = system "Shop" tag "internal" {
    web = container "Web app" tech "React" tag "external" tag "legacy"
    db = database "Orders" tech "Postgres" shape cylinder
    odd = gadget "Odd one"
  }
  shop.web -> shop.db "reads" tech "SQL" tag "async"
}
)";

bool has(const std::vector<Choice>& choices, const std::string& value) {
  return std::any_of(choices.begin(), choices.end(), [&](const Choice& c) { return c.value == value; });
}

}  // namespace

TEST(FieldChoices, OffersBuiltInsAndWhatTheModelUses) {
  Document doc(kSource, "/nonexistent/shop.draw");
  FieldChoices c = doc.choices();
  ASSERT_GE(c.kinds.size(), builtin_kinds().size());
  EXPECT_EQ(c.kinds.front().value, "person");
  EXPECT_TRUE(has(c.kinds, "database"));
  // A kind no picker entry has still shows, so the picker can display it.
  ASSERT_TRUE(has(c.kinds, "gadget"));
  EXPECT_EQ(c.kinds.back().group, "Used");
  EXPECT_TRUE(has(c.shapes, "cylinder"));
  EXPECT_EQ(c.shapes.size(), base_shapes().size());
  EXPECT_EQ(c.tags, (std::vector<std::string>{"async", "external", "internal", "legacy"}));
  EXPECT_EQ(c.technologies, (std::vector<std::string>{"Postgres", "React", "SQL"}));
}

TEST(FieldChoices, SplitsAndJoinsTags) {
  EXPECT_EQ(split_tags(" external,legacy , ,external"), (std::vector<std::string>{"external", "legacy"}));
  EXPECT_EQ(split_tags(""), std::vector<std::string>{});
  EXPECT_EQ(join_tags({"external", " legacy ", ""}), "external, legacy");
}

TEST(StyleEditor, SetsAndRemovesKeysInPlace) {
  std::string style = "fill=#dae8fc;stroke=#6c8ebf;rounded=1";
  EXPECT_EQ(style_value(style, "stroke"), std::optional<std::string>("#6c8ebf"));
  EXPECT_EQ(style_value(style, "dashed"), std::nullopt);
  EXPECT_EQ(with_style_value(style, "stroke", "#000000"), "fill=#dae8fc;stroke=#000000;rounded=1");
  EXPECT_EQ(with_style_value(style, "dashed", "1"), style + ";dashed=1");
  EXPECT_EQ(with_style_value(style, "fill", std::nullopt), "stroke=#6c8ebf;rounded=1");
  EXPECT_EQ(with_style_value("", "fill", "#ffffff"), "fill=#ffffff");
  // Unchanged text stays exactly as written.
  EXPECT_EQ(with_style_value("fill=#fff;;", "fill", "#fff"), "fill=#fff;;");
  EXPECT_EQ(with_style_value("fill=#fff;;", "dashed", std::nullopt), "fill=#fff;;");
}

TEST(StyleEditor, ControlsBackAtTheirDefaultRemoveTheKey) {
  const auto& keys = style_keys(StyleTarget::Elements);
  auto key = [&](const std::string& name) {
    return *std::find_if(keys.begin(), keys.end(), [&](const StyleKey& k) { return k.key == name; });
  };
  EXPECT_EQ(style_setting(key("rounded"), "0"), std::nullopt);
  EXPECT_EQ(style_setting(key("rounded"), "1"), std::optional<std::string>("1"));
  EXPECT_EQ(style_setting(key("align"), ""), std::nullopt);
  EXPECT_EQ(style_setting(key("align"), "left"), std::optional<std::string>("left"));
  EXPECT_EQ(style_setting(key("fontStyle"), "0"), std::nullopt);
  EXPECT_EQ(style_setting(key("fill"), ""), std::nullopt);
  EXPECT_EQ(style_setting(key("fontSize"), "14"), std::optional<std::string>("14"));

  const auto& edge = style_keys(StyleTarget::Relationships);
  auto end_fill = *std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == "endFill"; });
  EXPECT_EQ(style_setting(end_fill, "1"), std::nullopt);  // filled is the default
  EXPECT_EQ(style_setting(end_fill, "0"), std::optional<std::string>("0"));
}

TEST(StyleEditor, OnePopUpPicksTheLineStyle) {
  // Elements and relationships both get the pop-up instead of a Dashed box and a Pattern pop-up.
  for (auto target : {StyleTarget::Elements, StyleTarget::Relationships}) {
    const auto& keys = style_keys(target);
    EXPECT_TRUE(std::none_of(keys.begin(), keys.end(),
                             [](const StyleKey& k) { return k.key == "dashed" || k.key == "dashPattern"; }));
    auto line = std::find_if(keys.begin(), keys.end(), [](const StyleKey& k) { return k.key == kLineStyleKey; });
    ASSERT_NE(line, keys.end());
    std::vector<std::string> labels;
    for (const auto& c : line->choices) labels.push_back(c.label);
    EXPECT_EQ(labels, (std::vector<std::string>{"Solid", "Dashed", "Dotted", "Short dashes", "Long dashes",
                                                "Dash and dot", "Dash and two dots"}));
  }
  const auto& edge = style_keys(StyleTarget::Relationships);
  auto key = [&](std::string_view name) {
    return *std::find_if(edge.begin(), edge.end(), [&](const StyleKey& k) { return k.key == name; });
  };
  auto line = key(kLineStyleKey);
  // A pattern turns dashing on; the plain dashes drop the pattern; solid removes both keys.
  EXPECT_EQ(with_style_setting("stroke=#c00", line, "1 3"), "stroke=#c00;dashed=1;dashPattern=1 3");
  EXPECT_EQ(with_style_setting("dashed=1;dashPattern=1 3", line, "8 3 1 3"), "dashed=1;dashPattern=8 3 1 3");
  EXPECT_EQ(with_style_setting("dashed=1;dashPattern=1 3", line, "dashed"), "dashed=1");
  EXPECT_EQ(with_style_setting("stroke=#c00", line, "dashed"), "stroke=#c00;dashed=1");
  EXPECT_EQ(with_style_setting("stroke=#c00;dashed=1;dashPattern=1 3", line, ""), "stroke=#c00");
  EXPECT_EQ(with_style_setting("dashed=0", line, ""), "");
  // Unchanged text stays exactly as written.
  EXPECT_EQ(with_style_setting("dashPattern=1 3;dashed=1", line, "1 3"), "dashPattern=1 3;dashed=1");
  // What the pop-up shows: both keys read together; a pattern on a solid line does not show.
  EXPECT_EQ(style_control_value("stroke=#c00", line), std::nullopt);
  EXPECT_EQ(style_control_value("dashed=0;dashPattern=1 3", line), std::nullopt);
  EXPECT_EQ(style_control_value("dashed=1", line), std::optional<std::string>("dashed"));
  EXPECT_EQ(style_control_value("dashed=1;dashPattern=", line), std::optional<std::string>("dashed"));
  EXPECT_EQ(style_control_value("dashPattern=1 3;dashed=1", line), std::optional<std::string>("1 3"));
  EXPECT_EQ(style_control_value("stroke=#c00", key("stroke")), std::optional<std::string>("#c00"));
  // Both keys belong to the pop-up, so Other leaves them out and keeps them when edited.
  EXPECT_EQ(other_style("dashed=1;dashPattern=1 3;shadow=1", StyleTarget::Elements), "shadow=1");
  EXPECT_EQ(with_other_style("dashed=1;dashPattern=1 3;shadow=1", StyleTarget::Elements, ""),
            "dashed=1;dashPattern=1 3");
  EXPECT_EQ(with_style_setting("", key("double"), "1"), "double=1");
  EXPECT_EQ(with_style_setting("double=1", key("double"), "0"), "");
}

TEST(StyleEditor, EndsAreGroupedAndUseDrawIoNames) {
  const auto& edge = style_keys(StyleTarget::Relationships);
  auto end = *std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == "endArrow"; });
  std::vector<std::string> crows_foot;
  for (const auto& c : end.choices)
    if (c.group == "Crow's foot") crows_foot.push_back(c.value);
  EXPECT_EQ(crows_foot, (std::vector<std::string>{"ERone", "ERmandOne", "ERzeroToOne", "ERmany", "ERoneToMany",
                                                  "ERzeroToMany"}));
  // Default and None come first, without a group.
  EXPECT_EQ(end.choices[0].value, "");
  EXPECT_EQ(end.choices[1].value, "none");
  EXPECT_EQ(end.choices[1].group, "");
  // Each end previews as a line with that end; Default as the arrow it draws.
  EXPECT_EQ(style_sample(end, ""), (Style{{"endArrow", "classic"}}));
  EXPECT_EQ(style_sample(end, "ERmany"), (Style{{"endArrow", "ERmany"}}));
  auto start = *std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == "startArrow"; });
  EXPECT_EQ(style_sample(start, ""), (Style{{"startArrow", "none"}, {"endArrow", "none"}}));
  EXPECT_EQ(style_sample(start, "diamond"), (Style{{"startArrow", "diamond"}, {"endArrow", "none"}}));
  auto line = *std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == kLineStyleKey; });
  EXPECT_EQ(style_sample(line, ""), (Style{{"endArrow", "none"}}));
  EXPECT_EQ(style_sample(line, "dashed"), (Style{{"endArrow", "none"}, {"dashed", "1"}}));
  EXPECT_EQ(style_sample(line, "1 3"), (Style{{"endArrow", "none"}, {"dashed", "1"}, {"dashPattern", "1 3"}}));
  EXPECT_FALSE(style_sample(*std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == "stroke"; }), "#000"));
}

TEST(StyleEditor, APreviewIsRenderedAgainOnlyWhenItsColoursOrSizeChange) {
  LinePreview preview(Style{{"endArrow", "block"}, {"endFill", "0"}});
  auto stroke_of = [](const draw::DisplayList& list) {
    for (const auto& item : list.items)
      if (item.kind == draw::Item::Path && item.paint.stroke) return *item.paint.stroke;
    return draw::Rgb{-1, -1, -1};
  };
  // The hollow end is filled with the surface.
  auto has_fill = [](const draw::DisplayList& list, draw::Rgb c) {
    return std::any_of(list.items.begin(), list.items.end(), [&](const draw::Item& item) {
      return item.kind == draw::Item::Path && item.paint.fill && std::abs(item.paint.fill->r - c.r) < 1e-6 &&
             std::abs(item.paint.fill->g - c.g) < 1e-6 && std::abs(item.paint.fill->b - c.b) < 1e-6;
    });
  };
  const auto& first = preview.list("#ff0000", "#ffffff", 48, 14);
  ASSERT_TRUE(first);
  EXPECT_EQ(stroke_of(*first).r, 1);
  EXPECT_EQ(first->width, 48);
  EXPECT_TRUE(has_fill(*first, {1, 1, 1}));
  // The same colours and size give the list already drawn: a new one would have new storage.
  const draw::Item* drawn = first->items.data();
  const auto& again = preview.list("#ff0000", "#ffffff", 48, 14);
  ASSERT_TRUE(again);
  EXPECT_EQ(again->items.data(), drawn);
  // Another ink, surface or size draws it again.
  const auto& inked = preview.list("#0000ff", "#ffffff", 48, 14);
  ASSERT_TRUE(inked);
  EXPECT_EQ(stroke_of(*inked).b, 1);
  const auto& dark = preview.list("#0000ff", "#2d2d2d", 48, 14);
  ASSERT_TRUE(dark);
  EXPECT_TRUE(has_fill(*dark, {0x2d / 255.0, 0x2d / 255.0, 0x2d / 255.0}));
  EXPECT_FALSE(has_fill(*dark, {1, 1, 1}));
  const auto& wider = preview.list("#0000ff", "#2d2d2d", 60, 14);
  ASSERT_TRUE(wider);
  EXPECT_EQ(wider->width, 60);
}

TEST(StyleEditor, EveryEndThePickerOffersIsDrawnAndCompleted) {
  // The end names live in the picker, the renderer and the language server's completions; an end
  // added to one must be added to the others.
  const auto& edge = style_keys(StyleTarget::Relationships);
  auto end = *std::find_if(edge.begin(), edge.end(), [](const StyleKey& k) { return k.key == "endArrow"; });
  std::string text = "style relationship {\n  endArrow \n}\n";
  auto parsed = dsl::parse(text);
  std::vector<std::string> completed;
  for (const auto& c : lsp::completions(text, text.find("endArrow ") + std::string_view("endArrow ").size(), parsed, nullptr)) completed.push_back(c.label);
  for (const auto& c : end.choices) {
    if (c.value.empty()) continue;
    EXPECT_NE(std::find(completed.begin(), completed.end(), c.value), completed.end()) << c.value;
    std::string svg = line_sample_svg(Style{{"endArrow", c.value}}, 60, 20);
    if (c.value == "none")
      EXPECT_EQ(svg.find("<marker"), std::string::npos);
    else
      EXPECT_NE(svg.find("id=\"dd-m-" + c.value + "-"), std::string::npos) << c.value;
  }
}

TEST(StyleEditor, OtherHoldsTheKeysWithoutAControl) {
  std::string style = "fill=#dae8fc;shadow=1;stroke=#6c8ebf;whiteSpace=wrap";
  EXPECT_EQ(other_style(style, StyleTarget::Elements), "shadow=1;whiteSpace=wrap");
  // Unchanged: the text stays as written.
  EXPECT_EQ(with_other_style(style, StyleTarget::Elements, "shadow=1;whiteSpace=wrap"), style);
  EXPECT_EQ(with_other_style(style, StyleTarget::Elements, "whiteSpace=wrap ; shadow=1"), style);
  EXPECT_EQ(with_other_style(style, StyleTarget::Elements, "shadow=0"), "fill=#dae8fc;stroke=#6c8ebf;shadow=0");
  EXPECT_EQ(with_other_style(style, StyleTarget::Elements, ""), "fill=#dae8fc;stroke=#6c8ebf");
  // A key with a control typed in Other sets it.
  EXPECT_EQ(with_other_style("fill=#fff", StyleTarget::Elements, "fill=#000;glass=1"), "fill=#000;glass=1");
  // endArrow has a control on relationships only.
  EXPECT_EQ(other_style("endArrow=open;edgeStyle=orthogonalEdgeStyle;jettySize=auto", StyleTarget::Relationships),
            "jettySize=auto");
  EXPECT_EQ(other_style("endArrow=open", StyleTarget::Elements), "endArrow=open");
}

TEST(StyleEditor, ReadsColoursAndFontStyles) {
  EXPECT_EQ(normalized_color("#ABC"), std::optional<std::string>("#aabbcc"));
  EXPECT_EQ(normalized_color(" #DAE8FC "), std::optional<std::string>("#dae8fc"));
  EXPECT_EQ(normalized_color("none"), std::nullopt);
  EXPECT_EQ(normalized_color("#12345"), std::nullopt);
  EXPECT_EQ(normalized_color("#ggg"), std::nullopt);
  EXPECT_EQ(font_style_bits("5"), 5);
  EXPECT_EQ(font_style_bits("bold"), 0);
  EXPECT_EQ(font_style_bits(""), 0);
}

TEST(StyleEditor, EveryKeyIsWellFormed) {
  for (auto target : {StyleTarget::Elements, StyleTarget::Relationships}) {
    for (const auto& k : style_keys(target)) {
      EXPECT_FALSE(k.label.empty()) << k.key;
      EXPECT_FALSE(k.section.empty()) << k.key;
      if (k.control == StyleControl::Choice) {
        ASSERT_GE(k.choices.size(), 2u) << k.key;
        EXPECT_EQ(k.choices.front().value, "") << k.key;
      }
      if (k.control == StyleControl::Number) {
        EXPECT_LT(k.min, k.max) << k.key;
      }
      if (k.control == StyleControl::Flag) {
        EXPECT_TRUE(k.fallback == "0" || k.fallback == "1") << k.key;
      }
    }
  }
}

TEST(Properties, ReadAsTheFieldIsRead) {
  using Rows = std::vector<std::pair<std::string, std::string>>;
  EXPECT_EQ(property_rows(" owner = Ann \n\ntier=1\nflag\n=orphan\nowner=Bob"),
            (Rows{{"owner", "Bob"}, {"tier", "1"}, {"flag", ""}}));
  EXPECT_EQ(property_rows(""), Rows{});
  EXPECT_EQ(property_rows("url=https://x?a=b"), (Rows{{"url", "https://x?a=b"}}));
}

TEST(Properties, WriteOneLinePerRow) {
  EXPECT_EQ(property_text({{"owner", "Ann"}, {"", "dropped"}, {"a=b", "two\nlines"}, {" tier ", " 1 "}}),
            "owner=Ann\na_b=two lines\ntier=1");
  EXPECT_EQ(property_text({}), "");
  // A round trip keeps the text the field shows.
  EXPECT_EQ(property_text(property_rows("owner=Ann\ntier=1")), "owner=Ann\ntier=1");
}

TEST(StyleBlocks, SplitAtTheFirstColonOutsideQuotes) {
  auto rows = block_rows("container: fill=#438dd5\n\n  tag \"a:b\" :dashed=1\nrelationship\n");
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0], (BlockRow{"container", "fill=#438dd5"}));
  EXPECT_EQ(rows[1], (BlockRow{"tag \"a:b\"", "dashed=1"}));
  EXPECT_EQ(rows[2], (BlockRow{"relationship", ""}));
  EXPECT_EQ(block_text(rows), "container: fill=#438dd5\ntag \"a:b\": dashed=1\nrelationship: ");
  EXPECT_EQ(block_text({{"", "fill=#fff"}, {"person", "fill=#08427b;text=#ffffff"}}),
            "person: fill=#08427b;text=#ffffff");
}

TEST(StyleBlocks, KnowWhatTheyStyle) {
  EXPECT_EQ(selector_target("relationship"), StyleTarget::Relationships);
  EXPECT_EQ(selector_target(" relationship tag \"async\""), StyleTarget::Relationships);
  EXPECT_EQ(selector_target("relationships"), StyleTarget::Elements);
  EXPECT_EQ(selector_target("tag \"external\""), StyleTarget::Elements);
  EXPECT_EQ(selector_target("*"), StyleTarget::Elements);
  FieldChoices c;
  c.kinds = {{"container", "container", ""}};
  c.tags = {"a\"b"};
  EXPECT_EQ(selector_choices(c), (std::vector<std::string>{"*", "relationship", "container", "tag \"a\\\"b\"",
                                                           "relationship tag \"a\\\"b\""}));
}

TEST(FieldChoices, OfferBuiltInAndDeclaredThemes) {
  Document doc("theme brand extends dark { background \"#101418\" }\nmodel { a = person \"A\" }\n", "/nonexistent/t.draw");
  FieldChoices c = doc.choices();
  ASSERT_GE(c.themes.size(), 4u);
  EXPECT_EQ(c.themes.front().value, "default");
  EXPECT_TRUE(has(c.themes, "dark"));
  EXPECT_TRUE(has(c.themes, "mono"));
  EXPECT_EQ(c.themes.back(), (Choice{"brand", "brand", "Declared"}));
  EXPECT_TRUE(is_color_key("background"));
  EXPECT_FALSE(is_color_key("fontFamily"));
}

TEST(StyleEditor, NewLineStylesGoToDrawIo) {
  Document doc(R"(model {
  a = process "A" style "dashed=1;dashPattern=8 3 1 3"
  b = process "B"
  c = process "C"
  a -> b style "dashed=1;dashPattern=1 3;curved=1;rounded=1"
  a -> c
}
view v { include * }
)",
               "/nonexistent/t.draw");
  (void)doc.refresh();
  std::string out = doc.drawio();
  EXPECT_NE(out.find("rounded=1;html=1;dashed=1;dashPattern=1 3;curved=1;"), std::string::npos) << out;
  // An element's outline keeps its pattern too.
  EXPECT_NE(out.find("dashed=1;dashPattern=8 3 1 3;"), std::string::npos) << out;
  // A line without them is exported as before.
  EXPECT_NE(out.find("edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;"), std::string::npos) << out;
}
