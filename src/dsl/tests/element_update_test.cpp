// Inspector edits (op::UpdateElement): the model operation and the text it writes. Each changed
// property becomes the smallest edit of its own clause, so comments, the order clauses were
// written in and the element's body stay as they were.

#include <gtest/gtest.h>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const std::string kSource = R"(model {
  customer = person "Customer"   // the buyer
  shop = system "Shop" {
    web = container "Web app" shape rect tech "React" tag "edge" prop "owner" "Ann"  // storefront
    db  = container tech "Postgres" tag "a" tag "b" tag "c" style "fill=#eee"
  }
  customer -> shop.web "places order"
}
)";

Element element(const std::string& source, const std::string& id) {
  return *parse(source).model.elements.find(id);
}

SourceUpdate update(const Element& e, const std::string& source = kSource) {
  auto r = update_source(source, op::UpdateElement{e});
  EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
  for (const auto& d : r->result.diagnostics) EXPECT_NE(d.severity, Severity::Error) << d.message;
  EXPECT_EQ(print(r->result.model), print(r->model));
  EXPECT_EQ(*r->result.model.elements.find(e.id), e);
  return std::move(*r);
}

std::string line_of(const std::string& text, const std::string& part) {
  auto at = text.find(part);
  if (at == std::string::npos) return "";
  auto start = text.rfind('\n', at);
  start = start == std::string::npos ? 0 : start + 1;
  return text.substr(start, text.find('\n', at) - start);
}

}  // namespace

TEST(UpdateElement, ChangesOnlyTheClausesThatChanged) {
  Element e = element(kSource, "shop.web");
  e.technology = "Vue";
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  ASSERT_EQ(r.edits.size(), 1u);
  EXPECT_EQ(r.edits[0].insert, "tech \"Vue\"");
  EXPECT_EQ(line_of(r.source, "web ="),
            R"(    web = container "Web app" shape rect tech "Vue" tag "edge" prop "owner" "Ann"  // storefront)");
}

TEST(UpdateElement, AppendsNewClausesAfterTheLastOneAndKeepsTheComment) {
  Element e = element(kSource, "customer");
  e.technology = "Browser";
  e.description = "Buys things";
  e.tags = {"external"};
  e.properties.set("team", "Growth");
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(line_of(r.source, "customer ="),
            R"(  customer = person "Customer" tech "Browser" description "Buys things" tag "external" prop "team" "Growth"   // the buyer)");
}

TEST(UpdateElement, RemovesClausesWithTheSpaceBeforeThem) {
  Element e = element(kSource, "shop.web");
  e.shape.reset();
  e.properties = {};
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(line_of(r.source, "web ="), R"(    web = container "Web app" tech "React" tag "edge"  // storefront)");
}

