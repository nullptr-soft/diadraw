// Adding, renaming and removing views through a Document, as the shells' view menus do: the
// text each writes, and the view the canvas moves to.

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
  pin db below web   // dragged
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

}  // namespace

TEST(DocumentViews, AddShowsTheNewViewOfTheSameScope) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  EXPECT_EQ(doc.fresh_view(), "view1");
  std::string text = applied(doc, kSource, doc.add_view(doc.fresh_view()));
  EXPECT_NE(text.find("\n\nview view1 of shop {\n  include *\n}\n"), std::string::npos);
  EXPECT_EQ(doc.views(), (std::vector<std::string>{"orders", "view1"}));
  EXPECT_EQ(doc.view(), "view1");
  EXPECT_EQ(doc.fresh_view(), "view2");
}

TEST(DocumentViews, RenameKeepsTheViewWhereItIsAndShowsIt) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  std::string text = applied(doc, kSource, doc.add_view(" extra\t"));
  EXPECT_EQ(doc.view(), "extra");
  doc.choose_view("orders");
  text = applied(doc, text, doc.rename_view("checkout "));
  EXPECT_NE(text.find("view checkout of shop {\n  include *\n  pin db below web   // dragged\n}"), std::string::npos);
  EXPECT_EQ(doc.views(), (std::vector<std::string>{"checkout", "extra"}));
  EXPECT_EQ(doc.view(), "checkout");
  EXPECT_FALSE(doc.rename_view("extra").ok());
  EXPECT_FALSE(doc.rename_view("not a name").ok());
  auto same = doc.rename_view("checkout");
  ASSERT_TRUE(same.ok());
  EXPECT_FALSE(same->has_value());
}

TEST(DocumentViews, RemoveMovesToTheFirstViewLeftAndKeepsTheLastOne) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  std::string text = applied(doc, kSource, doc.add_view("extra"));
  doc.choose_view("orders");
  text = applied(doc, text, doc.remove_view());
  EXPECT_EQ(text.find("view orders"), std::string::npos);
  EXPECT_EQ(doc.views(), (std::vector<std::string>{"extra"}));
  EXPECT_EQ(doc.view(), "extra");
  EXPECT_FALSE(doc.remove_view().ok());
}

TEST(DocumentViews, ADocumentWithoutViewsKeepsTheOneItWasDrawnIn) {
  const std::string source = "model {\n  a = process\n}\n";
  Document doc(source, "test.draw");
  doc.refresh();
  EXPECT_EQ(doc.view(), "default");
  std::string text = applied(doc, source, doc.add_view("second"));
  EXPECT_NE(text.find("view default {\n  include *\n}\n\nview second {\n  include *\n}\n"), std::string::npos);
  EXPECT_EQ(doc.views(), (std::vector<std::string>{"default", "second"}));
  EXPECT_EQ(doc.view(), "second");
}
