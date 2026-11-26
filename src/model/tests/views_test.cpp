// Port of packages/core/src/views.test.ts.

#include "diadraw/model/views.hpp"

#include "helpers.hpp"

using namespace dd;

namespace {

Model shop_model() {
  Model m;
  m.elements.set("customer", make_element("customer", "person"));
  m.elements.set("shop", make_element("shop", "system"));
  m.elements.set("shop.web", make_element("shop.web", "container", {}, "shop"));
  m.elements.set("shop.db", make_element("shop.db", "database", {}, "shop"));
  m.relationships.set("r1", make_relationship("r1", {"customer", {}}, {"shop.web", {}}));
  m.relationships.set("r2", make_relationship("r2", {"shop.web", {}}, {"shop.db", {}}));
  m.views.set("all", make_view("all"));
  m.views.set("checkout", make_view("checkout", "shop"));
  View no_db = make_view("noDb");
  no_db.exclude = {"shop.db"};
  m.views.set("noDb", no_db);
  return m;
}

std::vector<std::string> element_ids(const ResolvedView& r) {
  std::vector<std::string> out;
  for (const auto& e : r.elements) out.push_back(e.id);
  return out;
}

std::vector<std::string> relationship_ids(const ResolvedView& r) {
  std::vector<std::string> out;
  for (const auto& rel : r.relationships) out.push_back(rel.id);
  return out;
}

ResolvedView resolve(const Model& m, const std::string& id) {
  return dd::test::ok(resolve_view(m, id));
}

using Ids = std::vector<std::string>;

}  // namespace

TEST(ResolveView, IncludesEverythingForAnUnscopedInclude) {
  auto r = resolve(shop_model(), "all");
  EXPECT_TRUE((element_ids(r) == Ids{"customer", "shop", "shop.web", "shop.db"}));
  EXPECT_TRUE(r.relationships.size() == 2);
}

TEST(ResolveView, LimitsToTheChildrenOfTheScope) {
  auto r = resolve(shop_model(), "checkout");
  EXPECT_TRUE((element_ids(r) == Ids{"shop.web", "shop.db"}));
  EXPECT_TRUE((relationship_ids(r) == Ids{"r2"}));
}

TEST(ResolveView, DropsExcludedElementsAndTheirRelationships) {
  auto r = resolve(shop_model(), "noDb");
  EXPECT_TRUE((element_ids(r) == Ids{"customer", "shop", "shop.web"}));
  EXPECT_TRUE((relationship_ids(r) == Ids{"r1"}));
}

TEST(ResolveView, ReportsAnUnknownView) {
  auto r = resolve_view(shop_model(), "missing");
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(dd::test::contains(r.error().message, "not found"));
}

TEST(ImpliedRelationships, LiftsRelationshipsOfHiddenChildrenToTheirVisibleAncestor) {
  Model m = shop_model();
  View ctx = make_view("context");
  ctx.include = {"customer", "shop"};
  m.views = {};
  m.views.set("context", ctx);
  auto r = resolve(m, "context");
  ASSERT_TRUE(r.relationships.size() == 1);
  EXPECT_TRUE(r.relationships[0].id == "r1");
  EXPECT_TRUE(r.relationships[0].source.element == "customer");
  EXPECT_TRUE(r.relationships[0].target.element == "shop");
  EXPECT_TRUE(r.relationships[0].implied);
}

TEST(ImpliedRelationships, DropsRelationshipsThatCollapseIntoOneElement) {
  Model m = shop_model();
  View s = make_view("s");
  s.include = {"shop"};
  m.views = {};
  m.views.set("s", s);
  EXPECT_TRUE(resolve(m, "s").relationships.empty());
}

TEST(Views, ScopedViewsShowDirectChildrenOnlyAndLiftDeeperRelationships) {
  Model m;
  m.elements.set("sys", make_element("sys", "system"));
  m.elements.set("sys.api", make_element("sys.api", "container", {}, "sys"));
  m.elements.set("sys.api.c", make_element("sys.api.c", "component", {}, "sys.api"));
  m.elements.set("sys.db", make_element("sys.db", "container", {}, "sys"));
  m.relationships.set("r", make_relationship("r", {"sys.api.c", {}}, {"sys.db", {}}));
  m.views.set("v", make_view("v", "sys"));
  auto r = resolve(m, "v");
  EXPECT_TRUE((element_ids(r) == Ids{"sys.api", "sys.db"}));
  ASSERT_TRUE(r.relationships.size() == 1);
  EXPECT_TRUE(r.relationships[0].source.element == "sys.api");
  EXPECT_TRUE(r.relationships[0].implied);
}

TEST(Views, GroupsAreListedOutermostFirstOnlyWhenUsed) {
  Model m;
  m.groups.set("inner", Group{"inner", "Inner", "outer"});
  m.groups.set("outer", Group{"outer", "Outer", {}});
  m.groups.set("unused", Group{"unused", "Unused", {}});
  auto e = make_element("a", "process");
  e.group = "inner";
  m.elements.set("a", e);
  m.views.set("v", make_view("v"));
  auto r = resolve(m, "v");
  ASSERT_TRUE(r.groups.size() == 2);
  EXPECT_TRUE(r.groups[0].id == "outer");
  EXPECT_TRUE(r.groups[1].id == "inner");
}

TEST(Views, WithDefaultViewAddsAViewOfEverythingOnlyWhenThereIsNone) {
  Model m;
  m.elements.set("a", make_element("a", "process"));
  auto with = with_default_view(m);
  EXPECT_TRUE(with.views.contains("default"));
  EXPECT_TRUE(with_default_view(with).views.size() == 1);
}

TEST(Views, WithDefaultViewOfAModelGivenUpKeepsItsViewsAndAddsOneOnlyWhenThereIsNone) {
  Model m;
  m.elements.set("a", make_element("a", "process"));
  Model with = with_default_view(Model(m));
  EXPECT_TRUE(with.views.keys() == Ids{"default"});
  Model own = with;
  own.views.set("mine", make_view("mine"));
  Model kept = with_default_view(std::move(own));
  EXPECT_TRUE((kept.views.keys() == Ids{"default", "mine"}));
  EXPECT_TRUE(kept.elements.contains("a"));
}
