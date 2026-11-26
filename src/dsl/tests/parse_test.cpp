// Ports of packages/dsl/src/parse.test.ts and use.test.ts.

#include "diadraw/dsl/parse.hpp"

#include <algorithm>

#include "diadraw/dsl/print.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const char* const kShop = R"(model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin db below web    // relative to the scope
}
)";

using Ids = std::vector<std::string>;

std::string slice(std::string_view s, Span span) { return std::string(s.substr(span.from, span.to - span.from)); }

std::vector<std::string> summary(const ParseResult& r) {
  std::vector<std::string> out;
  for (const auto& d : r.diagnostics) {
    out.push_back(std::to_string(d.line) + ":" + std::to_string(d.column) + " " +
                  (d.severity == Severity::Error ? "error " : "warning ") + d.message);
  }
  return out;
}

std::vector<std::string> messages(const ParseResult& r) {
  std::vector<std::string> out;
  for (const auto& d : r.diagnostics) out.push_back(d.message);
  return out;
}

/// print → parse → print is stable and the model survives.
void check_round_trip(const Model& model) {
  std::string printed = print(model);
  auto again = parse(printed);
  EXPECT_TRUE(again.diagnostics.empty());
  EXPECT_TRUE(again.model == model);
  EXPECT_TRUE(print(again.model) == printed);
}

}  // namespace

TEST(Parse, LowersThePlanExampleIntoTheModel) {
  auto r = parse(kShop);
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_TRUE((r.model.elements.keys() == Ids{"customer", "shop", "shop.web", "shop.db"}));
  const Element* db = r.model.elements.find("shop.db");
  ASSERT_TRUE(db);
  EXPECT_TRUE(db->kind == "container");
  EXPECT_TRUE(db->name == "Orders DB");
  EXPECT_TRUE(db->parent == std::optional<std::string>("shop"));
  EXPECT_TRUE(db->technology == std::optional<std::string>("Postgres"));
  EXPECT_TRUE(db->shape == std::optional<std::string>("cylinder"));
  EXPECT_TRUE((r.model.relationships.keys() == Ids{"customer->shop.web", "shop.web->shop.db"}));
  EXPECT_TRUE(r.model.relationships.find("customer->shop.web")->label == std::optional<std::string>("places order"));
  const View* v = r.model.views.find("checkout");
  ASSERT_TRUE(v);
  EXPECT_TRUE(v->scope == std::optional<std::string>("shop"));
  EXPECT_TRUE((v->include == Ids{"*"}));
  EXPECT_TRUE(v->layout.direction == Direction::Right);
  EXPECT_TRUE((v->hints == std::vector<LayoutHint>{PinHint{"shop.db", Placement::Below, "shop.web"}}));
  const auto& web = r.source_map.elements.at("shop.web");
  EXPECT_TRUE(slice(kShop, web.name) == "web");
  EXPECT_TRUE(slice(kShop, *web.label) == "\"Web app\"");
  EXPECT_TRUE(slice(kShop, r.source_map.views.at("checkout").hints[0]) == "pin db below web");
  check_round_trip(r.model);
}

TEST(Parse, DefaultsTheNameToTheIdentifierAndResolvesRelativeReferences) {
  auto r = parse(R"(model {
    sys = system {
      a = container
      b = container
      a -> b
    }
  })");
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_TRUE(r.model.elements.find("sys.a")->name == "a");
  EXPECT_TRUE(r.model.relationships.contains("sys.a->sys.b"));
}

