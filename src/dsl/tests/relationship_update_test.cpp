// Inspector edits of relationships (op::UpdateRelationship, op::RenameRelationships): the model
// operations and the text they write. Each changed property becomes the smallest edit of its own
// part of the line, and a new name moves the routes that point at the line along with it.

#include <gtest/gtest.h>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const std::string kSource = R"(model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app"
    db  = container "Orders DB"
    web -> db "writes" tag "sql" tech "JDBC" prop "pool" "8"  // hot path
  }
  customer -> shop.web
  calls = customer -> shop.web "browses" style "stroke=#f00"
  customer -> shop.web "pays"
}

view main {
  include *
  route "customer->shop.web#2" via 10, 20
  route "calls" via 30, 40
}
)";

Relationship relationship(const std::string& source, const std::string& id) {
  ParseResult parsed = parse(source);
  const Relationship* r = parsed.model.relationships.find(id);
  EXPECT_TRUE(r) << id;
  return r ? *r : Relationship{};
}

SourceUpdate update(const Operation& op, const std::string& source = kSource) {
  auto r = update_source(source, op);
  EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
  if (!r.ok()) return {};
  for (const auto& d : r->result.diagnostics) EXPECT_NE(d.severity, Severity::Error) << d.message;
  EXPECT_EQ(print(r->result.model), print(r->model));
  return std::move(*r);
}

std::string line_of(const std::string& text, const std::string& part) {
  auto at = text.find(part);
  if (at == std::string::npos) return "";
  auto start = text.rfind('\n', at);
  start = start == std::string::npos ? 0 : start + 1;
  return text.substr(start, text.find('\n', at) - start);
}

/// What the inspector does: the update for `after` (same id) with `name` as the line's name.
SourceUpdate inspect(const Relationship& after, const std::optional<std::string>& name,
                     const std::string& source = kSource, RelationshipId* renamed = nullptr) {
  auto op = editor::update_relationship_operation(parse(source).model, after, name, renamed);
  EXPECT_TRUE(op.ok()) << (op.ok() ? "" : op.error().message);
  if (!op.ok() || !*op) return {};
  return update(**op, source);
}

}  // namespace

TEST(UpdateRelationship, ChangesOnlyTheClausesThatChanged) {
  Relationship r = relationship(kSource, "shop.web->shop.db");
  r.technology = "R2DBC";
  auto u = update(op::UpdateRelationship{r});
  EXPECT_TRUE(u.minimal);
  ASSERT_EQ(u.edits.size(), 1u);
  EXPECT_EQ(u.edits[0].insert, "tech \"R2DBC\"");
  EXPECT_EQ(line_of(u.source, "web -> db"),
            R"(    web -> db "writes" tag "sql" tech "R2DBC" prop "pool" "8"  // hot path)");
  EXPECT_EQ(*u.result.model.relationships.find(r.id), r);
}

TEST(UpdateRelationship, ChangesTheArrowWhereItIsWritten) {
  Relationship r = relationship(kSource, "shop.web->shop.db");
  r.line = LineStyle::Dashed;
  auto u = update(op::UpdateRelationship{r});
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "web --> db"),
            R"(    web --> db "writes" tag "sql" tech "JDBC" prop "pool" "8"  // hot path)");
  r.line = LineStyle::Solid;
  r.bidirectional = true;
  u = update(op::UpdateRelationship{r});
  EXPECT_TRUE(u.minimal);
  EXPECT_NE(u.source.find("    web <-> db \"writes\""), std::string::npos);
}

TEST(UpdateRelationship, AddsALabelAfterTheTargetAndNewClausesAtTheEnd) {
  Relationship r = relationship(kSource, "customer->shop.web");
  r.label = "visits";
  r.technology = "HTTPS";
  r.tags = {"public"};
  auto u = update(op::UpdateRelationship{r});
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "  customer -> shop.web"),
            R"(  customer -> shop.web "visits" tech "HTTPS" tag "public")");

  // With clauses already there, the label still goes right after the target.
  Relationship db = relationship(kSource, "shop.web->shop.db");
  db.label.reset();
  u = update(op::UpdateRelationship{db});
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "web -> db"), R"(    web -> db tag "sql" tech "JDBC" prop "pool" "8"  // hot path)");
  db.label = "reads";
  u = update(op::UpdateRelationship{db}, u.source);
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "web -> db"),
            R"(    web -> db "reads" tag "sql" tech "JDBC" prop "pool" "8"  // hot path)");
}

