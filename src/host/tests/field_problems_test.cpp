// The inspector fields an edit would refuse, as the shells ask while a field is typed in to
// outline it: each problem names its field, and asking changes nothing.

#include <gtest/gtest.h>

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
  orders = shop.web -> shop.db
}
style container { fill "#438dd5" }
)";

/// The fields with a problem, in the order reported.
std::vector<std::string> fields_of(const std::vector<editor::FieldProblem>& problems) {
  std::vector<std::string> out;
  for (const auto& p : problems) out.push_back(p.field);
  return out;
}

using Fields = std::vector<std::string>;

}  // namespace

TEST(FieldProblems, NoneForTheFieldsAsShown) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  EXPECT_TRUE(doc.problems(*doc.fields("shop.web")).empty());
  EXPECT_TRUE(doc.problems(*doc.relationship_fields("orders")).empty());
  EXPECT_TRUE(doc.problems(*doc.relationship_fields("user->shop.web")).empty());
}

TEST(FieldProblems, AnIdTheRenameWouldRefuse) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ElementFields f = *doc.fields("shop.web");
  for (std::string bad : {"2web", "web app", "", "db"}) {
    f.local_id = bad;
    auto problems = doc.problems(f);
    ASSERT_EQ(fields_of(problems), Fields{"id"}) << bad;
    EXPECT_FALSE(problems[0].message.empty());
  }
  f.local_id = "db";
  EXPECT_NE(doc.problems(f)[0].message.find("already"), std::string::npos);
  // A free name, the same one, and one with spaces around it (the field is trimmed) are fine.
  for (std::string good : {"store", "web", "  store "}) {
    f.local_id = good;
    EXPECT_TRUE(doc.problems(f).empty()) << good;
  }
  // `user` is taken at the top level, not inside `shop`.
  f.local_id = "user";
  EXPECT_TRUE(doc.problems(f).empty());
  // Asking changes nothing.
  EXPECT_EQ(doc.text(), kSource);
}

TEST(FieldProblems, TheIdAnswerFollowsTheTextAndTheElement) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ElementFields f = *doc.fields("shop.web");
  f.local_id = "store";
  EXPECT_TRUE(doc.problems(f).empty());
  // Another text where `store` is taken inside `shop`: asked again, not kept.
  std::string taken = kSource;
  taken.replace(taken.find("    db ="), 8, "    store =");
  taken.replace(taken.find("shop.db\n"), 7, "shop.store");
  doc.set_text(taken);
  doc.refresh();
  ASSERT_TRUE(doc.fields("shop.store"));
  EXPECT_EQ(fields_of(doc.problems(f)), Fields{"id"});
  // Back to the first text, as an undo would.
  doc.set_text(kSource);
  doc.refresh();
  EXPECT_TRUE(doc.problems(f).empty());
  // The same Id for another element: `db` renamed to `web` is taken.
  ElementFields db = *doc.fields("shop.db");
  db.local_id = "web";
  f.local_id = "web";
  EXPECT_TRUE(doc.problems(f).empty());
  EXPECT_EQ(fields_of(doc.problems(db)), Fields{"id"});
}

TEST(FieldProblems, AKindAShapeOrAStyleTheUpdateWouldRefuse) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ElementFields f = *doc.fields("shop.web");
  f.kind = "web app";
  f.shape = "round box";
  f.style = "=red";
  EXPECT_EQ(fields_of(doc.problems(f)), (Fields{"kind", "shape", "style"}));
  f.kind = "  ";
  f.shape = "";
  f.style = "fill=red";
  EXPECT_EQ(fields_of(doc.problems(f)), Fields{"kind"});
  // A library's `alias.name` is a kind and a shape.
  f.kind = "aws.ec2";
  f.shape = "aws.lambda";
  EXPECT_TRUE(doc.problems(f).empty());
  // Every other field takes any text.
  f.name = "\"quoted\" ; = name";
  f.technology = "C++ / Qt";
  f.description = "line one\nline \"two\"";
  f.tags = "a, , b";
  f.props = "key with spaces=value\n=no key";
  EXPECT_TRUE(doc.problems(f).empty());
}

TEST(FieldProblems, SharedStylesTheUpdateWouldRefuse) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ElementFields f = *doc.fields("shop.web");
  ASSERT_EQ(f.shared_styles, "container: fill=#438dd5");
  for (std::string bad : {"container fill=red", "person: fill=red", "container: fill colour=red", "tag \"\": x=1"}) {
    f.shared_styles = bad;
    EXPECT_EQ(fields_of(doc.problems(f)), Fields{"shared-styles"}) << bad;
  }
  f.shared_styles = "container: fill=#438dd5\n*: dashed=1";
  EXPECT_TRUE(doc.problems(f).empty());
  // The update says the same.
  f.shared_styles = "container: fill colour=red";
  EXPECT_FALSE(doc.update(f).ok());
}

TEST(FieldProblems, ARelationshipNameTheUpdateWouldRefuse) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  RelationshipFields f = *doc.relationship_fields("user->shop.web");
  for (std::string bad : {"2nd", "user visits", "orders"}) {
    f.name = bad;
    EXPECT_EQ(fields_of(doc.problems(f)), Fields{"name"}) << bad;
  }
  for (std::string good : {"", "visits", " visits "}) {
    f.name = good;
    EXPECT_TRUE(doc.problems(f).empty()) << good;
  }
  // Its own name is not taken.
  RelationshipFields named = *doc.relationship_fields("orders");
  EXPECT_TRUE(doc.problems(named).empty());
  named.style = "=1";
  EXPECT_EQ(fields_of(doc.problems(named)), Fields{"style"});
}

TEST(FieldProblems, NoneForAnItemThatIsGone) {
  Document doc(kSource, "test.draw");
  doc.refresh();
  ElementFields f = *doc.fields("shop.web");
  f.id = "shop.gone";
  f.kind = "bad kind";
  EXPECT_TRUE(doc.problems(f).empty());
  RelationshipFields r = *doc.relationship_fields("orders");
  r.id = "gone";
  r.name = "2bad";
  EXPECT_TRUE(doc.problems(r).empty());
}
