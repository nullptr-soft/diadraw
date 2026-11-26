// Reconnecting a relationship (op::ReconnectRelationship): the model operation, the ids it gives
// relationships, and the text it writes. The moved end is rewritten where it is written; a named
// relationship keeps its id, a derived one (`a->b`) takes its new endpoints' id, and every
// `route` follows the relationship it was drawn for.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;
using namespace dd::dsl;
using editor::End;

namespace {

/// The operation the canvas would send, applied to `source`; checks the text reads back as the
/// model the operation produces.
SourceUpdate reconnect(const std::string& source, const RelationshipId& id, End end, Endpoint to) {
  ParseResult parsed = parse(source);
  auto op = editor::reconnect_operation(parsed, id, end, to);
  EXPECT_TRUE(op.ok()) << (op.ok() ? "" : op.error().message);
  EXPECT_TRUE(op->has_value());
  auto r = update_source(source, **op, &parsed);
  EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
  for (const auto& d : r->result.diagnostics) EXPECT_NE(d.severity, Severity::Error) << d.message;
  EXPECT_EQ(print(r->result.model), print(r->model));
  EXPECT_EQ(r->result.model.relationships.keys(), r->model.relationships.keys());
  return std::move(*r);
}

Endpoint to(const std::string& element, std::optional<std::string> port = {}) { return {element, port}; }

const Relationship& rel(const SourceUpdate& u, const std::string& id) {
  const Relationship* r = u.result.model.relationships.find(id);
  EXPECT_NE(r, nullptr) << id;
  static Relationship none;
  return r ? *r : none;
}

}  // namespace

TEST(Reconnect, RewritesOnlyTheMovedEnd) {
  const std::string src = R"(model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"
  web -> legacy "REST" tech "HTTPS"  // to be migrated
}
)";
  auto r = reconnect(src, "web->legacy", End::Target, to("api"));
  EXPECT_TRUE(r.minimal);
  ASSERT_EQ(r.edits.size(), 1u);
  EXPECT_EQ(r.edits[0].insert, "api");
  EXPECT_NE(r.source.find("  web -> api \"REST\" tech \"HTTPS\"  // to be migrated\n"), std::string::npos);
  EXPECT_EQ(rel(r, "web->api").target.element, "api");
}

TEST(Reconnect, MovesTheSource) {
  const std::string src = "model {\n  a = system\n  b = system\n  c = system\n  a -> b\n}\n";
  auto r = reconnect(src, "a->b", End::Source, to("c"));
  EXPECT_TRUE(r.minimal);
  EXPECT_NE(r.source.find("  c -> b\n"), std::string::npos);
  EXPECT_TRUE(r.result.model.relationships.contains("c->b"));
}

TEST(Reconnect, NamedRelationshipKeepsItsIdAndRoute) {
  const std::string src = R"(model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"
  calls = web -> legacy "REST"
}

view main {
  include *
  route "calls" via 200, 80
}
)";
  auto r = reconnect(src, "calls", End::Target, to("api"));
  EXPECT_TRUE(r.minimal);
  ASSERT_EQ(r.edits.size(), 1u);
  EXPECT_NE(r.source.find("calls = web -> api \"REST\""), std::string::npos);
  EXPECT_NE(r.source.find("route \"calls\" via 200, 80"), std::string::npos);
  EXPECT_EQ(rel(r, "calls").target.element, "api");
}

TEST(Reconnect, RouteFollowsADerivedId) {
  const std::string src = R"(model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"
  web -> legacy
}

view main {
  include *
  route "web->legacy" via 200, 80  // around the cache
}
)";
  auto r = reconnect(src, "web->legacy", End::Target, to("api"));
  EXPECT_TRUE(r.minimal);
  EXPECT_NE(r.source.find("  route \"web->api\" via 200, 80  // around the cache\n"), std::string::npos);
  const View* view = r.result.model.views.find("main");
  ASSERT_NE(view, nullptr);
  ASSERT_EQ(view->hints.size(), 1u);
  EXPECT_EQ(std::get<RouteHint>(view->hints[0]).relationship, "web->api");
}

TEST(Reconnect, ParallelLinesKeepTheirOwnRoutes) {
  // Moving the first of two parallel lines renumbers the second from #2 to the plain id, which
  // was the first one's: each route must stay with the line it was drawn for.
  const std::string src = R"(model {
  order   = container
  billing = container
  ledger  = container
  order -> billing "charge"
  order -> billing "audit"
}

