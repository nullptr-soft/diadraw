#include "diadraw/diadraw.h"

#include <string>

#include "helpers.hpp"

namespace {

std::string take(char* s) {
  std::string out = s ? s : "";
  dd_string_free(s);
  return out;
}

}  // namespace

TEST(CABI, AppliesUndoesAndReportsOperations) {
  EXPECT_TRUE(std::string(dd_version()).size() > 0);
  EXPECT_TRUE(dd_abi_version() == DD_ABI_VERSION);

  dd_document* doc = dd_document_new();
  char* error = nullptr;
  EXPECT_TRUE((dd_document_apply(doc, R"({"type":"addElement","element":{"id":"a","kind":"process"}})",
                          &error) == DD_OK));
  EXPECT_TRUE(error == nullptr);
  EXPECT_TRUE((dd_document_apply(doc, R"({"type":"addElement","element":{"id":"a","kind":"process"}})",
                          &error) == DD_REJECTED));
  EXPECT_TRUE(dd::test::contains(take(error), "already exists"));
  EXPECT_TRUE((dd_document_apply(doc, "{not json", &error) == DD_INVALID_ARGUMENT));
  take(error);

  auto model = take(dd_document_model_json(doc));
  EXPECT_TRUE((dd::test::contains(model, R"("a":{"id":"a","kind":"process","name":"a")")));

  EXPECT_TRUE(dd_document_can_undo(doc) == 1);
  EXPECT_TRUE(dd_document_undo(doc) == 1);
  EXPECT_TRUE(dd_document_undo(doc) == 0);
  EXPECT_TRUE((take(dd_document_model_json(doc)) ==
        R"({"elements":{},"relationships":{},"groups":{},"views":{}})"));
  EXPECT_TRUE(dd_document_redo(doc) == 1);
  dd_document_free(doc);
}

TEST(CABI, LoadsModelJSONAndResolvesViews) {
  char* error = nullptr;
  dd_document* doc = dd_document_from_json(
      R"({"elements":{"a":{"id":"a","kind":"process"}},"relationships":{},"groups":{},
          "views":{"v":{"id":"v"}}})",
      &error);
  ASSERT_TRUE(doc != nullptr);
  auto resolved = take(dd_document_resolve_view_json(doc, "v", &error));
  EXPECT_TRUE((dd::test::contains(resolved, R"("elements":[{"id":"a")")));
  EXPECT_TRUE(dd_document_resolve_view_json(doc, "nope", &error) == nullptr);
  EXPECT_TRUE(dd::test::contains(take(error), "not found"));
  dd_document_free(doc);

  EXPECT_TRUE(dd_document_from_json("[]", &error) == nullptr);
  take(error);
  EXPECT_TRUE((dd_document_apply(nullptr, "{}", nullptr) == DD_INVALID_ARGUMENT));
}
