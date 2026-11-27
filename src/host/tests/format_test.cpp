// Format Document through a Document, as every native shell's menu runs it: one edit over the
// lines that change, which the shell applies to its text view as one undo step.

#include <gtest/gtest.h>

#include "diadraw/dsl/patch.hpp"
#include "host/document.hpp"

using namespace dd;
using namespace dd::host;

namespace {

const std::string kSource = "// Orders\nmodel {\nshop = system \"Shop\" {\n      web = container \"Web app\"\n  }\n}\n";

}  // namespace

TEST(DocumentFormat, ReindentsInOneEditAndKeepsComments) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  auto change = doc.format(4);
  ASSERT_TRUE(change.ok()) << change.error().message;
  ASSERT_TRUE(change->has_value());
  ASSERT_EQ((*change)->edits.size(), 1u);
  EXPECT_FALSE((*change)->note.has_value());
  const std::string formatted =
      "// Orders\nmodel {\n    shop = system \"Shop\" {\n        web = container \"Web app\"\n    }\n}\n";
  EXPECT_EQ(dsl::apply_edits(kSource, (*change)->edits), formatted);
  EXPECT_EQ(doc.text(), formatted);
  // The edit leaves the unchanged start and end of the text alone.
  EXPECT_EQ((*change)->edits[0].from, std::string("// Orders\nmodel {\n").size());
  EXPECT_LT((*change)->edits[0].to, kSource.size());
}

TEST(DocumentFormat, NothingWhenAlreadyFormatted) {
  Document doc(kSource, "test.draw");
  ASSERT_TRUE(doc.format(2).ok());
  auto again = doc.format(2);
  ASSERT_TRUE(again.ok());
  EXPECT_FALSE(again->has_value());
}

TEST(DocumentFormat, EditsEndOnWholeCharacters) {
  const std::string source = "model {\n a = process \"Café\"\n b = process \"Ölçü\"\n}\n";
  Document doc(source, "test.draw");
  auto change = doc.format(2);
  ASSERT_TRUE(change.ok() && change->has_value());
  EXPECT_EQ(dsl::apply_edits(source, (*change)->edits), "model {\n  a = process \"Café\"\n  b = process \"Ölçü\"\n}\n");
}
