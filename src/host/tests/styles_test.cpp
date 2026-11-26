// Style blocks and themes as inspector fields (host/styles.hpp): what the fields show, and the
// text a committed field writes through a Document.

#include "host/styles.hpp"

#include <gtest/gtest.h>

#include "diadraw/dsl/parse.hpp"
#include "host/document.hpp"

using namespace dd;
using namespace dd::host;

namespace {

const std::string kSource = R"(theme brand extends dark {
  background "#101418"
  node person { fill "#08427b" }
}

theme brand

style container {
  fill "#438dd5"   // blue
}

style tag "external" { fill "#999999" dashed true }

style relationship { dashed true }

model {
  shop = system "Shop" {
    web = container "Web app" tag "external"
    db = database "Orders"
  }
}

view checkout of shop {
  include *
}
)";

/// Applies an inspector operation to a document and returns the new text.
std::string commit(const std::string& source, const Result<std::optional<Operation>>& op) {
  EXPECT_TRUE(op.ok()) << (op.ok() ? "" : op.error().message);
  if (!op.ok() || !*op) return source;
  Document doc(source, "");
  auto change = doc.apply(**op);
  EXPECT_TRUE(change.ok()) << (change.ok() ? "" : change.error().message);
  if (change.ok() && *change) {
    EXPECT_FALSE((*change)->note) << "reprinted:\n" << doc.text();
  }
  return doc.text();
}

const Element& element(const Model& model, const std::string& id) { return *model.elements.find(id); }

}  // namespace

TEST(StyleFields, ShowTheBlocksThatApplyToAnElement) {
  Model model = dsl::parse(kSource).model;
  EXPECT_EQ(shared_styles(model, element(model, "shop.web")),
            "container: fill=#438dd5\ntag \"external\": fill=#999999;dashed=1");
  EXPECT_EQ(shared_styles(model, element(model, "shop.db")), "");
}

TEST(StyleFields, ReadSelectorsAsTheyAreWritten) {
  auto rules = parse_style_lines("*: fill=#fff\n\ncontainer tag \"a:b\": stroke=red\nrelationship tag async: dashed=1");
  ASSERT_TRUE(rules.ok());
  ASSERT_EQ(rules->size(), 3u);
  EXPECT_FALSE((*rules)[0].kind);
  EXPECT_EQ((*rules)[1].kind, "container");
  EXPECT_EQ((*rules)[1].tag, "a:b");
  EXPECT_EQ((*rules)[2].target, StyleTarget::Relationships);
  EXPECT_EQ((*rules)[2].tag, "async");
  EXPECT_FALSE(parse_style_lines("container fill=red").ok());
  EXPECT_FALSE(parse_style_lines("relationship container: fill=red").ok());
  // What adding the block would refuse, with its line.
  auto key = parse_style_lines("container: fill=red\ncontainer: fill colour=red");
  ASSERT_FALSE(key.ok());
  EXPECT_EQ(key.error().message, "Line 2: Key \"fill colour\" of a style block is not a name");
  EXPECT_FALSE(parse_style_lines("x-y: fill=red").ok());
  EXPECT_FALSE(parse_style_lines("container: =red").ok());
}

TEST(StyleFields, ChangingAValueEditsThatEntry) {
  Model model = dsl::parse(kSource).model;
  auto text = commit(kSource, shared_styles_operation(model, element(model, "shop.web"),
                                                      "container: fill=#1168bd\ntag \"external\": fill=#999999;dashed=1"));
  EXPECT_NE(text.find("style container {\n  fill \"#1168bd\"   // blue\n}"), std::string::npos) << text;
  EXPECT_NE(text.find("style tag \"external\" { fill \"#999999\" dashed true }"), std::string::npos) << text;
}

TEST(StyleFields, KeysKeepTheirOrderWhateverOrderTheyAreTyped) {
  Model model = dsl::parse(kSource).model;
  auto text = commit(kSource, shared_styles_operation(model, element(model, "shop.web"),
                                                      "container: fill=#438dd5\ntag \"external\": opacity=50;dashed=1;fill=#999999"));
  EXPECT_NE(text.find("style tag \"external\" { fill \"#999999\" dashed true opacity 50 }"), std::string::npos) << text;
}

TEST(StyleFields, ANewLineAddsABlockAfterTheShownOnes) {
  Model model = dsl::parse(kSource).model;
  auto text = commit(kSource, shared_styles_operation(model, element(model, "shop.db"), "database: fill=#ffcc00"));
  EXPECT_NE(text.find("style relationship { dashed true }\n\nstyle database {\n  fill \"#ffcc00\"\n}\n\nmodel"),
            std::string::npos)
      << text;
  text = commit(kSource, shared_styles_operation(model, element(model, "shop.web"),
                                                 "container: fill=#438dd5\ncontainer tag \"external\": stroke=#000000\n"
                                                 "tag \"external\": fill=#999999;dashed=1"));
  EXPECT_NE(text.find("// blue\n}\n\nstyle container tag \"external\" {\n  stroke \"#000000\"\n}\n\nstyle tag"),
            std::string::npos)
      << text;
}

