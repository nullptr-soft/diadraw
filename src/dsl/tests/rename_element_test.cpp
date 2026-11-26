// Renaming an element (op::ChangeElementId): the model operation, the ids that follow it, and the
// text it writes. The declaration and every reference are rewritten where they are written, so
// comments and formatting stay; descendants, derived relationship ids, `route`s and group ids
// follow; and the session keeps the drawing where it was.

#include <gtest/gtest.h>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/model/json.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

/// The operation an inspector or a text editor would send, applied to `source`; checks the text
/// reads back as the model the operation produces.
SourceUpdate rename(const std::string& source, const ElementId& id, std::string_view to) {
  ParseResult parsed = parse(source);
  auto op = test::ok(editor::change_element_id_operation(parsed, id, to));
  if (!op) test::stop("nothing to rename");
  auto r = test::ok(update_source(source, *op, &parsed));
  for (const auto& d : r.result.diagnostics) EXPECT_NE(d.severity, Severity::Error) << d.message;
  EXPECT_EQ(print(r.result.model), print(r.model));
  EXPECT_EQ(r.result.model.relationships.keys(), r.model.relationships.keys());
  EXPECT_EQ(r.result.model.groups.keys(), r.model.groups.keys());
  return r;
}

const char* const kShop = R"(model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"  // the storefront
    db  = container "Orders DB" shape cylinder
    web -> db "writes"
    group "Edge" {
      cdn = container "CDN"
    }
  }
  customer -> shop.web "places order"
  orders = shop.web -> shop.db "orders"
}

view checkout of shop {
  include *
  layout fixed
  pin web at 10, 20
  route "shop.web->shop.db" via 50, 60
  note "Main entry" right of web
}

view all {
  include customer shop.web
  exclude shop.cdn
  align customer shop.web
}

view login {
  include customer shop.web
  layout sequence
  sequence {
    customer -> shop.web "opens"
    activate shop.web
    note over customer shop.web "hi"
  }
}
)";

}  // namespace

TEST(RenameElement, RewritesTheDeclarationAndEveryReference) {
  auto r = rename(kShop, "shop.web", "store");
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(test::contains(r.source, "    store = container \"Web app\" tech \"React\"  // the storefront\n"));
  EXPECT_TRUE(test::contains(r.source, "    store -> db \"writes\"\n"));
  EXPECT_TRUE(test::contains(r.source, "  customer -> shop.store \"places order\"\n"));
  EXPECT_TRUE(test::contains(r.source, "  orders = shop.store -> shop.db \"orders\"\n"));
  EXPECT_TRUE(test::contains(r.source, "  pin store at 10, 20\n"));
  EXPECT_TRUE(test::contains(r.source, "  route \"shop.store->shop.db\" via 50, 60\n"));
  EXPECT_TRUE(test::contains(r.source, "  note \"Main entry\" right of store\n"));
  EXPECT_TRUE(test::contains(r.source, "  include customer shop.store\n  exclude shop.cdn\n  align customer shop.store\n"));
  EXPECT_TRUE(test::contains(r.source, "    customer -> shop.store \"opens\"\n    activate shop.store\n"));
  EXPECT_TRUE(test::contains(r.source, "    note over customer shop.store \"hi\"\n"));
  EXPECT_FALSE(test::contains(r.source, "web"));
  // The derived id moved; the named one stayed.
  EXPECT_TRUE(r.model.relationships.contains("shop.store->shop.db"));
  EXPECT_TRUE(r.model.relationships.contains("customer->shop.store"));
  EXPECT_TRUE(r.model.relationships.contains("orders"));
  // The element keeps its place in the model's order.
  EXPECT_EQ(r.model.elements.keys(),
            (std::vector<std::string>{"customer", "shop", "shop.store", "shop.db", "shop.cdn"}));
}