view main {
  include *
  route "order->billing" via 10, 10
  route "order->billing#2" via 20, 20
}
)";
  auto r = reconnect(src, "order->billing", End::Target, to("ledger"));
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(rel(r, "order->ledger").label, "charge");
  EXPECT_EQ(rel(r, "order->billing").label, "audit");
  EXPECT_FALSE(r.result.model.relationships.contains("order->billing#2"));
  EXPECT_NE(r.source.find("route \"order->ledger\" via 10, 10"), std::string::npos);
  EXPECT_NE(r.source.find("route \"order->billing\" via 20, 20"), std::string::npos);
}

TEST(Reconnect, JoiningAPairRenumbersTheLaterLine) {
  const std::string src = R"(model {
  a = system
  b = system
  c = system
  a -> c "first"
  a -> b "second"
}

view main {
  include *
  route "a->b" via 5, 5
}
)";
  auto r = reconnect(src, "a->c", End::Target, to("b"));
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(rel(r, "a->b").label, "first");
  EXPECT_EQ(rel(r, "a->b#2").label, "second");
  EXPECT_NE(r.source.find("route \"a->b#2\" via 5, 5"), std::string::npos);
}

TEST(Reconnect, NamingOneLineLeavesItsNeighboursAlone) {
  const std::string src = R"(model {
  a = system
  b = system
  c = system
  a -> b "one"
  keep = a -> b "two"
  a -> b "three"
}
)";
  auto r = reconnect(src, "keep", End::Target, to("c"));
  EXPECT_TRUE(r.minimal);
  EXPECT_EQ(rel(r, "keep").target.element, "c");
  EXPECT_EQ(rel(r, "a->b").label, "one");
  EXPECT_EQ(rel(r, "a->b#2").label, "three");
}

TEST(Reconnect, WritesTheShortestReferenceFromWhereTheLineIsDeclared) {
  const std::string src = R"(model {
  customer = person
  shop = system {
    web = container
    db  = container
    api = container { port http north }
    web -> db
  }
}
)";
  auto inside = reconnect(src, "shop.web->shop.db", End::Target, to("shop.api"));
  EXPECT_TRUE(inside.minimal);
  EXPECT_NE(inside.source.find("    web -> api\n"), std::string::npos);

  auto outside = reconnect(src, "shop.web->shop.db", End::Target, to("customer"));
  EXPECT_TRUE(outside.minimal);
  EXPECT_NE(outside.source.find("    web -> customer\n"), std::string::npos);

  auto port = reconnect(src, "shop.web->shop.db", End::Target, to("shop.api", "http"));
  EXPECT_TRUE(port.minimal);
  EXPECT_NE(port.source.find("    web -> api.http\n"), std::string::npos);
  EXPECT_TRUE(port.result.model.relationships.contains("shop.web->shop.api.http"));
}

TEST(Reconnect, ReprintsWhenNoReferenceReachesTheElement) {
  // Inside `shop`, `db` means shop.db, so the top-level db cannot be written from there.
  const std::string src = R"(model {
  db = database
  shop = system {
    web = container
    db  = container
    web -> db
  }
}
)";
  auto r = reconnect(src, "shop.web->shop.db", End::Target, to("db"));
  EXPECT_FALSE(r.minimal);
  EXPECT_EQ(rel(r, "shop.web->db").target.element, "db");
}

TEST(Reconnect, UndoesWithItsInverse) {
  const std::string src = R"(model {
  order   = container
  billing = container
  ledger  = container
  order -> billing "charge"
  order -> billing "audit"
}

view main {
  include *
  route "order->billing" via 10, 10
  route "order->billing#2" via 20, 20
}
)";
  ParseResult parsed = parse(src);
  auto op = editor::reconnect_operation(parsed, "order->billing", End::Target, to("ledger"));
  ASSERT_TRUE(op.ok() && op->has_value());
  auto applied = apply_operation(parsed.model, **op);
  ASSERT_TRUE(applied.ok());
  auto undone = apply_operation(applied->model, applied->inverse);
  ASSERT_TRUE(undone.ok());
  EXPECT_EQ(print(undone->model), print(parsed.model));
  EXPECT_EQ(undone->model.relationships.keys(), parsed.model.relationships.keys());
  // And the inverse patches the text back.
  auto forward = update_source(src, **op, &parsed);
  ASSERT_TRUE(forward.ok());
  auto back = update_source(forward->source, applied->inverse);
  ASSERT_TRUE(back.ok());
  EXPECT_TRUE(back->minimal);
  EXPECT_EQ(back->source, src);
}