TEST(UpdateElement, KeepsTagsThatStayAndAddsNewOnesAtTheEnd) {
  Element e = element(kSource, "shop.db");
  e.tags = {"a", "c", "d"};
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(line_of(r.source, "db  ="), R"(    db  = container tech "Postgres" tag "a" tag "c" style "fill=#eee" tag "d")");
}

TEST(UpdateElement, RewritesKindNameAndStyleInPlace) {
  Element e = element(kSource, "shop.db");
  e.kind = "database";
  e.name = "Orders";
  e.style = Style{{"fill", "#dae8fc"}, {"stroke", "#6c8ebf"}};
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(line_of(r.source, "db  ="),
            R"(    db  = database "Orders" tech "Postgres" tag "a" tag "b" tag "c" style "fill=#dae8fc;stroke=#6c8ebf")");
}

TEST(UpdateElement, ChangesAPropertyValueInPlace) {
  Element e = element(kSource, "shop.web");
  e.properties.set("owner", "Bob");
  e.properties.set("tier", "1");
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(line_of(r.source, "web ="),
            R"(    web = container "Web app" shape rect tech "React" tag "edge" prop "owner" "Bob" prop "tier" "1"  // storefront)");
}

TEST(UpdateElement, KeepsTheBodyOfAContainer) {
  Element e = element(kSource, "shop");
  e.technology = "Kubernetes";
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_NE(r.source.find("  shop = system \"Shop\" tech \"Kubernetes\" {\n    web = container"), std::string::npos);
}

TEST(UpdateElement, QuotesWhatNeedsQuoting) {
  Element e = element(kSource, "customer");
  e.description = "Says \"hi\"\nand leaves";
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(element(r.source, "customer").description, e.description);
}

TEST(UpdateElement, UndoesWithItsInverse) {
  Model model = parse(kSource).model;
  Element e = *model.elements.find("shop.db");
  e.tags.clear();
  e.style.reset();
  auto applied = apply_operation(model, op::UpdateElement{e});
  ASSERT_TRUE(applied.ok());
  auto undone = apply_operation(applied->model, applied->inverse);
  ASSERT_TRUE(undone.ok());
  EXPECT_EQ(undone->model, model);
}

TEST(UpdateElement, RefusesToMoveAnElementOrWriteWhatCannotBeParsed) {
  Model model = parse(kSource).model;
  Element moved = *model.elements.find("shop.db");
  moved.parent.reset();
  EXPECT_FALSE(apply_operation(model, op::UpdateElement{moved}).ok());
  Element bad = *model.elements.find("shop.db");
  bad.kind = "not a kind";
  EXPECT_FALSE(apply_operation(model, op::UpdateElement{bad}).ok());
  bad = *model.elements.find("shop.db");
  bad.shape = "rect;x";
  EXPECT_FALSE(apply_operation(model, op::UpdateElement{bad}).ok());
  bad = *model.elements.find("shop.db");
  bad.style = Style{{"fill;x", "1"}};
  EXPECT_FALSE(apply_operation(model, op::UpdateElement{bad}).ok());
  Element missing = make_element("nobody", "person");
  EXPECT_FALSE(apply_operation(model, op::UpdateElement{missing}).ok());
}

TEST(UpdateElement, RoundTripsThroughJson) {
  Operation op = op::UpdateElement{element(kSource, "shop.web")};
  auto back = operation_from_json(to_json(op));
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, op);
}

TEST(UpdateElement, SessionSkipsAnEditThatChangesNothing) {
  editor::Session session(kSource);
  Element e = *session.model().elements.find("shop.web");
  auto same = session.update_element(e);
  ASSERT_TRUE(same.ok());
  EXPECT_FALSE(same->has_value());
  e.shape = "hexagon";
  auto changed = session.update_element(e);
  ASSERT_TRUE(changed.ok());
  ASSERT_TRUE(changed->has_value());
  EXPECT_NE(session.source().find("shape hexagon tech \"React\""), std::string::npos);
}

TEST(UpdateElement, WritesADescriptionThatSpansLinesAsABlockString) {
  Element e = element(kSource, "shop.web");
  e.description = "Serves the shop.\n\nCaches \"pages\" in C:\\cache";
  auto r = update(e);
  EXPECT_TRUE(r.minimal);
  EXPECT_NE(r.source.find(R"(tag "edge" prop "owner" "Ann" description """
      Serves the shop.

      Caches "pages" in C:\\cache
    """  // storefront)"),
            std::string::npos)
      << r.source;

  // Changing it again rewrites the block in place.
  Element again = element(r.source, "shop.web");
  again.description = "One line";
  auto r2 = update(again, r.source);
  EXPECT_EQ(line_of(r2.source, "web ="),
            R"(    web = container "Web app" shape rect tech "React" tag "edge" prop "owner" "Ann" description "One line"  // storefront)");
}

TEST(UpdateElement, AddsAnElementWithABlockDescriptionInsideABody) {
  Element e;
  e.id = "shop.queue";
  e.parent = "shop";
  e.kind = "container";
  e.name = "Queue";
  e.description = "Orders waiting.\nRetried twice.";
  auto r = update_source(kSource, op::AddElement{e});
  ASSERT_TRUE(r.ok());
  EXPECT_NE(r->source.find(R"(    queue = container "Queue" description """
      Orders waiting.
      Retried twice.
    """
)"),
            std::string::npos)
      << r->source;
  EXPECT_EQ(parse(r->source).model.elements.find("shop.queue")->description, e.description);
}
