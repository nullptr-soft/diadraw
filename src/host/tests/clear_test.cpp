// Clear Canvas through a Document, as every shell's menu runs it: the whole text becomes an empty
// diagram in one change that undo reverses, and the canvas follows.

#include <gtest/gtest.h>

#include "diadraw/dsl/patch.hpp"
#include "host/document.hpp"

using namespace dd;
using namespace dd::host;

namespace {

const std::string kSource = R"(model {
  shop = system "Shop" {
    web = container "Web app"
    db = database "Orders"
  }
  web -> db
}

view orders of shop {
  include *
}
)";

}  // namespace

TEST(DocumentClear, ReplacesTheWholeTextWithAnEmptyDiagramInOneEdit) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  auto change = doc.clear();
  ASSERT_TRUE(change.ok()) << change.error().message;
  ASSERT_TRUE(change->has_value());
  ASSERT_EQ((*change)->edits.size(), 1u);
  EXPECT_FALSE((*change)->note.has_value());
  // The shell applies it to its text view; the result is what the session holds.
  std::string after = dsl::apply_edits(kSource, (*change)->edits);
  EXPECT_EQ(after, Document::kEmptyText);
  EXPECT_EQ(doc.text(), Document::kEmptyText);
}

TEST(DocumentClear, MovesTheCanvasToTheEmptyDiagramsView) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  EXPECT_EQ(doc.view(), "orders");
  ASSERT_TRUE(doc.clear().ok());
  auto refreshed = doc.refresh();
  EXPECT_FALSE(refreshed.error) << refreshed.status;
  EXPECT_EQ(doc.views(), (std::vector<std::string>{"default"}));
  EXPECT_EQ(doc.view(), "default");
  ASSERT_TRUE(refreshed.render.has_value());
  EXPECT_TRUE(doc.session().model().elements.empty());
}

TEST(DocumentClear, DoesNothingOnAnEmptyDiagram) {
  Document doc(std::string(Document::kEmptyText), "test.draw");
  doc.refresh();
  auto change = doc.clear();
  ASSERT_TRUE(change.ok());
  EXPECT_FALSE(change->has_value());
}

TEST(DocumentClear, UndoAsTypingBringsTheDiagramBack) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ASSERT_TRUE(doc.clear().ok());
  doc.refresh();
  // A shell's undo puts its text view back and hands the text to the document, as typing does.
  doc.set_text(kSource);
  doc.refresh();
  EXPECT_EQ(doc.text(), kSource);
  EXPECT_EQ(doc.view(), "orders");
  EXPECT_TRUE(doc.session().model().elements.contains("shop.web"));
}

TEST(DocumentClear, AnElementCanBeAddedAfterwards) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ASSERT_TRUE(doc.clear().ok());
  doc.refresh();
  std::string created;
  auto change = doc.add("system", &created);
  ASSERT_TRUE(change.ok()) << change.error().message;
  ASSERT_TRUE(change->has_value());
  EXPECT_FALSE(created.empty());
  EXPECT_TRUE(doc.session().model().elements.contains(created));
}