TEST(StyleFields, DeletingALineRemovesItsBlockAndEditingTheSelectorKeepsTheBody) {
  Model model = dsl::parse(kSource).model;
  auto text = commit(kSource, shared_styles_operation(model, element(model, "shop.web"), "tag \"external\": fill=#999999;dashed=1"));
  EXPECT_EQ(text.find("style container"), std::string::npos);
  text = commit(kSource, shared_styles_operation(model, element(model, "shop.web"),
                                                 "container: fill=#438dd5\ncontainer tag \"external\": fill=#999999;dashed=1"));
  EXPECT_NE(text.find("style container tag \"external\" { fill \"#999999\" dashed true }"), std::string::npos) << text;
}

TEST(StyleFields, RefuseBlocksThatDoNotApply) {
  Model model = dsl::parse(kSource).model;
  auto op = shared_styles_operation(model, element(model, "shop.db"), "container: fill=red");
  ASSERT_FALSE(op.ok());
  EXPECT_NE(op.error().message.find("does not apply to shop.db"), std::string::npos);
  EXPECT_FALSE(shared_styles_operation(model, element(model, "shop.db"), "relationship: dashed=1").ok());
}

TEST(DocumentFields, ShowThemesStylesAndTheDeclaredTheme) {
  Model model = dsl::parse(kSource).model;
  DocumentFields f = document_fields(model, "checkout");
  EXPECT_EQ(f.theme, "brand");
  EXPECT_EQ(f.view_theme, "");
  EXPECT_EQ(f.styles, "container: fill=#438dd5\ntag \"external\": fill=#999999;dashed=1\nrelationship: dashed=1");
  EXPECT_EQ(f.declared, "brand");
  EXPECT_EQ(f.extends, "dark");
  EXPECT_EQ(f.theme_keys, "background=#101418");
  EXPECT_EQ(f.theme_nodes, "person: fill=#08427b");
  EXPECT_FALSE(document_operation(model, "checkout", f)->has_value());
}

TEST(DocumentFields, PickABuiltInThemeForTheDocumentOrTheView) {
  Model model = dsl::parse(kSource).model;
  DocumentFields f = document_fields(model, "checkout");
  f.view_theme = "mono";
  auto text = commit(kSource, document_operation(model, "checkout", f));
  EXPECT_NE(text.find("view checkout of shop {\n  include *\n  theme mono\n}"), std::string::npos) << text;
  f = document_fields(model, "checkout");
  f.theme = "";
  text = commit(kSource, document_operation(model, "checkout", f));
  EXPECT_EQ(text.find("theme brand\n"), std::string::npos) << text;
}

TEST(DocumentFields, AnUnknownNameStartsANewThemeFromTheOldOne) {
  Model model = dsl::parse(kSource).model;
  DocumentFields f = document_fields(model, "checkout");
  f.theme = "night";
  auto text = commit(kSource, document_operation(model, "checkout", f));
  EXPECT_NE(text.find("theme brand extends dark {\n  background \"#101418\"\n  node person { fill \"#08427b\" }\n}\n\n"
                      "theme night extends brand {}\n\ntheme night\n"),
            std::string::npos)
      << text;
}

TEST(DocumentFields, EditTheDeclaredThemesKeysAndNodes) {
  Model model = dsl::parse(kSource).model;
  DocumentFields f = document_fields(model, "checkout");
  f.extends = "mono";
  f.theme_keys = "background=#000000\nedge=#9aa5b1";
  f.theme_nodes = "person: fill=#08427b;text=#ffffff\ncontainer: fill=#1168bd";
  auto text = commit(kSource, document_operation(model, "checkout", f));
  EXPECT_NE(text.find(R"(theme brand extends mono {
  background "#000000"
  node person { fill "#08427b" text "#ffffff" }
  edge "#9aa5b1"
  node container {
    fill "#1168bd"
  }
})"),
            std::string::npos)
      << text;
}

TEST(DocumentFields, EditAllStyleBlocksIncludingRelationshipOnes) {
  Model model = dsl::parse(kSource).model;
  DocumentFields f = document_fields(model, "checkout");
  f.styles = "container: fill=#438dd5\ntag \"external\": fill=#999999;dashed=1\nrelationship: dashed=1;endArrow=open";
  auto text = commit(kSource, document_operation(model, "checkout", f));
  EXPECT_NE(text.find("style relationship { dashed true endArrow open }"), std::string::npos) << text;
}
