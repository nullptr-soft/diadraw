// Element kinds that libraries add (`kinds` in a manifest, `x = alias.kind` in a document): from
// the manifest through parsing, rendering, the editor and the language server.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "diadraw/dsl/highlight.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/layout/layout.hpp"
#include "diadraw/library/library.hpp"
#include "diadraw/lsp/features.hpp"
#include "diadraw/lsp/server.hpp"
#include "helpers.hpp"

using namespace dd;
using namespace dd::library;

namespace {

const char* const kBundle = R"({"format":"diadraw-library","formatVersion":1,"name":"acme","version":"1.0.0",
  "license":"MIT",
  "shapes":{
    "gear":{"svg":"<circle cx=\"5\" cy=\"5\" r=\"4\"/>","viewBox":"0 0 10 10","label":"Gear","kind":"service"},
    "box":{"base":"rect","kind":"component"}},
  "kinds":{
    "service":{"label":"Service","shape":"gear","description":"A deployable service","keywords":["svc"]},
    "queue":{"base":"hexagon","fill":"#fde68a","stroke":"#b45309","fontColor":"#451a03","stereotype":"queue"},
    "plain":{}}})";

std::shared_ptr<const Json> acme() { return std::make_shared<const Json>(*parse_bundle(kBundle)); }

LibrarySet acme_set(const std::string& alias = "acme") {
  auto r = resolve_uses({{"acme", "^1", alias}}, {memory_source({acme()})});
  EXPECT_TRUE(r.unresolved.empty());
  return r.set;
}

const char* const kDoc = R"(use "acme@^1"

model {
  api = acme.service "Orders API"
  jobs = acme.queue "Jobs" {
    worker = component "Worker"
  }
  q = acme.queue "Events"
  odd = acme.nope
  api -> q
}
)";

std::vector<std::string> messages(const std::vector<ManifestIssue>& issues) {
  std::vector<std::string> out;
  for (const auto& i : issues) out.push_back((i.error ? "E " : "W ") + (i.path ? *i.path + ": " : "") + i.message);
  return out;
}

}  // namespace

TEST(LibraryKinds, ManifestsAreChecked) {
  auto m = Json::parse(R"({"name":"k","version":"1.0.0","license":"MIT","shapes":{"a":{"base":"rect"}},
    "kinds":{"ok":{"shape":"a"},"9x":{"base":"rect"},"lost":{"shape":"missing"},"far":{"shape":"other.thing"},
             "bare":{},"num":{"base":"rect","fill":3},"str":"no"}})");
  EXPECT_EQ(messages(validate_manifest(*m)),
            (std::vector<std::string>{
                "E kinds.9x: Kind name \"9x\" is not an identifier",
                "E kinds.lost.shape: Shape \"missing\" is not a shape or composite of this library",
                "W kinds.far.shape: Shape \"other.thing\" refers to a library that is not a dependency",
                "W kinds.bare: Kind has neither shape nor base; its elements draw as boxes",
                "E kinds.num.fill: fill must be a string",
                "E kinds.str: A kind is an object",
            }));
}

TEST(LibraryKinds, ResolveToAShapeAndColours) {
  auto set = acme_set("a");
  auto service = set.kind("a.service");
  ASSERT_TRUE(service);
  EXPECT_EQ(service->shape, std::optional<std::string>("a.gear"));
  EXPECT_EQ(service->label, "Service");
  auto queue = set.kind("a.queue");
  ASSERT_TRUE(queue);
  EXPECT_EQ(queue->shape, std::optional<std::string>("hexagon"));
  EXPECT_EQ(queue->fill, std::optional<std::string>("#fde68a"));
  EXPECT_EQ(queue->text, std::optional<std::string>("#451a03"));
  EXPECT_EQ(set.kind("a.plain")->label, "Plain");
  EXPECT_FALSE(set.kind("a.nope"));
  EXPECT_FALSE(set.kind("service"));
  EXPECT_FALSE(set.kind("b.service"));
  // A shape's `kind` names the library's kind when it has one, else a built-in kind.
  EXPECT_EQ(set.art("a.gear")->kind, std::optional<std::string>("a.service"));
  EXPECT_EQ(set.art("a.box")->kind, std::optional<std::string>("component"));
}