TEST(UpdateRelationship, RemovesClausesAndRewritesStyleAndProps) {
  Relationship r = relationship(kSource, "calls");
  r.style.reset();
  r.properties.set("retries", "3");
  auto u = update(op::UpdateRelationship{r});
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "calls ="), R"(  calls = customer -> shop.web "browses" prop "retries" "3")");

  Relationship db = relationship(kSource, "shop.web->shop.db");
  db.tags.clear();
  db.properties.set("pool", "16");
  db.style = Style{{"dashed", "1"}};
  u = update(op::UpdateRelationship{db});
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "web -> db"),
            R"(    web -> db "writes" tech "JDBC" prop "pool" "16" style "dashed=1"  // hot path)");
}

TEST(UpdateRelationship, UndoesWithItsInverseAndRefusesToReconnect) {
  Model model = parse(kSource).model;
  Relationship r = *model.relationships.find("calls");
  r.tags = {"a"};
  r.label.reset();
  auto applied = apply_operation(model, op::UpdateRelationship{r});
  ASSERT_TRUE(applied.ok());
  auto undone = apply_operation(applied->model, applied->inverse);
  ASSERT_TRUE(undone.ok());
  EXPECT_EQ(undone->model, model);

  Relationship moved = *model.relationships.find("calls");
  moved.target = {"shop.db", std::nullopt};
  EXPECT_FALSE(apply_operation(model, op::UpdateRelationship{moved}).ok());
  Relationship bad = *model.relationships.find("calls");
  bad.tags = {""};
  EXPECT_FALSE(apply_operation(model, op::UpdateRelationship{bad}).ok());
  bad = *model.relationships.find("calls");
  bad.style = Style{{"a=b", "1"}};
  EXPECT_FALSE(apply_operation(model, op::UpdateRelationship{bad}).ok());
  bad.id = "nothing";
  EXPECT_FALSE(apply_operation(model, op::UpdateRelationship{bad}).ok());
}

TEST(RenameRelationships, RenamesAndMovesRoutesKeepingTheOrder) {
  Model model = parse(kSource).model;
  auto applied = apply_operation(model, op::RenameRelationships{{{"calls", "browse"}}});
  ASSERT_TRUE(applied.ok());
  auto keys = applied->model.relationships.keys();
  EXPECT_EQ(keys, (std::vector<std::string>{"shop.web->shop.db", "customer->shop.web", "browse",
                                            "customer->shop.web#2"}));
  const auto& hints = applied->model.views.find("main")->hints;
  EXPECT_EQ(std::get<RouteHint>(hints[1]).relationship, "browse");
  auto undone = apply_operation(applied->model, applied->inverse);
  ASSERT_TRUE(undone.ok());
  EXPECT_EQ(undone->model, model);
  EXPECT_EQ(undone->model.relationships.keys(), model.relationships.keys());

  // Swaps go together; collisions and unknown ids are refused.
  EXPECT_TRUE(apply_operation(model, op::RenameRelationships{{{"customer->shop.web", "customer->shop.web#2"},
                                                               {"customer->shop.web#2", "customer->shop.web"}}})
                  .ok());
  EXPECT_FALSE(apply_operation(model, op::RenameRelationships{{{"calls", "customer->shop.web"}}}).ok());
  EXPECT_FALSE(apply_operation(model, op::RenameRelationships{{{"nothing", "x"}}}).ok());
  EXPECT_FALSE(apply_operation(model, op::RenameRelationships{{{"calls", ""}}}).ok());
}

TEST(RenameRelationships, RoundTripsThroughJson) {
  for (Operation op : {Operation{op::RenameRelationships{{{"a", "b"}, {"c->d", "e"}}}},
                       Operation{op::UpdateRelationship{relationship(kSource, "calls")}}}) {
    auto back = operation_from_json(to_json(op));
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(*back, op);
  }
}

