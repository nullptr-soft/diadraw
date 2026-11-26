// Port of packages/core/src/operations.test.ts.

#include "diadraw/model/operations.hpp"

#include "helpers.hpp"

using namespace dd;
using namespace dd::test;

namespace {

Element shop_el() { return make_element("shop", "system", "Shop"); }
Element web_el() { return make_element("shop.web", "container", "Web app", "shop"); }
Element db_el() {
  auto e = make_element("shop.db", "container", "Orders DB", "shop");
  e.ports.push_back({"sql", PortSide::North});
  return e;
}
Element customer_el() { return make_element("customer", "person", "Customer"); }
Relationship writes() {
  return make_relationship("r1", {"shop.web", {}}, {"shop.db", "sql"}, "writes");
}
Relationship places() { return make_relationship("r2", {"customer", {}}, {"shop.web", {}}); }

Model base_model() {
  return build({op::AddElement{shop_el()}, op::AddElement{web_el()}, op::AddElement{db_el()},
                op::AddRelationship{writes()}});
}

}  // namespace

TEST(ApplyOperation, AddsAnElementAndReturnsARemovingInverse) {
  auto [model, inverse] = apply_ok(Model{}, op::AddElement{shop_el()});
  EXPECT_TRUE(*model.elements.find("shop") == shop_el());
  EXPECT_TRUE((inverse == Operation{op::RemoveElement{"shop"}}));
}

TEST(ApplyOperation, DoesNotMutateTheInputModel) {
  Model before;
  (void)apply_ok(before, op::AddElement{shop_el()});
  EXPECT_TRUE(before.elements.empty());
}

TEST(ApplyOperation, RejectsDuplicateIdsAndMissingParents) {
  auto model = build({op::AddElement{shop_el()}});
  EXPECT_TRUE((contains(apply_error(model, op::AddElement{shop_el()}), "already exists")));
  EXPECT_TRUE((contains(apply_error(Model{}, op::AddElement{web_el()}), "Parent \"shop\"")));
}

TEST(ApplyOperation, RenamesAnElementAndInvertsToTheOldName) {
  auto model = build({op::AddElement{shop_el()}});
  auto [renamed, inverse] = apply_ok(model, op::RenameElement{"shop", "Store"});
  EXPECT_TRUE(renamed.elements.find("shop")->name == "Store");
  EXPECT_TRUE(apply_ok(renamed, inverse).model == model);
}

TEST(ApplyOperation, AddsARelationshipOnlyBetweenExistingElementsAndPorts) {
  auto model = build({op::AddElement{shop_el()}, op::AddElement{web_el()}, op::AddElement{db_el()}});
  EXPECT_TRUE((*apply_ok(model, op::AddRelationship{writes()}).model.relationships.find("r1") == writes()));
  EXPECT_TRUE((contains(apply_error(model, op::AddRelationship{places()}), "Endpoint \"customer\"")));
  auto bad_port = make_relationship("r3", {"shop.web", "nope"}, {"shop.db", {}});
  EXPECT_TRUE((contains(apply_error(model, op::AddRelationship{bad_port}), "Port \"shop.web.nope\"")));
}

TEST(ApplyOperation, RemovesARelationshipAndRestoresItOnInverse) {
  auto model = base_model();
  auto [removed, inverse] = apply_ok(model, op::RemoveRelationship{"r1"});
  EXPECT_TRUE(removed.relationships.empty());
  EXPECT_TRUE(apply_ok(removed, inverse).model == model);
}

TEST(ApplyOperation, CascadesElementRemovalToDescendantsAndRelationshipsAndUndoesItExactly) {
  auto model = build({op::AddElement{customer_el()}, op::AddElement{shop_el()},
                      op::AddElement{web_el()}, op::AddElement{db_el()},
                      op::AddRelationship{writes()}, op::AddRelationship{places()}});
  auto [removed, inverse] = apply_ok(model, op::RemoveElement{"shop"});
  EXPECT_TRUE((removed.elements.keys() == std::vector<std::string>{"customer"}));
  EXPECT_TRUE(removed.relationships.empty());
  auto restored = apply_ok(removed, inverse).model;
  EXPECT_TRUE(restored == model);
  // Order is part of the model: the restored elements come back in their original order here.
  EXPECT_TRUE(restored.elements.keys() == model.elements.keys());
}

