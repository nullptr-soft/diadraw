// Port of packages/core/src/history.test.ts.

#include "diadraw/model/history.hpp"

#include "helpers.hpp"

using namespace dd;

namespace {

Element a() { return make_element("a", "process", "A"); }
Element b() { return make_element("b", "process", "B"); }
Relationship ab() { return make_relationship("ab", {"a", {}}, {"b", {}}); }

void apply(History& log, const Operation& op) {
  auto r = log.apply(op);
  ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
}

}  // namespace

TEST(History, UndoesAndRedoesASequenceOfOperations) {
  History log;
  apply(log, op::AddElement{a()});
  apply(log, op::AddElement{b()});
  apply(log, op::AddRelationship{ab()});
  apply(log, op::RenameElement{"a", "Start"});
  Model final_model = log.model();

  log.undo();
  EXPECT_TRUE(log.model().elements.find("a")->name == "A");
  log.undo();
  EXPECT_TRUE(log.model().relationships.empty());
  log.undo();
  log.undo();
  EXPECT_TRUE((log.model() == Model{}));
  EXPECT_FALSE(log.can_undo());

  while (log.can_redo()) log.redo();
  EXPECT_TRUE(log.model() == final_model);
}

TEST(History, UndoesACascadingRemoveInOneStep) {
  History log;
  apply(log, op::AddElement{a()});
  apply(log, op::AddElement{b()});
  apply(log, op::AddRelationship{ab()});
  Model before = log.model();
  apply(log, op::RemoveElement{"b"});
  EXPECT_TRUE(log.model().relationships.empty());
  log.undo();
  EXPECT_TRUE(log.model() == before);
}

TEST(History, ClearsTheRedoStackWhenANewOperationIsApplied) {
  History log;
  apply(log, op::AddElement{a()});
  log.undo();
  EXPECT_TRUE(log.can_redo());
  apply(log, op::AddElement{b()});
  EXPECT_FALSE(log.can_redo());
  EXPECT_TRUE(log.operations().size() == 1);
}

TEST(History, IsANoOpToUndoOrRedoWithAnEmptyStack) {
  History log;
  EXPECT_TRUE(&log.undo() == &log.model());
  EXPECT_TRUE(&log.redo() == &log.model());
  EXPECT_TRUE((log.model() == Model{}));
}

TEST(History, LeavesTheLogUnchangedWhenAnOperationFails) {
  History log;
  apply(log, op::AddElement{a()});
  EXPECT_FALSE((log.apply(op::AddElement{a()}).ok()));
  EXPECT_TRUE(log.operations().size() == 1);
}

TEST(History, NotifiesSubscribersOnApplyUndoAndRedo) {
  History log;
  int calls = 0;
  auto id = log.subscribe([&](const Model&, const Operation&) { ++calls; });
  apply(log, op::AddElement{a()});
  log.undo();
  log.redo();
  EXPECT_TRUE(calls == 3);
  log.unsubscribe(id);
  log.undo();
  EXPECT_TRUE(calls == 3);
}