TEST(RenameElement, RenamingAParentMovesItsDescendantsAndTheirGroups) {
  auto r = rename(kShop, "shop", "store");
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(test::contains(r.source, "  store = system \"Shop\" {\n"));
  EXPECT_EQ(r.model.elements.find("store")->name, "Shop");
  // Written from inside the element, references do not name it and stay.
  EXPECT_TRUE(test::contains(r.source, "    web -> db \"writes\"\n"));
  EXPECT_TRUE(test::contains(r.source, "  customer -> store.web \"places order\"\n"));
  EXPECT_TRUE(test::contains(r.source, "view checkout of store {\n"));
  EXPECT_TRUE(test::contains(r.source, "  pin web at 10, 20\n"));
  EXPECT_TRUE(test::contains(r.source, "  route \"store.web->store.db\" via 50, 60\n"));
  EXPECT_TRUE(test::contains(r.source, "  exclude store.cdn\n"));
  EXPECT_EQ(r.model.elements.keys(),
            (std::vector<std::string>{"customer", "store", "store.web", "store.db", "store.cdn"}));
  EXPECT_TRUE(r.model.groups.contains("store/Edge"));
  EXPECT_EQ(r.model.elements.find("store.cdn")->group, std::optional<GroupId>("store/Edge"));
  EXPECT_EQ(r.model.elements.find("store.web")->parent, std::optional<ElementId>("store"));
  EXPECT_EQ(r.model.views.find("checkout")->scope, std::optional<ElementId>("store"));
}

TEST(RenameElement, PortsAndParallelLinesKeepTheirIds) {
  const std::string src = R"(model {
  a = process {
    port out east
  }
  b = process
  a.out -> b
  a -> b "first"
  a -> b "second"
  b -> a
}

view v {
  include *
  layout fixed
  route "a->b#2" via 5, 5
}
)";
  auto r = rename(src, "a", "start");
  EXPECT_TRUE(r.minimal) << r.source;
  // Without a label the element shows its id, so its name follows; a label stays.
  EXPECT_EQ(r.model.elements.find("start")->name, "start");
  EXPECT_TRUE(test::contains(r.source, "  start = process {\n    port out east\n  }\n"));
  EXPECT_TRUE(test::contains(r.source, "  start.out -> b\n  start -> b \"first\"\n  start -> b \"second\"\n  b -> start\n"));
  EXPECT_TRUE(test::contains(r.source, "route \"start->b#2\" via 5, 5"));
  EXPECT_EQ(r.model.relationships.keys(),
            (std::vector<std::string>{"start.out->b", "start->b", "start->b#2", "b->start"}));
}

TEST(RenameElement, RefusesBadAndTakenNames) {
  ParseResult parsed = parse(kShop);
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "shop.web", "db").ok());
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "shop.web", "2nd").ok());
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "shop.web", "a.b").ok());
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "shop.web", "").ok());
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "nope", "x").ok());
  EXPECT_FALSE(editor::change_element_id_operation(parsed, "shop", "customer").ok());
  // A name a top-level element has is free inside another element.
  EXPECT_TRUE(editor::change_element_id_operation(parsed, "shop.web", "customer").ok());
  // The same name changes nothing.
  EXPECT_FALSE(test::ok(editor::change_element_id_operation(parsed, "shop.web", "web")).has_value());
}

TEST(RenameElement, TheModelOperationChecksAndUndoes) {
  Model m = parse(kShop).model;
  EXPECT_TRUE(test::contains(test::apply_error(m, op::ChangeElementId{"nope", "x", {}, {}, {}}), "not found"));
  EXPECT_TRUE(test::contains(test::apply_error(m, op::ChangeElementId{"shop.web", "db", {}, {}, {}}), "keep the place"));
  EXPECT_TRUE(test::contains(test::apply_error(m, op::ChangeElementId{"shop.web", "shop.db", {}, {}, {}}), "already exists"));
  EXPECT_TRUE(test::contains(test::apply_error(m, op::ChangeElementId{"shop.web", "shop.1x", {}, {}, {}}), "not an element name"));
  EXPECT_TRUE(test::contains(test::apply_error(m, op::ChangeElementId{"shop", "store", {{"nope", "x"}}, {}, {}}), "not found"));
  EXPECT_TRUE(
      test::contains(test::apply_error(m, op::ChangeElementId{"shop", "store", {{"orders", "customer->shop.web"}}, {}, {}}),
                     "already exists"));

  ParseResult parsed = parse(kShop);
  auto op = test::ok(editor::change_element_id_operation(parsed, "shop", "store"));
  ASSERT_TRUE(op.has_value());
  auto [renamed, inverse] = test::apply_ok(m, *op);
  EXPECT_EQ(std::get<op::ChangeElementId>(inverse).id, "store");
  auto back = test::apply_ok(renamed, inverse);
  EXPECT_EQ(print(back.model), print(m));
  EXPECT_TRUE(back.model == m);
  // The operation reads back from JSON as it was written.
  auto json = to_json(*op);
  EXPECT_TRUE(test::ok(operation_from_json(json)) == *op);
}