TEST(Parse, HandlesPortsGroupsArrowsTagsAndEscapes) {
  auto r = parse(R"(model {
    api = container "API \"v2\"" tag "external" { port http north }
    group "Clients" {
      web = container
      group "Mobile" { ios = container }
    }
    web -> api.http "calls" tech "HTTPS"
    ios --> api
    web <-> ios
    web -> api
    web -> api
  })");
  EXPECT_TRUE(summary(r).empty());
  const Element* api = r.model.elements.find("api");
  EXPECT_TRUE(api->name == "API \"v2\"");
  EXPECT_TRUE((api->tags == Ids{"external"}));
  EXPECT_TRUE((api->ports == std::vector<Port>{{"http", PortSide::North}}));
  EXPECT_TRUE(r.model.elements.find("web")->group == std::optional<std::string>("Clients"));
  EXPECT_TRUE(r.model.groups.find("Clients/Mobile")->parent == std::optional<std::string>("Clients"));
  const Relationship* calls = r.model.relationships.find("web->api.http");
  ASSERT_TRUE(calls);
  EXPECT_TRUE(calls->target.port == std::optional<std::string>("http"));
  EXPECT_TRUE(calls->technology == std::optional<std::string>("HTTPS"));
  EXPECT_TRUE(r.model.relationships.find("ios->api")->line == LineStyle::Dashed);
  EXPECT_TRUE(r.model.relationships.find("web->ios")->bidirectional);
  EXPECT_TRUE(r.model.relationships.contains("web->api#2"));
}

TEST(Parse, ReportsUnknownReferencesDuplicatesAndKindsWithLineAndColumn) {
  auto r = parse("model {\n  a = process\n  a = process\n  a -> nope\n  b = widget\n}\nview v { include zzz }");
  EXPECT_TRUE((summary(r) == Ids{
                          "3:3 error Element \"a\" already exists",
                          "4:8 error Unknown element \"nope\"",
                          "5:7 warning Unknown element kind \"widget\"",
                          "7:18 error Unknown element \"zzz\"",
                      }));
  EXPECT_TRUE((r.model.elements.keys() == Ids{"a", "b"}));
}

TEST(Parse, WarnsAboutAnUnknownLayoutAndKeepsTheName) {
  auto r = parse("model {\n  a = process\n}\nview v {\n  layout circular direction right\n}\nview w { layout tree }");
  EXPECT_TRUE((summary(r) == Ids{
                          "5:10 warning Unknown layout \"circular\"; using layered "
                          "(known: layered, tree, grid, radial, force, fixed, sequence)",
                      }));
  EXPECT_EQ(r.model.views.find("v")->layout.algorithm, "circular");
  EXPECT_EQ(r.model.views.find("w")->layout.algorithm, "tree");
}

TEST(Parse, RecoversFromSyntaxErrorsAndKeepsWhatItCouldRead) {
  auto r = parse("model {\n  a = process\n  b = \n  c = process\n}");
  bool error_on_line_4 = false;
  for (const auto& d : r.diagnostics)
    if (d.severity == Severity::Error && d.line == 4) error_on_line_4 = true;
  EXPECT_TRUE(error_on_line_4);
  EXPECT_TRUE(r.model.elements.contains("a"));
  EXPECT_TRUE(r.model.elements.find("b")->kind == "c");
}

TEST(Parse, NeverFailsOnArbitraryInput) {
  for (const char* input : {"", "}}}", "view", "model { -> }", "view x of { pin }", "\"unterminated",
                            "model { a = process \"x\" 42 }", "view v { include a order order order order "
                            "order order order order order order order order order order order order }",
                            "model { a = b { c = d { e = f", "\xC3\xA9\xF0\x9F\x98\x80 model"}) {
    auto r = parse(input);
    (void)r;
  }
}

TEST(Parse, ColumnsCountUTF16Units) {
  // "😀" is 4 bytes but 2 UTF-16 units, so `widget` sits at column 30, not 32.
  auto r = parse("model { a = process \"\xF0\x9F\x98\x80\" b = widget }");
  ASSERT_TRUE(r.diagnostics.size() == 1);
  EXPECT_TRUE(r.diagnostics[0].column == 30);
  // A statement cut short is reported where the next token starts, as Lezer does.
  EXPECT_TRUE((summary(parse("model {\n  x = process zz\n}")) == Ids{"3:1 error Syntax error: incomplete statement"}));
}

