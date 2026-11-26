// Port of packages/dsl/src/patch.test.ts (the example-based part; the property test is in
// patch_property_test.cpp).

#include "diadraw/dsl/patch.hpp"

#include "diadraw/dsl/print.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const std::string kSource = R"(// Shop, C4 container level
model {
  customer = person "Customer"   // the buyer
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  /* traffic */
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin db below web    // written by a canvas drag
}

view everything { include customer shop }
)";

SourceUpdate update(const Operation& op, const std::string& source = kSource) {
  auto r = update_source(source, op);
  if (!r.ok()) dd::test::stop(r.error().message);
  for (const auto& d : r->result.diagnostics) EXPECT_TRUE(d.severity != Severity::Error) << d.message;
  EXPECT_TRUE(print(r->result.model) == print(r->model));
  return std::move(*r);
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(UpdateSource, RenamesByReplacingOnlyTheLabel) {
  auto r = update(op::RenameElement{"shop.web", "Storefront"});
  EXPECT_TRUE(r.minimal);
  auto at = static_cast<std::uint32_t>(kSource.find("\"Web app\""));
  EXPECT_TRUE((r.edits == std::vector<TextEdit>{{at, at + 9, "\"Storefront\""}}));
}

TEST(UpdateSource, AddsALabelWhenRenamingAnElementWithoutOne) {
  auto r = update(op::RenameElement{"a", "Alpha"}, "model {\n  a = process  // keep\n}\n");
  EXPECT_TRUE((r.source == "model {\n  a = process \"Alpha\"  // keep\n}\n"));
}

TEST(UpdateSource, AddsTopLevelAndNestedElementsAtTheRightIndentation) {
  auto r = update(op::AddElement{make_element("mail", "system", "Mail")});
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "  shop.web -> shop.db \"writes\"\n  mail = system \"Mail\"\n}"));
  r = update(op::AddElement{make_element("shop.cache", "container", "Cache", "shop")});
  EXPECT_TRUE(has(r.source,
            "    db  = container \"Orders DB\" tech \"Postgres\" shape cylinder\n    cache = container \"Cache\"\n  }"));
}

TEST(UpdateSource, OpensABodyWhenAddingTheFirstChild) {
  auto r = update(op::AddElement{make_element("customer.app", "component", "App", "customer")});
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE((has(r.source, "customer = person \"Customer\" {\n    app = component \"App\"\n  }   // the buyer")));
}

TEST(UpdateSource, RemovesAnElementWithItsRelationshipsAndHintsKeepingComments) {
  auto r = update(op::RemoveElement{"shop.db"});
  EXPECT_TRUE(r.minimal);
  EXPECT_FALSE(has(r.source, "Orders DB"));
  EXPECT_FALSE(has(r.source, "writes"));
  EXPECT_FALSE(has(r.source, "pin db"));
  EXPECT_TRUE(has(r.source, "// the buyer"));
  EXPECT_TRUE(has(r.source, "/* traffic */"));
  EXPECT_TRUE(has(r.source, "// Shop, C4 container level"));
}

TEST(UpdateSource, RemovesAViewScopedToARemovedElementAndDropsSelectors) {
  auto r = update(op::RemoveElement{"shop"});
  EXPECT_TRUE(r.minimal);
  EXPECT_FALSE(has(r.source, "view checkout"));
  EXPECT_TRUE((has(r.source, "view everything { include customer }")));
}

TEST(UpdateSource, AddsAndRemovesRelationships) {
  auto rel = make_relationship(relationship_id({"customer", {}}, {"shop.db", {}}), {"customer", {}},
                               {"shop.db", {}}, "reads");
  auto r = update(op::AddRelationship{rel});
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "  customer -> shop.db \"reads\"\n}"));
  r = update(op::RemoveRelationship{"shop.web->shop.db"});
  EXPECT_FALSE(has(r.source, "writes"));
  EXPECT_TRUE(r.edits.size() == 1);
}

TEST(UpdateSource, SetsChangesAndClearsRelationshipLabels) {
  auto r = update(op::SetRelationshipLabel{"customer->shop.web", "buys"});
  EXPECT_TRUE(has(r.source, "customer -> shop.web \"buys\""));
  r = update(op::SetRelationshipLabel{"customer->shop.web", std::nullopt});
  EXPECT_TRUE(has(r.source, "  customer -> shop.web\n"));
}

TEST(UpdateSource, WritesHintsAndLayoutIntoTheView) {
  auto r = update(op::AddHint{"checkout", AlignHint{{"shop.web", "shop.db"}, Axis::Vertical}, {}});
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "  pin db below web    // written by a canvas drag\n  align shop.web shop.db vertically\n}"));
  r = update(op::RemoveHint{"checkout", 0});
  EXPECT_FALSE(has(r.source, "pin db"));
  r = update(op::SetLayout{"checkout", {"layered", Direction::Down, {}}});
  EXPECT_TRUE(has(r.source, "  layout layered direction down\n"));
  r = update(op::SetLayout{"everything", {"layered", Direction::Up, {}}});
  EXPECT_TRUE((has(r.source, "view everything { include customer shop layout layered direction up }")));
}

TEST(UpdateSource, AddsAndRemovesViews) {
  View extra = make_view("extra");
  extra.include = {"customer"};
  auto r = update(op::AddView{extra});
  EXPECT_TRUE(r.source.size() >= 34);
  EXPECT_TRUE((r.source.substr(r.source.size() - 34) == "view extra {\n  include customer\n}\n"));
  r = update(op::RemoveView{"everything"});
  EXPECT_FALSE(has(r.source, "view everything"));
}