TEST(RenameElement, ANameThatWouldCaptureAnotherReferenceIsRefused) {
  // Inside `shop`, `db` names the top-level `db`; a `shop.db` would capture that reference.
  const std::string src = R"(model {
  db = container
  shop = system {
    web = container
    web -> db
  }
}
)";
  editor::Session session(src);
  auto r = session.rename_element("shop.web", "db");
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(test::contains(r.error().message, "cannot be used"));
  EXPECT_EQ(session.source(), src);
  // Elsewhere the name is free.
  auto other = test::ok(session.rename_element("shop.web", "store"));
  ASSERT_TRUE(other.has_value());
  EXPECT_TRUE(test::contains(session.source(), "    store = container\n    store -> db\n"));
}

TEST(RenameElement, TheSessionKeepsTheDrawingWhereItWas) {
  editor::Session session(kShop);
  auto before = test::ok(session.render("all"));
  ASSERT_TRUE(before.geometry.nodes.contains("shop.web"));
  Box was = *before.geometry.nodes.find("shop.web");
  auto renamed = test::ok(session.rename_element("shop.web", "store"));
  ASSERT_TRUE(renamed.has_value());
  EXPECT_EQ(renamed->id, "shop.store");
  EXPECT_FALSE(renamed->update.edits.empty());
  auto after = test::ok(session.render("all"));
  ASSERT_TRUE(after.geometry.nodes.contains("shop.store"));
  EXPECT_EQ(after.geometry.nodes.find("shop.store")->x, was.x);
  EXPECT_EQ(after.geometry.nodes.find("shop.store")->y, was.y);
  EXPECT_FALSE(test::ok(session.rename_element("shop.store", "store")).has_value());
}

TEST(RenameElement, GroupIdsAreNumberedAsTheTextNumbersThem) {
  // A top-level group "a" holds a group "G" (`a/G`) before element `a`'s own group "G" (`a/G#2`).
  const std::string src = R"(model {
  group "a" {
    group "G" {
      x = container
    }
  }
  a = system {
    group "G" {
      y = container
    }
  }
}
)";
  auto r = rename(src, "a", "z");
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(r.model.groups.keys(), (std::vector<std::string>{"a", "a/G", "z/G"}));
  EXPECT_EQ(r.model.elements.find("z.y")->group, std::optional<GroupId>("z/G"));

  // The other way: the new id is taken by a group outside, so the renamed one becomes `b/G#2`.
  const std::string taken = R"(model {
  group "b" {
    group "G" {
      x = container
    }
  }
  a = system {
    group "G" {
      y = container
    }
  }
}
)";
  auto t = rename(taken, "a", "b");
  EXPECT_TRUE(t.minimal);
  EXPECT_EQ(t.model.groups.keys(), (std::vector<std::string>{"b", "b/G", "b/G#2"}));
}

TEST(RenameElement, KeywordsAreNamesToo) {
  for (std::string_view name : {"view", "model", "port", "group", "include", "system", "of", "tech"}) {
    editor::Session session(kShop);
    auto r = session.rename_element("shop.web", name);
    ASSERT_TRUE(r.ok()) << name << ": " << r.error().message;
    ASSERT_TRUE(r->has_value()) << name;
    EXPECT_TRUE((*r)->update.minimal) << name;
    EXPECT_TRUE(session.model().elements.contains("shop." + std::string(name))) << name;
    EXPECT_TRUE(test::contains(session.source(), "// the storefront")) << name;
  }
}

