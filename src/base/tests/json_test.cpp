#include "diadraw/base/json.hpp"

#include <cmath>

#include "diadraw/model/json.hpp"
#include "helpers.hpp"

using namespace dd;

TEST(Json, ParsesAndPrintsKeepingMemberOrder) {
  auto j = Json::parse(R"({"b":1,"a":[true,false,null,"x\né"],"c":{"z":0.5,"y":-2}})");
  ASSERT_TRUE(j.ok());
  EXPECT_TRUE((j->dump() == "{\"b\":1,\"a\":[true,false,null,\"x\\n\xC3\xA9\"],\"c\":{\"z\":0.5,\"y\":-2}}"));
  EXPECT_TRUE((j->dump_sorted() ==
        "{\"a\":[true,false,null,\"x\\n\xC3\xA9\"],\"b\":1,\"c\":{\"y\":-2,\"z\":0.5}}"));
}

TEST(Json, EqualityIgnoresMemberOrder) {
  EXPECT_TRUE((*Json::parse(R"({"a":1,"b":2})") == *Json::parse(R"({"b":2,"a":1})")));
  EXPECT_FALSE(*Json::parse("[1,2]") == *Json::parse("[2,1]"));
}

TEST(Json, PrintsNumbersAsJavaScriptDoes) {
  EXPECT_TRUE(format_number(3) == "3");
  EXPECT_TRUE(format_number(-0.0) == "0");
  EXPECT_TRUE(format_number(0.1) == "0.1");
  EXPECT_TRUE(format_number(1e21) == "1e+21");
  EXPECT_TRUE(format_number(1e-7) == "1e-7");
  EXPECT_TRUE(format_number(123.456) == "123.456");
  EXPECT_EQ(js_number_string(0.00001), "0.00001");
  EXPECT_EQ(js_number_string(1e-7), "1e-7");
  EXPECT_EQ(js_number_string(123456789012345680000.0), "123456789012345680000");
  EXPECT_EQ(js_number_string(-1.5e300), "-1.5e+300");
  EXPECT_EQ(js_number_string(std::nan("")), "NaN");
  EXPECT_EQ(js_round(-2.5), -2);
  EXPECT_EQ(js_round(2.5), 3);
  EXPECT_EQ(js_round(0.49999999999999994), 0);
}

TEST(Json, DecodesSurrogatePairs) {
  auto j = Json::parse(R"("😀")");
  ASSERT_TRUE(j.ok());
  EXPECT_TRUE(j->as_string() == "\xF0\x9F\x98\x80");
}

TEST(Json, RejectsMalformedInputWithAnOffset) {
  for (const char* bad : {"{", "[1,]", "{\"a\" 1}", "tru", "\"abc", "1 2"}) {
    auto j = Json::parse(bad);
    EXPECT_FALSE(j.ok());
  }
  EXPECT_TRUE(dd::test::contains(Json::parse("[1,]").error().message, "offset"));
}

TEST(ModelJSON, AModelSurvivesARoundTripOrderAndOptionalFieldsIncluded) {
  Model m;
  auto shop = make_element("shop", "system", "Shop");
  shop.ports.push_back({"api", PortSide::East});
  shop.style = Style{{"fill", "#dae8fc"}, {"rounded", "1"}};
  shop.properties.set("owner", "Ann");
  m.elements.set("shop", shop);
  m.elements.set("db", make_element("db", "database", {}, "shop"));
  auto r = make_relationship("r1", {"shop", "api"}, {"db", {}}, "writes");
  r.line = LineStyle::Dashed;
  m.relationships.set("r1", r);
  m.groups.set("g", Group{"g", "Group", {}});
  View v = make_view("v", "shop");
  v.layout = {"layered", Direction::Right, 40.0};
  v.hints = {PinHint{"db", Placement::LeftOf, "shop"}, PinAtHint{"db", 1.5, 2, 100, {}},
             RouteHint{"r1", {{1, 2}, {3, 4}}, Point2{1, 0.5}, {}}, AlignHint{{"a", "b"}, Axis::Vertical},
             OrderHint{{"x"}}};
  v.sequence = std::vector<SequenceItem>{
      SequenceMessage{"shop", "db", "q", MessageStyle::Async},
      SequenceActivation{false, "db"},
      SequenceNote{NotePosition::Right, {"db"}, "n"},
      SequenceDivider{},
      SequenceDelay{"5 min"},
      SequenceRef{{"shop", "db"}, "Logout"},
      SequenceFrame{FrameKind::Loop, {FrameSection{"each", {SequenceMessage{"db", "shop", {}, MessageStyle::Reply}}}}},
  };
  v.autonumber = true;
  m.views.set("v", v);
  m.libraries = std::vector<LibraryUse>{{"aws-icons", "^1", "aws"}};

  auto text = to_json(m).dump();
  auto parsed = Json::parse(text);
  ASSERT_TRUE(parsed.ok());
  auto back = model_from_json(*parsed);
  ASSERT_TRUE(back.ok()) << (back.ok() ? "" : back.error().message);
  EXPECT_TRUE(*back == m);
  EXPECT_TRUE(to_json(*back).dump() == text);
}