TEST(LibraryKinds, ShowInThePalette) {
  auto palette = acme_set().palette();
  std::vector<std::string> kinds;
  for (const auto& p : palette)
    if (p.type == "kind") kinds.push_back(p.ref + " " + p.label + " " + p.category + " " + p.kind.value_or("-"));
  EXPECT_EQ(kinds, (std::vector<std::string>{"acme.service Service Kinds acme.service", "acme.queue Queue Kinds acme.queue",
                                             "acme.plain Plain Kinds acme.plain"}));
  auto service =
      std::find_if(palette.begin(), palette.end(), [](const PaletteItem& p) { return p.ref == "acme.service"; });
  ASSERT_TRUE(service->art && service->art->shape.body);
  EXPECT_EQ(to_json(*service).get("type")->as_string(), "kind");
}

TEST(LibraryKinds, ParseAsQualifiedKindsAndRoundTrip) {
  auto r = dsl::parse(kDoc);
  EXPECT_TRUE(r.diagnostics.empty()) << r.diagnostics[0].message;
  EXPECT_EQ(r.model.elements.find("api")->kind, "acme.service");
  auto& src = r.source_map.elements.at("api");
  EXPECT_EQ(std::string(kDoc).substr(src.kind.from, src.kind.to - src.kind.from), "acme.service");
  std::string printed = dsl::print(r.model);
  EXPECT_NE(printed.find("api = acme.service \"Orders API\""), std::string::npos);
  EXPECT_TRUE(dsl::parse(printed).model == r.model);

  auto missing = dsl::parse("model {\n  a = acme.service\n  b = acme.\n}\n");
  ASSERT_GE(missing.diagnostics.size(), 2u);
  EXPECT_EQ(missing.diagnostics[0].message, "No library is used as \"acme\"; add `use` for it");
  EXPECT_EQ(missing.diagnostics.back().severity, dsl::Severity::Error);  // `acme.` is incomplete
}

TEST(LibraryKinds, UnknownKindsOfAUsedLibraryAreReported) {
  auto r = dsl::parse(kDoc);
  auto unknown = unknown_references(r.model, acme_set());
  ASSERT_EQ(unknown.size(), 1u);
  EXPECT_EQ(unknown[0].element, "odd");
  EXPECT_EQ(unknown[0].message(), "No kind \"acme.nope\" in its library");
  // A library the set does not have is reported through its `use`, not per element.
  EXPECT_TRUE(unknown_references(r.model, LibrarySet{}).empty());
}

TEST(LibraryKinds, DrawWithTheirShapeColoursAndStereotype) {
  auto set = acme_set();
  auto art = set.renderer();
  auto r = dsl::parse(kDoc);
  Model model = with_default_view(r.model);
  auto view = test::ok(resolve_view(model, model.views.keys().front()));
  auto geometry = test::ok(layout(view, create_measure(default_theme(), art.shapes, art.kinds)));
  RenderOptions options;
  options.shapes = art.shapes;
  options.kinds = art.kinds;
  std::string svg = render_svg(view, geometry, options);
  auto node = [&](const std::string& id) { return test::item_svg(svg, "dd-node", id); };
  EXPECT_NE(node("api").find("<circle"), std::string::npos);  // the kind's library drawing
  EXPECT_NE(node("q").find("<polygon"), std::string::npos);   // its base shape, a hexagon
  EXPECT_NE(node("q").find("fill=\"#fde68a\""), std::string::npos);
  EXPECT_NE(node("q").find("#451a03"), std::string::npos);
  EXPECT_NE(node("jobs").find("[queue]"), std::string::npos);  // a boundary with the stereotype
  EXPECT_EQ(node("odd").find("#fde68a"), std::string::npos);
  // Without the kinds, the same document still renders, as boxes.
  std::string plain = render_svg(view, test::ok(layout(view, create_measure())));
  EXPECT_EQ(plain.find("#fde68a"), std::string::npos);
}