TEST(InspectRelationship, NamesALineAndItsRouteFollows) {
  // Naming the second of the unnamed `customer -> shop.web` lines: the third keeps its id.
  RelationshipId renamed;
  auto u = inspect(relationship(kSource, "customer->shop.web#2"), "pays", kSource, &renamed);
  EXPECT_EQ(renamed, "pays");
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "\"pays\""), R"(  pays = customer -> shop.web "pays")");
  EXPECT_EQ(line_of(u.source, "via 10"), R"(  route "pays" via 10, 20)");
  EXPECT_TRUE(u.result.model.relationships.contains("customer->shop.web"));
}

TEST(InspectRelationship, NamingTheFirstOfParallelLinesRenumbersTheRest) {
  // `customer -> shop.web` is `customer->shop.web`; the line with "pays" is `#2`, routed.
  auto u = inspect(relationship(kSource, "customer->shop.web"), "visits");
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "visits"), R"(  visits = customer -> shop.web)");
  // "pays" is now the first unnamed line between them, and its route says so.
  EXPECT_EQ(line_of(u.source, "via 10"), R"(  route "customer->shop.web" via 10, 20)");
  EXPECT_EQ(u.result.model.relationships.find("customer->shop.web")->label, "pays");
  EXPECT_TRUE(u.result.diagnostics.empty());
}

TEST(InspectRelationship, ClearingANameDerivesTheIdAgain) {
  auto u = inspect(relationship(kSource, "calls"), std::nullopt);
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "browses"), R"(  customer -> shop.web "browses" style "stroke=#f00")");
  // It becomes `#2`, which pushes "pays" to `#3`; both routes follow.
  EXPECT_EQ(line_of(u.source, "via 30"), R"(  route "customer->shop.web#2" via 30, 40)");
  EXPECT_EQ(line_of(u.source, "via 10"), R"(  route "customer->shop.web#3" via 10, 20)");
  EXPECT_EQ(u.result.model.relationships.find("customer->shop.web#3")->label, "pays");
  EXPECT_TRUE(u.result.diagnostics.empty());
}

TEST(InspectRelationship, RenamesAndEditsInOneGo) {
  Relationship r = relationship(kSource, "calls");
  r.label = "shops";
  r.line = LineStyle::Dashed;
  RelationshipId renamed;
  auto u = inspect(r, "shopping", kSource, &renamed);
  EXPECT_EQ(renamed, "shopping");
  EXPECT_TRUE(u.minimal);
  EXPECT_EQ(line_of(u.source, "shopping ="), R"(  shopping = customer --> shop.web "shops" style "stroke=#f00")");
  EXPECT_EQ(line_of(u.source, "via 30"), R"(  route "shopping" via 30, 40)");
}

TEST(InspectRelationship, RefusesBadOrTakenNamesAndSkipsNoChange) {
  Model model = parse(kSource).model;
  Relationship r = *model.relationships.find("customer->shop.web");
  EXPECT_FALSE(editor::update_relationship_operation(model, r, "2fast").ok());
  EXPECT_FALSE(editor::update_relationship_operation(model, r, "has space").ok());
  EXPECT_FALSE(editor::update_relationship_operation(model, r, "calls").ok());
  auto same = editor::update_relationship_operation(model, r, std::nullopt);
  ASSERT_TRUE(same.ok());
  EXPECT_FALSE(same->has_value());
  Relationship named = *model.relationships.find("calls");
  same = editor::update_relationship_operation(model, named, "calls");
  ASSERT_TRUE(same.ok());
  EXPECT_FALSE(same->has_value());
}

TEST(InspectRelationship, SessionWritesTheRenameAsTextEdits) {
  editor::Session session(kSource);
  Relationship r = *session.model().relationships.find("customer->shop.web");
  RelationshipId renamed;
  auto changed = session.update_relationship(r, "visits", &renamed);
  ASSERT_TRUE(changed.ok());
  ASSERT_TRUE(changed->has_value());
  EXPECT_EQ(renamed, "visits");
  EXPECT_TRUE(session.model().relationships.contains("visits"));
  // The edits turn the old text into the new one, for a text view to apply as one undo step.
  std::string undone = apply_edits(kSource, (*changed)->edits);
  EXPECT_EQ(undone, session.source());
}