TEST(ApplyOperation, AppliesBatchesAtomically) {
  Operation bad = op::Batch{{op::AddElement{shop_el()}, op::AddElement{shop_el()}}};
  EXPECT_TRUE((contains(apply_error(Model{}, bad), "already exists")));

  Operation ok = op::Batch{{op::AddElement{shop_el()}, op::AddElement{web_el()}}};
  auto [model, inverse] = apply_ok(Model{}, ok);
  EXPECT_TRUE((model.elements.keys() == std::vector<std::string>{"shop", "shop.web"}));
  EXPECT_TRUE((apply_ok(model, inverse).model == Model{}));
}

TEST(ApplyOperation, AFailedBatchLeavesAnInPlaceModelExactlyAsItWasOrderIncluded) {
  auto model = build({op::AddElement{customer_el()}, op::AddElement{shop_el()}});
  Model in_place = model;
  auto r = apply_in_place(in_place, op::Batch{{op::RemoveElement{"customer"},
                                               op::AddElement{shop_el()}}});
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(in_place.elements.keys() == model.elements.keys());
}

TEST(GroupViewAndHintOperations, AddsAndRemovesGroupsRefusingToRemoveAGroupInUse) {
  auto [model, _] = apply_ok(base_model(), op::AddGroup{{"g", "G", {}}});
  auto x = make_element("x", "process");
  x.group = "g";
  auto in_group = apply_ok(model, op::AddElement{x}).model;
  EXPECT_TRUE((contains(apply_error(in_group, op::RemoveGroup{"g"}), "not empty")));
  auto [removed, inverse] = apply_ok(model, op::RemoveGroup{"g"});
  EXPECT_TRUE(removed.groups.empty());
  EXPECT_TRUE(apply_ok(removed, inverse).model == model);
}

TEST(GroupViewAndHintOperations, SetsAndClearsRelationshipLabels) {
  auto base = base_model();
  auto [model, inverse] = apply_ok(base, op::SetRelationshipLabel{"r1", std::nullopt});
  EXPECT_FALSE(model.relationships.find("r1")->label.has_value());
  EXPECT_TRUE(apply_ok(model, inverse).model == base);
}

TEST(GroupViewAndHintOperations, AddsViewsEditsLayoutAndHintsAndInvertsEachStep) {
  std::vector<Operation> steps{
      op::AddView{make_view("v", "shop")},
      op::SetLayout{"v", {"layered", Direction::Right, {}}},
      op::AddHint{"v", PinHint{"shop.db", Placement::Below, "shop.web"}, {}},
      op::AddHint{"v", OrderHint{{"shop.web"}}, 0},
      op::ReplaceHint{"v", 0, PinAtHint{"shop.web", 10, 20, 120, 60}},
      op::RemoveHint{"v", 1},
  };
  Model model = base_model();
  std::vector<Model> history;
  std::vector<Operation> inverses;
  for (const auto& step : steps) {
    history.push_back(model);
    auto result = apply_ok(model, step);
    model = std::move(result.model);
    inverses.push_back(std::move(result.inverse));
  }
  EXPECT_TRUE((model.views.find("v")->hints == std::vector<LayoutHint>{PinAtHint{"shop.web", 10, 20, 120, 60}}));
  for (std::size_t i = steps.size(); i-- > 0;) {
    model = apply_ok(model, inverses[i]).model;
    EXPECT_TRUE(model == history[i]);
  }
}

TEST(GroupViewAndHintOperations, ValidatesViewScopeAndHintIndexes) {
  auto base = base_model();
  EXPECT_TRUE((contains(apply_error(base, op::AddView{make_view("v", "nope")}), "Scope")));
  auto with_view = apply_ok(base, op::AddView{make_view("v")}).model;
  EXPECT_TRUE((contains(apply_error(with_view, op::RemoveHint{"v", 0}), "out of range")));
  EXPECT_TRUE((contains(apply_error(with_view, op::ReplaceHint{"v", 0, OrderHint{{"a"}}}), "out of range")));
}

TEST(GroupViewAndHintOperations, RenamesAViewWhereItStandsAndBack) {
  auto model = apply_ok(base_model(), op::Batch{{op::AddView{make_view("a")}, op::AddView{make_view("b", "shop")},
                                                 op::AddView{make_view("c")}}})
                   .model;
  auto [renamed, inverse] = apply_ok(model, op::RenameView{"b", "inside"});
  EXPECT_TRUE((renamed.views.keys() == std::vector<std::string>{"a", "inside", "c"}));
  ASSERT_TRUE(renamed.views.find("inside"));
  EXPECT_TRUE(renamed.views.find("inside")->id == "inside");
  EXPECT_TRUE(renamed.views.find("inside")->scope == std::optional<std::string>("shop"));
  EXPECT_TRUE(apply_ok(renamed, inverse).model == model);
  EXPECT_TRUE((contains(apply_error(model, op::RenameView{"b", "c"}), "already exists")));
  EXPECT_TRUE((contains(apply_error(model, op::RenameView{"nope", "d"}), "not found")));
  for (const char* bad : {"", "9z", "a b", "x{", "shop.web"})
    EXPECT_TRUE((contains(apply_error(model, op::RenameView{"b", bad}), "is not a view name"))) << bad;
}