TEST(Parse, ContextualKeywordsAreNamesWhereAnIdentifierIsExpected) {
  const char* source = R"(model {
  layout = container "Layout"
  order = process
  view = system { left = container  right = container  left -> right }
  layout -> order "uses"
  group "g" { model = process }
}
view pin of view {
  include *
  layout layered direction right
  pin view.left left of view.right
  order view.left view.right
}
)";
  auto r = parse(source);
  EXPECT_TRUE(summary(r).empty());
  EXPECT_TRUE((r.model.elements.keys() == Ids{"layout", "order", "view", "view.left", "view.right", "model"}));
  const View* v = r.model.views.find("pin");
  ASSERT_TRUE(v);
  EXPECT_TRUE(v->scope == std::optional<std::string>("view"));
  EXPECT_TRUE(v->layout.direction == Direction::Right);
  EXPECT_TRUE((v->hints == std::vector<LayoutHint>{PinHint{"view.left", Placement::LeftOf, "view.right"},
                                            OrderHint{{"view.left", "view.right"}}}));
  check_round_trip(r.model);
}

TEST(Parse, FreeformSyntaxStylesPropertiesSizedPinsAndRoutes) {
  const char* source = R"(model {
  a = process "A" style "fill=#dae8fc;stroke=#6c8ebf;rounded=1" prop "drawio.id" "x1" prop "owner" "Ann"
  b = process "B"
  a -> b "go" style "endArrow=open;dashed=1" prop "tooltip" "hi"
}

view page1 {
  include *
  layout fixed direction down
  pin a at 10, 20 size 120, 60
  pin b at 200, 20.5 size 80, 40
  route "a->b" via 150, 50 150, 40 from 1, 0.5 to 0, 0.5
}
)";
  auto r = parse(source);
  EXPECT_TRUE(summary(r).empty());
  const Element* a = r.model.elements.find("a");
  EXPECT_TRUE((*a->style == Style{{"fill", "#dae8fc"}, {"stroke", "#6c8ebf"}, {"rounded", "1"}}));
  EXPECT_TRUE((a->properties == Properties{{"drawio.id", "x1"}, {"owner", "Ann"}}));
  const Relationship* ab = r.model.relationships.find("a->b");
  EXPECT_TRUE((*ab->style == Style{{"endArrow", "open"}, {"dashed", "1"}}));
  EXPECT_TRUE((ab->properties == Properties{{"tooltip", "hi"}}));
  const View* v = r.model.views.find("page1");
  EXPECT_TRUE(v->layout.algorithm == "fixed");
  EXPECT_TRUE((v->hints == std::vector<LayoutHint>{
                        PinAtHint{"a", 10, 20, 120.0, 60.0},
                        PinAtHint{"b", 200, 20.5, 80.0, 40.0},
                        RouteHint{"a->b", {{150, 50}, {150, 40}}, Point2{1, 0.5}, Point2{0, 0.5}},
                    }));
  check_round_trip(r.model);
  EXPECT_TRUE((messages(parse("model { a = process }\nview v { route \"a->z\" }")) == Ids{"Unknown relationship \"a->z\""}));
}

TEST(Parse, ExplicitRelationshipIdsKeepTheirIdentity) {
  const char* source = R"(model {
  a = process
  b = process { port http }
  c = process
  a -> b
  pay = a -> b "pays"
  a -> b
  call = a --> b.http
  shop = system { web = container  db = container  writes = web -> db }
}

view v {
  route "pay" via 10, 20
  route "a->b#2" via 30, 40
}
)";
  auto r = parse(source);
  EXPECT_TRUE(summary(r).empty());
  // Naming one edge does not shift the derived ids of its siblings.
  EXPECT_TRUE((r.model.relationships.keys() == Ids{"a->b", "pay", "a->b#2", "call", "writes"}));
  const Relationship* pay = r.model.relationships.find("pay");
  EXPECT_TRUE(pay->source.element == "a" && pay->target.element == "b");
  EXPECT_TRUE(pay->label == std::optional<std::string>("pays"));
  const Relationship* call = r.model.relationships.find("call");
  EXPECT_TRUE(call->target.port == std::optional<std::string>("http"));
  EXPECT_TRUE(call->line == LineStyle::Dashed);
  // An id declared inside an element is not prefixed with the element's path.
  const Relationship* writes = r.model.relationships.find("writes");
  EXPECT_TRUE(writes->source.element == "shop.web" && writes->target.element == "shop.db");
  const auto& src = r.source_map.relationships.at("pay");
  EXPECT_TRUE(slice(source, src.decl) == "pay = a -> b \"pays\"");
  EXPECT_TRUE(src.id && slice(source, *src.id) == "pay");
  EXPECT_FALSE(r.source_map.relationships.at("a->b").id);
  check_round_trip(r.model);
  EXPECT_TRUE(print(r.model).find("  pay = a -> b \"pays\"\n") != std::string::npos);

  // Moving an endpoint or deleting a parallel edge keeps the id, so the route still applies.
  std::string moved(source);
  moved.replace(moved.find("pay = a -> b"), 12, "pay = c -> b");
  moved.erase(moved.find("  a -> b\n"), 9);
  auto r2 = parse(moved);
  EXPECT_TRUE((summary(r2) == Ids{"13:9 error Unknown relationship \"a->b#2\""}));
  EXPECT_TRUE(r2.model.relationships.find("pay")->source.element == "c");
  EXPECT_TRUE(std::get<RouteHint>(r2.model.views.find("v")->hints[0]).relationship == "pay");
}