TEST(RenameElement, ChildrenFollowWhereverTheModelListsThem) {
  // A model read from JSON may list a child before its parent.
  Model m;
  m.elements.set("p.c", make_element("p.c", "container", {}, ElementId("p")));
  m.elements.set("p", make_element("p", "system"));
  auto [renamed, inverse] = test::apply_ok(m, op::ChangeElementId{"p", "q", {}, {}, {}});
  ASSERT_TRUE(renamed.elements.contains("q.c"));
  EXPECT_EQ(renamed.elements.find("q.c")->parent, std::optional<ElementId>("q"));
  EXPECT_FALSE(renamed.elements.contains("p"));
}

TEST(RenameElement, AParentCycleFromJsonDoesNotHang) {
  Model m;
  m.elements.set("p", make_element("p", "system", {}, ElementId("q")));
  m.elements.set("q", make_element("q", "system", {}, ElementId("p")));
  auto [renamed, inverse] = test::apply_ok(m, op::ChangeElementId{"p", "x", {}, {}, {}});
  EXPECT_TRUE(renamed.elements.contains("x"));
  EXPECT_EQ(renamed.elements.find("q")->parent, std::optional<ElementId>("x"));
  auto removed = test::apply_ok(m, op::RemoveElement{"p"});
  EXPECT_TRUE(removed.model.elements.empty());
}

TEST(RenameElement, TheOperationsJsonListsRenamesAsFromAndTo) {
  op::ChangeElementId o{"a", "b", {{"a->c", "b->c"}}, {{"a/G", "b/G"}}, std::string("b")};
  Json json = to_json(Operation{o});
  ASSERT_TRUE(json.get("relationships") && json.get("relationships")->is_array());
  const Json& first = json.get("relationships")->as_array().at(0);
  EXPECT_EQ(first.get("from")->as_string(), "a->c");
  EXPECT_EQ(first.get("to")->as_string(), "b->c");
  EXPECT_TRUE(test::ok(operation_from_json(json)) == Operation{o});
  // Malformed: an object where a list belongs, and a rename without `to`.
  auto object = Json::parse(R"({"type":"changeElementId","id":"a","to":"b","groups":{"a/G":"b/G"}})");
  EXPECT_FALSE(operation_from_json(test::ok(object)).ok());
  auto missing = Json::parse(R"({"type":"changeElementId","id":"a","to":"b","relationships":[{"from":"a->c"}]})");
  EXPECT_FALSE(operation_from_json(test::ok(missing)).ok());
}

TEST(RenameElement, ChainedGroupRenamesKeepEachGroupsLayout) {
  // `a`'s group comes first, so renaming `a` to `z` makes it `z/G` and pushes the outer two on.
  const std::string src = R"(model {
  a = system {
    group "G" {
      p = container
    }
  }
  group "z" {
    group "G" {
      q = container
    }
    group "G" {
      r = container
    }
  }
}

view all {
  include *
}
)";
  editor::Session session(src);
  auto before = test::ok(session.render("all"));
  auto box = [](const editor::ViewRender& r, const std::string& group) {
    const Box* found = r.geometry.nodes.find(group_node_id(group));
    EXPECT_NE(found, nullptr) << group;
    return found ? *found : Box{};
  };
  Box inner = box(before, "a/G"), outer1 = box(before, "z/G"), outer2 = box(before, "z/G#2");
  auto renamed = test::ok(session.rename_element("a", "z"));
  ASSERT_TRUE(renamed.has_value());
  EXPECT_EQ(renamed->operation.groups,
            (std::map<GroupId, GroupId>{{"a/G", "z/G"}, {"z/G", "z/G#2"}, {"z/G#2", "z/G#3"}}));
  auto after = test::ok(session.render("all"));
  EXPECT_FALSE(after.geometry.nodes.contains(group_node_id("a/G")));
  EXPECT_EQ(box(after, "z/G").x, inner.x);
  EXPECT_EQ(box(after, "z/G").y, inner.y);
  EXPECT_EQ(box(after, "z/G#2").x, outer1.x);
  EXPECT_EQ(box(after, "z/G#3").x, outer2.x);
}