TEST(LibraryKinds, AnElementWithItsOwnShapeKeepsIt) {
  auto art = acme_set().renderer();
  Element e;
  e.id = "x";
  e.kind = "acme.queue";
  EXPECT_EQ(with_kind(e, art.kinds).shape, std::optional<std::string>("hexagon"));
  e.shape = "cylinder";
  EXPECT_EQ(with_kind(e, art.kinds).shape, std::optional<std::string>("cylinder"));
}

TEST(LibraryKinds, TheEditorCreatesThemAndRendersThem) {
  auto bundle = acme();
  editor::Session session("use \"acme@^1\"\nmodel {\n}\n", [bundle](const Model& model) -> LibraryArt {
    return resolve_uses(model.libraries.value_or(std::vector<LibraryUse>{}), {memory_source({bundle})}).set.renderer();
  });
  auto [id, update] = test::ok(session.create("acme.queue"));
  EXPECT_EQ(id, "queue1");
  EXPECT_NE(session.source().find("queue1 = acme.queue \"New queue\""), std::string::npos) << session.source();
  auto rendered = test::ok(session.render(session.model().views.keys().front()));
  EXPECT_NE(rendered.scene.content.find("#fde68a"), std::string::npos);
}

TEST(LibraryKinds, HighlightAsOneKindName) {
  std::vector<std::string> out;
  std::string_view text = "a = acme.service \"A\"";
  for (const auto& t : dsl::highlight(text))
    out.push_back(std::string(text.substr(t.from, t.to - t.from)) + ":" + std::string(dsl::token_kind_name(t.kind)));
  EXPECT_EQ(out, (std::vector<std::string>{"a:definition", "=:operator", "acme.service:typeName", "\"A\":string"}));
}

TEST(LibraryKinds, CompleteAfterEqualsAndAlias) {
  lsp::LibraryContext context{acme_set(), {}};
  auto labels = [&](std::string_view text) {
    auto r = dsl::parse(text);
    std::vector<std::string> out;
    for (const auto& c : lsp::completions(text, text.size(), r, &context)) out.push_back(c.label + ":" + c.kind);
    return out;
  };
  auto after_equals = labels("use \"acme@^1\"\nmodel {\n  a = ");
  EXPECT_NE(std::find(after_equals.begin(), after_equals.end(), "container:kind"), after_equals.end());
  EXPECT_NE(std::find(after_equals.begin(), after_equals.end(), "acme.service:kind"), after_equals.end());
  EXPECT_EQ(labels("use \"acme@^1\"\nmodel {\n  a = acme."),
            (std::vector<std::string>{"acme.service:kind", "acme.queue:kind", "acme.plain:kind"}));
  // Kinds are not shapes.
  auto shapes = labels("use \"acme@^1\"\nmodel {\n  a = component shape acme.");
  EXPECT_EQ(std::count(shapes.begin(), shapes.end(), "acme.service:value"), 0);
}

TEST(LibraryKinds, TheLanguageServerWarnsAtTheKind) {
  std::vector<Json> sent;
  auto bundle = acme();
  lsp::Server server([&](const Json& m) { sent.push_back(m); }, lsp::memory_resolver({memory_source({bundle})}));
  Json open = Json::object();
  open.set("jsonrpc", Json("2.0"));
  open.set("method", Json("textDocument/didOpen"));
  Json doc = Json::object();
  doc.set("uri", Json("untitled:k"));
  doc.set("version", Json(1.0));
  doc.set("text", Json(std::string(kDoc)));
  Json params = Json::object();
  params.set("textDocument", doc);
  open.set("params", params);
  server.handle(open);
  const auto& diagnostics = sent.back().get("params")->get("diagnostics")->as_array();
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].get("message")->as_string(), "No kind \"acme.nope\" in its library");
  EXPECT_EQ(diagnostics[0].get("range")->get("start")->get("line")->as_number(), 8);
  EXPECT_EQ(diagnostics[0].get("range")->get("start")->get("character")->as_number(), 8);
}