TEST(Parse, ExplicitRelationshipIdsAreUniqueAndMayBeKeywords) {
  auto r = parse("model {\n  a = process\n  b = process\n  x = a -> b\n  x = b -> a\n  a = b -> a\n}");
  // Relationship ids and element ids are separate: `a` names both an element and an edge.
  EXPECT_TRUE((summary(r) == Ids{"5:3 error Relationship \"x\" already exists"}));
  EXPECT_TRUE((r.model.relationships.keys() == Ids{"x", "a"}));

  auto k = parse("model {\n  a = process\n  b = process\n  group = a -> b\n  port = a -> b\n  layout = a -> b\n"
                 "  web = container\n  ext = c4.person\n}");
  EXPECT_TRUE((k.model.relationships.keys() == Ids{"group", "port", "layout"}));
  EXPECT_TRUE(k.model.elements.contains("web"));
  EXPECT_TRUE(k.model.elements.find("ext")->kind == "c4.person");
}

TEST(Parse, SequenceViewsMessagesActivationsFramesNotesAndMarkers) {
  const char* source = R"(model {
  user = person "User"
  web = container "Web"
  api = container "API"
}

view login {
  include user web api
  layout sequence direction right
  sequence {
    autonumber
    user -> web "Log in"
    activate web
    web ->> api "POST /login"
    alt "valid" {
      api --> web "200"
      note over web api "token issued"
    } else "invalid" {
      api --> web "401"
      note right of web "show error"
    }
    deactivate web
    divider "Later"
    delay "5 min"
    ref over user web "Logout"
    loop {
      web -> web "poll"
    }
  }
}
)";
  auto r = parse(source);
  EXPECT_TRUE(summary(r).empty());
  const View* v = r.model.views.find("login");
  ASSERT_TRUE(v);
  EXPECT_TRUE(v->autonumber == std::optional<bool>(true));
  EXPECT_TRUE((v->include == Ids{"user", "web", "api"}));
  ASSERT_TRUE(v->sequence);
  const auto& seq = *v->sequence;
  std::vector<std::size_t> kinds;
  for (const auto& i : seq) kinds.push_back(i.index());
  // message, activate, message, frame, deactivate, divider, delay, ref, frame
  EXPECT_TRUE((kinds == std::vector<std::size_t>{0, 1, 0, 6, 1, 3, 4, 5, 6}));
  EXPECT_TRUE((seq[2] == SequenceItem{SequenceMessage{"web", "api", "POST /login", MessageStyle::Async}}));
  const auto& alt = std::get<SequenceFrame>(seq[3]);
  EXPECT_TRUE(alt.frame == FrameKind::Alt);
  ASSERT_TRUE(alt.sections.size() == 2);
  EXPECT_TRUE(alt.sections[0].label == std::optional<std::string>("valid"));
  EXPECT_TRUE((alt.sections[1].items[1] ==
        SequenceItem{SequenceNote{NotePosition::Right, {"web"}, "show error"}}));
  check_round_trip(r.model);
  EXPECT_TRUE((messages(parse("model { a = person }\nview v { sequence { a -> b \"x\" } }")) == Ids{"Unknown element \"b\""}));
}

const char* const kSrc = R"(// Libraries first.
use "c4@^1"
use "aws-icons@^2.1" as aws