TEST(ModelJSON, ReadsTheTypeScriptShapeWithDefaultsForWhatJSONLeavesOut) {
  auto j = Json::parse(R"({"elements":{"a":{"id":"a","kind":"process"}},
    "relationships":{},"groups":{},"views":{"v":{"id":"v","layout":{"algorithm":"fixed"}}}})");
  ASSERT_TRUE(j.ok());
  auto m = model_from_json(*j);
  ASSERT_TRUE(m.ok());
  EXPECT_TRUE(m->elements.find("a")->name == "a");
  EXPECT_TRUE((m->views.find("v")->include == std::vector<std::string>{"*"}));
  EXPECT_TRUE(m->views.find("v")->layout.direction == Direction::Down);
}

TEST(ModelJSON, NamesTheProblemInMalformedInput) {
  auto j = Json::parse(R"({"elements":{"a":{"id":"a","kind":3}}})");
  auto m = model_from_json(*j);
  ASSERT_FALSE(m.ok());
  EXPECT_TRUE(dd::test::contains(m.error().message, "\"kind\" must be a string"));

  auto op = operation_from_json(*Json::parse(R"({"type":"explode"})"));
  ASSERT_FALSE(op.ok());
  EXPECT_TRUE(dd::test::contains(op.error().message, "unknown type"));
}

TEST(ModelJSON, EveryOperationSurvivesARoundTrip) {
  std::vector<Operation> ops{
      op::AddElement{make_element("a", "process")},
      op::RemoveElement{"a"},
      op::RenameElement{"a", "A"},
      op::AddRelationship{make_relationship("r", {"a", {}}, {"b", "p"})},
      op::RemoveRelationship{"r"},
      op::ReconnectRelationship{"r", {"a", {}}, {"c", "p"}, {}},
      op::ReconnectRelationship{"a->b", {"a", {}}, {"c", {}}, {{"a->b", "a->c"}, {"a->c", "a->c#2"}}},
      op::SetRelationshipLabel{"r", std::nullopt},
      op::SetRelationshipLabel{"r", "x"},
      op::AddGroup{{"g", "G", "h"}},
      op::RemoveGroup{"g"},
      op::AddView{make_view("v")},
      op::RemoveView{"v"},
      op::RenameView{"v", "w"},
      op::ReplaceView{make_view("v", "a")},
      op::SetLayout{"v", {"layered", Direction::Up, {}}},
      op::AddHint{"v", OrderHint{{"a"}}, 2},
      op::AddHint{"v", OrderHint{{"a"}}, {}},
      op::RemoveHint{"v", 0},
      op::ReplaceHint{"v", 1, RouteHint{"pay", {{10, 20}}, Point2{1, 0.5}, std::nullopt}},
      op::UseLibrary{{"bpmn", "*", "bpmn"}, {}},
      op::UnuseLibrary{"bpmn"},
      op::Batch{{op::RemoveView{"v"}, op::Batch{{op::RemoveElement{"a"}}}}},
  };
  for (const auto& o : ops) {
    auto back = operation_from_json(*Json::parse(to_json(o).dump()));
    ASSERT_TRUE(back.ok()) << (back.ok() ? "" : back.error().message);
    EXPECT_TRUE(*back == o);
  }
}