TEST(UpdateSource, RenamesAViewInPlace) {
  auto r = update(op::RenameView{"checkout", "orders"});
  EXPECT_TRUE(r.minimal);
  ASSERT_TRUE(r.edits.size() == 1);
  EXPECT_TRUE(has(r.source, "view orders of shop {\n  include *\n"));
  EXPECT_TRUE(has(r.source, "pin db below web    // written by a canvas drag"));
  EXPECT_TRUE((r.result.model.views.keys() == std::vector<std::string>{"orders", "everything"}));
}

TEST(UpdateSource, AppliesBatchesAndCreatesAModelBlockWhenThereIsNone) {
  auto r = update(op::Batch{{op::AddElement{make_element("a", "process")},
                             op::AddElement{make_element("b", "process")},
                             op::AddRelationship{make_relationship("a->b", {"a", {}}, {"b", {}})}}},
                  "// empty\n");
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE((r.source == "model {\n  a = process\n  b = process\n  a -> b\n}\n\n// empty\n"));
}

TEST(UpdateSource, FallsBackToReprintingWhenAPatchCannotExpressTheChange) {
  auto r = update(op::AddGroup{{"G", "G", {}}}, "model {\n  a = process\n}\n");
  EXPECT_FALSE(r.minimal);
}

TEST(UpdateSource, RejectsInvalidOperations) {
  EXPECT_FALSE((update_source(kSource, op::RemoveElement{"nope"}).ok()));
}

TEST(UpdateSource, PatchesAddingAndRemovingAUseInPlace) {
  const std::string src = "// Libraries first.\nuse \"c4@^1\"\nuse \"aws-icons@^2.1\" as aws\n\nmodel {\n}\n";
  auto added = update(op::UseLibrary{{"plantuml-gcp", "^1", "gcp"}, {}}, src);
  EXPECT_TRUE(added.minimal);
  EXPECT_TRUE(has(added.source, "use \"aws-icons@^2.1\" as aws\nuse \"plantuml-gcp@^1\" as gcp\n"));
  auto removed = update(op::UnuseLibrary{"aws"}, added.source);
  EXPECT_TRUE(removed.minimal);
  EXPECT_TRUE(removed.model.libraries->size() == 2);
  auto first = update(op::UseLibrary{{"c4", "^1", "c4"}, {}}, "model {\n}\n");
  EXPECT_TRUE((first.source == "use \"c4@^1\"\n\nmodel {\n}\n"));
}

TEST(Patch, DiffEditNeverSplitsAUTF8Character) {
  // "é" is C3 A9 and "è" is C3 A8: they share their first byte.
  auto edits = diff_edit("a\xC3\xA9z", "a\xC3\xA8z");
  ASSERT_TRUE(edits.size() == 1);
  EXPECT_TRUE(edits[0].from == 1);
  EXPECT_TRUE(edits[0].to == 3);
  EXPECT_TRUE(edits[0].insert == "\xC3\xA8");
}

// ── Runs of hint steps, as a group move makes them ──

namespace {

const char* const kHints = R"(model {
  a = process "A"
  b = process "B"
  c = process "C"
}

view v {
  include *
  pin b below a pin c below b
}

view w { include * }
)";

Operation remove_both(std::string view = "v") {
  return op::Batch{{op::RemoveHint{view, 1}, op::RemoveHint{view, 0}}};
}

}  // namespace

TEST(UpdateSource, RemovingEveryHintOnALineTakesTheLine) {
  auto r = update(remove_both(), kHints);
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "view v {\n  include *\n}\n")) << r.source;
}

namespace {

/// kHints with view v's body replaced.
std::string with_body(const std::string& body) {
  std::string source = kHints;
  const std::string from = "  include *\n  pin b below a pin c below b\n";
  source.replace(source.find(from), from.size(), body);
  return source;
}

}  // namespace

TEST(UpdateSource, RemovingHintsAfterAStatementLeavesTheStatementTidy) {
  auto r = update(remove_both(), with_body("  include * pin b below a pin c below b\n"));
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "view v {\n  include *\n}\n")) << r.source;
}

TEST(UpdateSource, RemovingHintsOnNeighbouringLinesTakesThoseLines) {
  auto r = update(remove_both(), with_body("  include *\n  pin b below a\n  pin c below b\n"));
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "view v {\n  include *\n}\n")) << r.source;
}

TEST(UpdateSource, RemovingHintsOnTwoLinesKeepsEachLineItsOwnRest) {
  // What stays on each line stays on that line, with its own comment.
  auto r = update(remove_both(), with_body("  include * pin b below a\n  pin c below b pin a below c\n"));
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "view v {\n  include *\n  pin a below c\n}\n")) << r.source;
  r = update(remove_both(), with_body("  include * pin b below a\n  pin c below b // keep?\n"));
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "view v {\n  include *\n}\n")) << r.source;
}

TEST(UpdateSource, HintsAppendedTogetherKeepTheirOrder) {
  auto add = [](std::string view, std::string element, double x) {
    return op::AddHint{std::move(view), PinAtHint{std::move(element), x, 0, std::nullopt, std::nullopt}, std::nullopt};
  };
  auto r = update(op::Batch{{add("v", "a", 1), add("v", "b", 2), add("w", "a", 3), add("w", "c", 4)}}, kHints);
  EXPECT_TRUE(r.minimal);
  EXPECT_TRUE(has(r.source, "  pin b below a pin c below b\n  pin a at 1, 0\n  pin b at 2, 0\n}\n")) << r.source;
  EXPECT_TRUE(has(r.source, "view w {\n  include *\n  pin a at 3, 0\n  pin c at 4, 0\n}\n")) << r.source;
}