model {
  u = person "User" shape c4.person
  fn = component "Handler" shape aws.lambda
}
)";

TEST(UseStatements, RecordsLibrariesAndQualifiedShapes) {
  auto r = parse(kSrc);
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_TRUE((*r.model.libraries == std::vector<LibraryUse>{{"c4", "^1", "c4"}, {"aws-icons", "^2.1", "aws"}}));
  EXPECT_TRUE(r.model.elements.find("fn")->shape == std::optional<std::string>("aws.lambda"));
}

TEST(UseStatements, DefaultsTheRangeAndTheAlias) {
  auto r = parse("use \"@acme/cloud-shapes\"\nmodel {}\n");
  EXPECT_TRUE((*r.model.libraries == std::vector<LibraryUse>{{"@acme/cloud-shapes", "*", "cloud_shapes"}}));
}

TEST(UseStatements, ReportsBadNamesAndDuplicateAliases) {
  auto r = parse("use \"Not A Name\"\nuse \"a@1\" as x\nuse \"b@1\" as x\nmodel {}\n");
  EXPECT_TRUE((summary(r) == Ids{"1:5 error \"Not A Name\" is not a library name",
                          "3:14 error Library alias \"x\" is already used"}));
}

TEST(UseStatements, PrintsUsesFirstWithAsOnlyWhenTheAliasIsNotTheDefault) {
  std::string text = print(parse(kSrc).model);
  EXPECT_TRUE(text.rfind("use \"c4@^1\"\nuse \"aws-icons@^2.1\" as aws\n\n", 0) == 0);
  EXPECT_TRUE(print(parse(text).model) == text);
}

TEST(Parse, ReadsSizesAndTurnsAndPrintsThemBack) {
  auto r = parse(R"(model {
  shop = system "Shop" {
    web = container "Web"
  }
  size = process "A name that is also a keyword"
}

view v of shop {
  include *
  size web 200, 90
  rotate web -30.5
}

view w {
  include shop size
  rotate size 90
  size size 0, 10
}
)");
  const View* v = r.model.views.find("v");
  ASSERT_TRUE(v);
  ASSERT_EQ(v->hints.size(), 2u);
  EXPECT_EQ(v->hints[0], (LayoutHint{SizeHint{"shop.web", 200, 90}}));
  EXPECT_EQ(v->hints[1], (LayoutHint{RotateHint{"shop.web", -30.5}}));
  // `size` is still a name; a size of 0 is an error and is left out.
  const View* w = r.model.views.find("w");
  ASSERT_TRUE(w);
  EXPECT_EQ(w->include, (Ids{"shop", "size"}));
  ASSERT_EQ(w->hints.size(), 1u);
  EXPECT_EQ(w->hints[0], (LayoutHint{RotateHint{"size", 90}}));
  EXPECT_TRUE((summary(r) == Ids{"17:13 error A size must be wider and taller than 0"}));
  EXPECT_EQ(print_hint(v->hints[0]), "size shop.web 200, 90");
  EXPECT_EQ(print_hint(v->hints[1]), "rotate shop.web -30.5");
  auto again = parse(print(r.model));
  EXPECT_EQ(again.model.views.find("v")->hints, v->hints);
}

TEST(BlockStrings, SpanLinesAndDropTheIndentationTheirLinesShare) {
  auto r = parse("model {\n  db = container \"DB\" description \"\"\"\n    Orders and lines.\n\n      Indented \"more\".\n"
                 "    Path C:\\\\x, line\\nbreak\n    \"\"\" tech \"PG\"\n}");
  EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
  const Element* db = r.model.elements.find("db");
  ASSERT_TRUE(db);
  EXPECT_EQ(db->description, "Orders and lines.\n\n  Indented \"more\".\nPath C:\\x, line\nbreak");
  EXPECT_EQ(db->technology, "PG");
}

TEST(BlockStrings, KeepTextOnTheirFirstAndLastLines) {
  EXPECT_EQ(unquote(R"("""one line""")"), "one line");
  EXPECT_EQ(unquote(R"("""first
  second""")"),
            "first\n  second");
  EXPECT_EQ(unquote("\"\"\"\r\n  a\r\n  b\r\n  \"\"\""), "a\nb");
  EXPECT_EQ(unquote(R"("""""")"), "");
  EXPECT_EQ(unquote(R"("""a\"""b""")"), "a\"\"\"b");
}

