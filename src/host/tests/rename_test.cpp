// Renaming an element through a Document, as the shells' Rename Element… and inspectors do: the
// element a Rename acts on, the selection that follows it, and an inspector edit that renames
// and changes other fields at once.

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
  user = person
  user -> shop.web
  shop.web -> shop.db
}
)";

/// Applies a change as a shell applies it to its text view, and hands the text back to the
/// document as typing would, to check the change and the session agree.
std::string applied(Document& doc, const std::string& before, const Result<std::optional<Change>>& change) {
  EXPECT_TRUE(change.ok()) << (change.ok() ? "" : change.error().message);
  if (!change.ok() || !*change) return before;
  std::string after = dsl::apply_edits(before, (*change)->edits);
  EXPECT_EQ(after, doc.text());
  doc.set_text(after);
  doc.refresh();
  return after;
}

std::size_t at(const std::string& text, std::string_view what, std::size_t plus = 0) {
  return text.find(what) + plus;
}

}  // namespace

TEST(DocumentRename, RenamesTheElementTheTextOrTheSelectionNames) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  editor::Selection none;
  editor::Selection one{{"shop.db"}, {}};
  std::size_t web = at(kSource, "shop.web ->", 6);  // on `web` in a reference
  // The text has the focus: its cursor wins over the selection.
  EXPECT_EQ(doc.element_to_rename(one, true, web), std::optional<std::string>("shop.web"));
  EXPECT_EQ(doc.element_to_rename(one, true, at(kSource, "system")), std::nullopt);
  // Otherwise the one selected element, or with none the one at the cursor.
  EXPECT_EQ(doc.element_to_rename(one, false, web), std::optional<std::string>("shop.db"));
  EXPECT_EQ(doc.element_to_rename(none, false, web), std::optional<std::string>("shop.web"));
  EXPECT_EQ(doc.element_to_rename(none, false, Document::kNoCaret), std::nullopt);
  // A declaration names its element too.
  EXPECT_EQ(doc.element_at(at(kSource, "shop =", 1)), std::optional<std::string>("shop"));
}

TEST(DocumentRename, TheSelectionFollowsTheRename) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  editor::Selection selection{{"shop", "shop.web", "user", "shopper"}, {"user->shop.web", "shop.web->shop.db"}};
  std::string renamed;
  applied(doc, kSource, doc.rename_element("shop", " store ", &renamed));
  EXPECT_EQ(renamed, "store");
  auto after = doc.follow_rename(selection, "shop", renamed);
  // `shopper` only starts with the same letters: it stays.
  EXPECT_EQ(after.elements, (std::vector<std::string>{"store", "store.web", "user", "shopper"}));
  // The lines' ids were derived from their ends: they changed and leave the selection.
  EXPECT_TRUE(after.relationships.empty());
}

TEST(DocumentRename, AnInspectorEditRenamesAndUpdatesAsOneChange) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  auto fields = doc.fields("shop.web");
  ASSERT_TRUE(fields.has_value());
  fields->local_id = "store";
  fields->technology = "React";
  std::string renamed;
  auto change = doc.update(*fields, &renamed);
  ASSERT_TRUE(change.ok() && change->has_value());
  EXPECT_EQ(renamed, "shop.store");
  EXPECT_EQ((*change)->edits.size(), 1u);  // one edit, so one undo step in a text view
  std::string text = applied(doc, kSource, change);
  EXPECT_NE(text.find("    store = container \"Web app\" tech \"React\"\n"), std::string::npos);
  EXPECT_NE(text.find("  user -> shop.store\n  shop.store -> shop.db\n"), std::string::npos);
}

TEST(DocumentRename, ARenameStandsWhenTheOtherFieldsAreRefused) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  auto fields = doc.fields("shop.web");
  ASSERT_TRUE(fields.has_value());
  fields->local_id = "store";
  fields->kind = "not a kind";
  std::string renamed;
  auto change = doc.update(*fields, &renamed);
  ASSERT_TRUE(change.ok() && change->has_value());
  EXPECT_EQ(renamed, "shop.store");
  ASSERT_TRUE((*change)->note.has_value());  // says why the kind was not set
  std::string text = applied(doc, kSource, change);
  EXPECT_NE(text.find("    store = container \"Web app\"\n"), std::string::npos);
}

TEST(DocumentRename, ATakenNameChangesNothing) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  auto change = doc.rename_element("shop.web", "db");
  ASSERT_FALSE(change.ok());
  EXPECT_NE(change.error().message.find("already"), std::string::npos);
  EXPECT_EQ(doc.text(), kSource);
}

TEST(DocumentRename, ANameOnSeveralLinesReadsBack) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  std::string text = applied(doc, kSource, doc.rename("shop.db", "Orders\nand lines"));
  // One string with an escaped line break, which parses back to the same name.
  EXPECT_NE(text.find(R"(db = database "Orders\nand lines")"), std::string::npos) << text;
  EXPECT_EQ(doc.name_of("shop.db"), "Orders\nand lines");
}