TEST(GroupViewAndHintOperations, LibrariesUseRefuseATakenAliasUnuseBackToNone) {
  LibraryUse aws{"aws-icons", "^1", "aws"};
  auto [model, inverse] = apply_ok(Model{}, op::UseLibrary{aws, {}});
  ASSERT_TRUE(model.libraries.has_value());
  EXPECT_TRUE(model.libraries->size() == 1);
  EXPECT_TRUE((contains(apply_error(model, op::UseLibrary{aws, {}}), "already in use")));
  auto back = apply_ok(model, inverse).model;
  EXPECT_FALSE(back.libraries.has_value());
}

TEST(Operations, RemoveElementStripsViewReferencesDropsScopedViewsAndRestoresBothOnUndo) {
  auto model = build({op::AddElement{shop_el()}, op::AddElement{web_el()}, op::AddElement{db_el()},
                      op::AddElement{customer_el()}});
  View ctx = make_view("ctx");
  ctx.include = {"customer", "shop.db"};
  ctx.hints = {PinHint{"shop.db", Placement::Below, "customer"}, PinAtHint{"customer", 0, 0, {}, {}}};
  model = apply_ok(model, op::Batch{{op::AddView{make_view("inside", "shop")}, op::AddView{ctx}}}).model;

  auto removed = apply_ok(model, op::RemoveElement{"shop"});
  EXPECT_TRUE(removed.model.views.find("inside") == nullptr);
  const View* stripped = removed.model.views.find("ctx");
  ASSERT_TRUE(stripped);
  EXPECT_TRUE((stripped->include == std::vector<std::string>{"customer"}));
  EXPECT_TRUE((stripped->hints == std::vector<LayoutHint>{PinAtHint{"customer", 0, 0, {}, {}}}));
  EXPECT_TRUE(apply_ok(removed.model, removed.inverse).model == model);
}

TEST(Operations, RouteHintsGoWithTheirRelationshipAndComeBackOnUndo) {
  auto model = base_model();
  View v = make_view("v");
  v.layout = {"fixed", Direction::Down, {}};
  v.hints = {RouteHint{"r1", {{1, 2}}, {}, {}}, PinAtHint{"shop.web", 0, 0, 10, 10}};
  model = apply_ok(model, op::AddView{v}).model;

  auto kinds = [](const View& view) {
    std::vector<std::size_t> out;
    for (const auto& h : view.hints) out.push_back(h.index());
    return out;
  };
  const std::vector<std::size_t> only_pin_at{1};  // PinAtHint's index in the variant

  auto removed = apply_ok(model, op::RemoveRelationship{"r1"});
  EXPECT_TRUE(kinds(*removed.model.views.find("v")) == only_pin_at);
  EXPECT_TRUE(apply_ok(removed.model, removed.inverse).model == model);
  auto gone = apply_ok(model, op::RemoveElement{"shop.db"});
  EXPECT_TRUE(kinds(*gone.model.views.find("v")) == only_pin_at);
}

TEST(Operations, RemovingAnElementStripsItFromSequenceStepsFramesIncluded) {
  auto model = build({op::AddElement{make_element("a", "person")},
                      op::AddElement{make_element("b", "system")},
                      op::AddElement{make_element("c", "system")}});
  View v = make_view("seq");
  v.layout.algorithm = "sequence";
  v.sequence = std::vector<SequenceItem>{
      SequenceMessage{"a", "b", "hi", MessageStyle::Sync},
      SequenceFrame{FrameKind::Alt,
                    {FrameSection{"ok", {SequenceMessage{"b", "c", {}, MessageStyle::Async},
                                         SequenceMessage{"a", "b", {}, MessageStyle::Reply}}}}},
  };
  model = apply_ok(model, op::AddView{v}).model;
  auto removed = apply_ok(model, op::RemoveElement{"c"});
  const auto& seq = *removed.model.views.find("seq")->sequence;
  ASSERT_TRUE(seq.size() == 2);
  const auto& frame = std::get<SequenceFrame>(seq[1]);
  EXPECT_TRUE(frame.sections[0].items.size() == 1);
  EXPECT_TRUE(apply_ok(removed.model, removed.inverse).model == model);
}