TEST(BlockStrings, WorkWhereverAStringDoes) {
  auto r = parse("model {\n  a = process \"\"\"\n    Two\n    lines\n    \"\"\"\n}");
  EXPECT_TRUE(r.diagnostics.empty());
  EXPECT_EQ(r.model.elements.find("a")->name, "Two\nlines");
}

TEST(BlockStrings, AnUnclosedOneIsAnErrorAtItsOpening) {
  auto r = parse("model {\n  a = process description \"\"\"\n    never closed\n  b = process\n}");
  ASSERT_FALSE(r.diagnostics.empty());
  EXPECT_EQ(r.diagnostics[0].line, 2);
  EXPECT_EQ(r.diagnostics[0].column, 27);
  EXPECT_EQ(r.diagnostics[0].message, "Unclosed block string: end it with \"\"\"");
  EXPECT_TRUE(r.model.elements.contains("a"));
}

TEST(BlockStrings, PrintMultiLineDescriptionsAndRoundTrip) {
  Model model = parse("model {\n  s = system {\n    db = container description \"Orders.\\n\\nArchived \\\"\\\"\\\\.\"\n  }\n}").model;
  std::string printed = print(model);
  EXPECT_NE(printed.find("    db = container description \"\"\"\n      Orders.\n\n      Archived \"\"\\\\.\n    \"\"\"\n"),
            std::string::npos)
      << printed;
  check_round_trip(model);
}

TEST(BlockStrings, PrintTextABlockCannotHoldWithEscapes) {
  for (const char* text : {"  leading\n  blanks", "a\n   \nb", "has \"\"\" in it\nx", "cr\r\nlf", "\n", "line\n"}) {
    std::string printed = quote_lines(text, "  ");
    EXPECT_EQ(unquote(printed), text) << printed;
  }
  EXPECT_EQ(quote_lines("  leading\n  blanks", ""), quote("  leading\n  blanks"));
  EXPECT_EQ(quote_lines("one line", "  "), "\"one line\"");
  EXPECT_EQ(quote_lines("\nfirst blank\n", ""), "\"\"\"\n\n  first blank\n\n\"\"\"");
  EXPECT_EQ(unquote(quote_lines("\nfirst blank\n", "")), "\nfirst blank\n");
}

TEST(BlockStrings, AClosedOneWhereNoStringFitsIsUnexpectedNotUnclosed) {
  auto r = parse("model {\n  a = \"\"\"x\"\"\"\n  b = process\n}");
  EXPECT_EQ(messages(r), (std::vector<std::string>{"Syntax error: incomplete statement", "Unexpected \"\"\"\"x\"\"\"\""}));
  EXPECT_TRUE(r.model.elements.contains("b"));
}

TEST(BlockStrings, AnUnclosedOneAfterAnotherErrorIsStillReported) {
  auto r = parse("model {\n  a = process tech 5 \"\"\"\n    x\n  b = process\n}\n");
  auto is_unclosed = [](const Diagnostic& d) { return d.message == "Unclosed block string: end it with \"\"\""; };
  EXPECT_EQ(std::ranges::count_if(r.diagnostics, is_unclosed), 1) << testing::PrintToString(messages(r));
  auto unclosed = std::ranges::find_if(r.diagnostics, is_unclosed);
  ASSERT_NE(unclosed, r.diagnostics.end());
  EXPECT_EQ(unclosed->line, 2);
  EXPECT_EQ(unclosed->column, 22);
  // The error before it stops where the string opens instead of running to the end.
  for (const auto& d : r.diagnostics)
    if (d.message == "Unexpected \"5\"") EXPECT_LE(d.span.to, unclosed->span.from);
}

TEST(BlockStrings, AnEscapedStrayByteDoesNotSwallowTheClosingQuotes) {
  auto r = parse("model {\n  a = process description \"\"\"x\\\xE2\"\"\"\n  b = process\n}");
  EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
  EXPECT_TRUE(r.model.elements.contains("b"));
}