TEST(Reconnect, RefusesMissingElementsPortsAndClashingIds) {
  Model m;
  m.elements.set("a", make_element("a", "system"));
  m.elements.set("b", make_element("b", "system"));
  m.relationships.set("r", make_relationship("r", {"a", {}}, {"b", {}}));
  m.relationships.set("s", make_relationship("s", {"b", {}}, {"a", {}}));
  EXPECT_FALSE(apply_operation(m, op::ReconnectRelationship{"x", {"a", {}}, {"b", {}}, {}}).ok());
  EXPECT_FALSE(apply_operation(m, op::ReconnectRelationship{"r", {"a", {}}, {"nobody", {}}, {}}).ok());
  EXPECT_FALSE(apply_operation(m, op::ReconnectRelationship{"r", {"a", {}}, {"b", "http"}, {}}).ok());
  EXPECT_FALSE(apply_operation(m, op::ReconnectRelationship{"r", {"a", {}}, {"a", {}}, {{"r", "s"}}}).ok());
  // Swapping two ids at once is fine.
  auto swapped = apply_operation(m, op::ReconnectRelationship{"r", {"a", {}}, {"a", {}}, {{"r", "s"}, {"s", "r"}}});
  ASSERT_TRUE(swapped.ok());
  EXPECT_EQ(swapped->model.relationships.find("s")->target.element, "a");
  EXPECT_EQ(swapped->model.relationships.find("r")->source.element, "b");
  EXPECT_EQ(swapped->model.relationships.keys(), (std::vector<std::string>{"s", "r"}));
}

TEST(Reconnect, RoundTripsThroughJson) {
  Operation op = op::ReconnectRelationship{"a->b", {"a", {}}, {"c", "http"}, {{"a->b", "a->c.http"}}};
  auto back = operation_from_json(to_json(op));
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, op);
}

TEST(Reconnect, SessionReportsTheNewIdAndSkipsANoOp) {
  editor::Session session("model {\n  a = system\n  b = system\n  c = system\n  a -> b\n}\n");
  auto same = session.reconnect("a->b", End::Target, to("b"));
  ASSERT_TRUE(same.ok());
  EXPECT_FALSE(same->has_value());
  auto moved = session.reconnect("a->b", End::Target, to("c"));
  ASSERT_TRUE(moved.ok());
  ASSERT_TRUE(moved->has_value());
  EXPECT_EQ((*moved)->id, "a->c");
  EXPECT_NE(session.source().find("  a -> c\n"), std::string::npos);
  EXPECT_FALSE(session.reconnect("nope", End::Target, to("c")).ok());
}

TEST(Reconnect, RandomReconnectsOverTheCorpusPatchInPlaceAndKeepEveryRoute) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(DIADRAW_CORPUS_DIR))
    if (entry.path().extension() == ".draw") files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  ASSERT_FALSE(files.empty());
  std::mt19937 rng(7);
  auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
  int total = 0, minimal = 0;
  for (const auto& file : files) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    for (int step = 0; step < 20; ++step) {
      ParseResult parsed = parse(source);
      auto rels = parsed.model.relationships.keys();
      auto elements = parsed.model.elements.keys();
      if (rels.empty() || elements.size() < 3) break;
      const RelationshipId id = rels[pick(rels.size())];
      End end = rng() % 2 ? End::Source : End::Target;
      const Relationship& r = *parsed.model.relationships.find(id);
      std::string element = elements[pick(elements.size())];
      if (element == r.source.element || element == r.target.element) continue;
      auto op = editor::reconnect_operation(parsed, id, end, {element, std::nullopt});
      ASSERT_TRUE(op.ok() && op->has_value()) << file << " " << id;
      auto result = update_source(source, **op, &parsed);
      ASSERT_TRUE(result.ok()) << file << " " << id << ": " << result.error().message;
      for (const auto& d : result->result.diagnostics)
        ASSERT_NE(d.severity, Severity::Error) << file << " " << id << ": " << d.message;
      ASSERT_EQ(print(result->result.model), print(result->model)) << file << " " << id;
      // Every route still names a relationship, and the same one it did before.
      const auto& renames = std::get<op::ReconnectRelationship>(**op).renames;
      for (const auto& view : parsed.model.views) {
        const View* after = result->result.model.views.find(view.id);
        ASSERT_NE(after, nullptr);
        ASSERT_EQ(after->hints.size(), view.hints.size());
        for (std::size_t i = 0; i < view.hints.size(); ++i) {
          const auto* was = std::get_if<RouteHint>(&view.hints[i]);
          if (!was) continue;
          auto renamed = renames.find(was->relationship);
          std::string expected = renamed == renames.end() ? was->relationship : renamed->second;
          EXPECT_EQ(std::get<RouteHint>(after->hints[i]).relationship, expected) << file;
          if (parsed.model.relationships.contains(was->relationship)) {
            EXPECT_TRUE(result->result.model.relationships.contains(expected)) << file;
          }
        }
      }
      ++total;
      if (result->minimal) ++minimal;
      source = result->source;
    }
  }
  EXPECT_GT(total, 50);
  EXPECT_GT(static_cast<double>(minimal) / total, 0.9);
}
