// The MCP server (host/mcp.hpp) as a client sees it: the JSON-RPC handshake, the tool list and
// every tool, inline documents and files, and the errors an agent can recover from.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "diadraw/io/codec.hpp"
#include "diadraw/base/text.hpp"
#include "host/files.hpp"
#include "host/mcp.hpp"

using namespace dd;
using namespace dd::host;

namespace {

namespace fs = std::filesystem;

constexpr const char* kDoc = R"(model {
  user = person "User"
  app = system "App" {
    web = container "Web" tech "React"
    db = container "DB" shape cylinder
  }
  user -> app.web "uses"
  app.web -> app.db "reads"
}
view context {
  include *
}
view containers of app {
  include *
  layout layered direction right
}
)";

Json request(int id, const char* method, Json params = Json::object()) {
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("method", method);
  m.set("params", std::move(params));
  return m;
}

class Mcp : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("diadraw-mcp-test-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    server_.emplace(McpOptions{path_utf8(dir_), "9.9.9"});
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  /// The `result` of tools/call `name`.
  Json call(const char* name, Json args) {
    Json params = Json::object();
    params.set("name", name);
    params.set("arguments", std::move(args));
    auto reply = server_->handle(request(1, "tools/call", std::move(params)));
    EXPECT_TRUE(reply);
    const Json* result = reply ? reply->get("result") : nullptr;
    EXPECT_TRUE(result) << (reply ? reply->dump() : "");
    return result ? *result : Json::object();
  }

  static bool is_error(const Json& result) {
    const Json* e = result.get("isError");
    return e && e->as_bool();
  }
  /// The text of every text content item, joined.
  static std::string text(const Json& result) {
    std::string out;
    for (const auto& c : result.get("content")->as_array())
      if (c.get("type")->as_string() == "text") out += c.get("text")->as_string();
    return out;
  }
  static Json args(std::initializer_list<std::pair<const char*, Json>> items) {
    Json a = Json::object();
    for (const auto& [k, v] : items) a.set(k, v);
    return a;
  }

  fs::path dir_;
  std::optional<McpServer> server_;
};

TEST_F(Mcp, InitializeAgreesOnAProtocolVersionAndOffersTools) {
  Json params = Json::object();
  params.set("protocolVersion", "2024-11-05");
  auto reply = server_->handle(request(7, "initialize", params));
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply->get("id")->as_number(), 7);
  const Json& result = *reply->get("result");
  EXPECT_EQ(result.get("protocolVersion")->as_string(), "2024-11-05");
  EXPECT_TRUE(result.get("capabilities")->get("tools"));
  EXPECT_EQ(result.get("serverInfo")->get("name")->as_string(), "diadraw");
  EXPECT_EQ(result.get("serverInfo")->get("version")->as_string(), "9.9.9");
  EXPECT_NE(result.get("instructions")->as_string().find("model {"), std::string::npos);

  // A version the server does not know: it answers with its newest.
  params.set("protocolVersion", "1999-01-01");
  reply = server_->handle(request(8, "initialize", params));
  EXPECT_EQ(reply->get("result")->get("protocolVersion")->as_string(), McpServer::kProtocolVersions[0]);
}

TEST_F(Mcp, NotificationsAndResponsesGetNoReply) {
  Json n = Json::object();
  n.set("jsonrpc", "2.0");
  n.set("method", "notifications/initialized");
  EXPECT_FALSE(server_->handle(n));
  Json r = Json::object();
  r.set("jsonrpc", "2.0");
  r.set("id", 3);
  r.set("result", Json::object());
  EXPECT_FALSE(server_->handle(r));
}

TEST_F(Mcp, ReportsProtocolErrors) {
  auto unknown = server_->handle(request(2, "nope"));
  EXPECT_EQ(unknown->get("error")->get("code")->as_number(), -32601);
  Json params = Json::object();
  params.set("name", "nope");
  auto tool = server_->handle(request(3, "tools/call", params));
  EXPECT_EQ(tool->get("error")->get("code")->as_number(), -32602);
  auto not_object = server_->handle(Json::array());
  EXPECT_EQ(not_object->get("error")->get("code")->as_number(), -32600);
  Json parse = McpServer::parse_error("bad");
  EXPECT_TRUE(parse.get("id")->is_null());
  EXPECT_EQ(parse.get("error")->get("code")->as_number(), -32700);
  EXPECT_TRUE(server_->handle(request(4, "ping"))->get("result")->is_object());
}

TEST_F(Mcp, ListsItsTools) {
  auto reply = server_->handle(request(1, "tools/list"));
  std::vector<std::string> names;
  for (const auto& t : reply->get("result")->get("tools")->as_array()) {
    names.push_back(t.get("name")->as_string());
    EXPECT_EQ(t.get("inputSchema")->get("type")->as_string(), "object");
    EXPECT_FALSE(t.get("description")->as_string().empty());
  }
  EXPECT_EQ(names, (std::vector<std::string>{"check", "render", "format", "import", "reference", "examples"}));
}

TEST_F(Mcp, ChecksADocument) {
  Json ok = call("check", args({{"source", kDoc}}));
  EXPECT_FALSE(is_error(ok));
  EXPECT_EQ(text(ok), "ok; views: context, containers\n");

  Json bad = call("check", args({{"source", "model {\n  a = person \"A\"\n  a -> b\n}\n"}}));
  EXPECT_TRUE(is_error(bad));
  EXPECT_NE(text(bad).find("3:8: error: "), std::string::npos) << text(bad);
  EXPECT_NE(text(bad).find("1 error"), std::string::npos);

  Json no_view = call("check", args({{"source", "model { a = person \"A\" }"}}));
  EXPECT_NE(text(no_view).find("no view declared"), std::string::npos);
}

TEST_F(Mcp, ReadsDocumentsFromFilesRelativeToItsFolder) {
  std::ofstream(dir_ / "doc.draw") << kDoc;
  Json ok = call("check", args({{"path", "doc.draw"}}));
  EXPECT_FALSE(is_error(ok)) << text(ok);
  Json missing = call("check", args({{"path", "missing.draw"}}));
  EXPECT_TRUE(is_error(missing));
  EXPECT_NE(text(missing).find("cannot read"), std::string::npos);
  Json both = call("check", args({{"path", "doc.draw"}, {"source", kDoc}}));
  EXPECT_TRUE(is_error(both));
  Json neither = call("check", Json::object());
  EXPECT_TRUE(is_error(neither));
}

TEST_F(Mcp, RendersAViewAsAnImage) {
  Json result = call("render", args({{"source", kDoc}, {"view", "containers"}}));
  ASSERT_FALSE(is_error(result)) << text(result);
  EXPECT_NE(text(result).find("view \"containers\""), std::string::npos);
  EXPECT_NE(text(result).find("other views: context"), std::string::npos);
  const Json* image = nullptr;
  for (const auto& c : result.get("content")->as_array())
    if (c.get("type")->as_string() == "image") image = &c;
  if (!image) GTEST_SKIP() << "no PNG backend in this build";
  EXPECT_EQ(image->get("mimeType")->as_string(), "image/png");
  auto png = io::base64_decode(image->get("data")->as_string());
  ASSERT_TRUE(png);
  EXPECT_EQ(png->substr(1, 3), "PNG");
}

TEST_F(Mcp, RendersSvgAndWritesFiles) {
  Json svg = call("render", args({{"source", kDoc}, {"format", "svg"}, {"output", "out/../diagram.svg"}}));
  ASSERT_FALSE(is_error(svg)) << text(svg);
  EXPECT_NE(text(svg).find("<svg"), std::string::npos);
  auto written = read_file(path_utf8(dir_ / "diagram.svg"));
  ASSERT_TRUE(written);
  EXPECT_EQ(written->rfind("<svg", 0), 0u);

  Json none = call("render", args({{"source", kDoc}, {"format", "none"}, {"output", path_utf8(dir_ / "d.pdf")}}));
  ASSERT_FALSE(is_error(none)) << text(none);
  EXPECT_EQ(none.get("content")->as_array().size(), 1u);
  EXPECT_EQ(read_file(path_utf8(dir_ / "d.pdf")).value_or("").rfind("%PDF", 0), 0u);
}

TEST_F(Mcp, RefusesWhatItCannotRender) {
  EXPECT_TRUE(is_error(call("render", args({{"source", kDoc}, {"view", "nope"}}))));
  EXPECT_NE(text(call("render", args({{"source", kDoc}, {"view", "nope"}}))).find("views: context, containers"),
            std::string::npos);
  EXPECT_TRUE(is_error(call("render", args({{"source", "model { a -> }"}}))));
  EXPECT_TRUE(is_error(call("render", args({{"source", kDoc}, {"format", "gif"}}))));
  EXPECT_TRUE(is_error(call("render", args({{"source", kDoc}, {"output", "x.gif"}}))));
  EXPECT_TRUE(is_error(call("render", args({{"source", kDoc}, {"scale", 100}}))));
  EXPECT_TRUE(is_error(call("render", args({{"source", kDoc}, {"scale", "2"}}))));
}

TEST_F(Mcp, FormatsADocument) {
  Json result = call("format", args({{"source", "model{a=person \"A\"}"}}));
  EXPECT_EQ(text(result), "model {\n  a = person \"A\"\n}\n");
  EXPECT_TRUE(is_error(call("format", args({{"source", "model { a -> }"}}))));
  EXPECT_TRUE(is_error(call("format", args({{"source", "model {}"}, {"write", true}}))));

  std::ofstream(dir_ / "messy.draw") << "model{a=person \"A\"}";
  Json written = call("format", args({{"path", "messy.draw"}, {"write", true}}));
  ASSERT_FALSE(is_error(written)) << text(written);
  EXPECT_EQ(read_file(path_utf8(dir_ / "messy.draw")), "model {\n  a = person \"A\"\n}\n");
}

TEST_F(Mcp, ImportsOtherFormats) {
  Json result = call("import", args({{"source", "flowchart LR\n  A[Start] --> B[Done]\n"}, {"from", "mermaid"}}));
  ASSERT_FALSE(is_error(result)) << text(result);
  EXPECT_NE(text(result).find("A = process \"Start\""), std::string::npos) << text(result);
  // The format is guessed when not given.
  EXPECT_FALSE(is_error(call("import", args({{"source", "flowchart LR\n  A --> B\n"}}))));
  EXPECT_TRUE(is_error(call("import", args({{"source", "x"}, {"from", "visio"}}))));

  Json written = call("import", args({{"source", "flowchart LR\n  A --> B\n"}, {"output", "imported.draw"}}));
  ASSERT_FALSE(is_error(written));
  EXPECT_TRUE(read_file(path_utf8(dir_ / "imported.draw")));
}

TEST_F(Mcp, ListsWhatADocumentCanUse) {
  std::string all = text(call("reference", Json::object()));
  EXPECT_NE(all.find("Element kinds: person system container"), std::string::npos) << all;
  EXPECT_NE(all.find("cylinder"), std::string::npos);
  EXPECT_NE(all.find("Themes (`theme …`): default dark mono"), std::string::npos);
  EXPECT_NE(all.find("sequence"), std::string::npos);
  EXPECT_NE(all.find("  c4: "), std::string::npos);

  std::string c4 = text(call("reference", args({{"library", "c4"}})));
  EXPECT_EQ(c4.rfind("use \"c4@^1\"\n", 0), 0u) << c4;
  EXPECT_NE(c4.find("shape c4.person"), std::string::npos);
  EXPECT_EQ(text(call("reference", args({{"library", "c4@^1"}}))), c4);  // a `use` spec works too

  std::string search = text(call("reference", args({{"library", "c4"}, {"search", "QUEUE"}})));
  EXPECT_NE(search.find("queue"), std::string::npos);
  EXPECT_EQ(search.find("c4.person"), std::string::npos);

  EXPECT_TRUE(is_error(call("reference", args({{"library", "no-such-library"}}))));
}

TEST_F(Mcp, ReturnsTheExamples) {
  std::string list = text(call("examples", Json::object()));
  EXPECT_NE(list.find("c4-02-banking"), std::string::npos);
  std::string banking = text(call("examples", args({{"name", "c4-02-banking.draw"}})));
  EXPECT_NE(banking.find("model {"), std::string::npos);
  EXPECT_TRUE(is_error(call("examples", args({{"name", "nope"}}))));
}

TEST_F(Mcp, RefusesAFolderAsADocument) {
  Json dot = call("check", args({{"path", "."}}));
  EXPECT_TRUE(is_error(dot)) << text(dot);
  EXPECT_TRUE(is_error(call("check", args({{"path", ""}}))));
}

TEST_F(Mcp, WritesIntoNewFolders) {
  Json result = call("render", args({{"source", kDoc}, {"format", "none"}, {"output", "docs/diagrams/x.svg"}}));
  ASSERT_FALSE(is_error(result)) << text(result);
  EXPECT_TRUE(read_file(path_utf8(dir_ / "docs" / "diagrams" / "x.svg")));
}

TEST_F(Mcp, HandlesNonAsciiPaths) {
  const std::string name = "Zo\xC3\xAB diagram.draw";
  ASSERT_TRUE(write_file(path_utf8(dir_) + "/" + name, kDoc));
  Json result = call("check", args({{"path", name}}));
  EXPECT_FALSE(is_error(result)) << text(result);
}

#ifndef _WIN32
TEST_F(Mcp, KeepsPosixFileNamesThatAreNotUtf8) {
  // A Latin-1 name is just bytes on Linux and macOS: it must open and be written as given.
  const std::string name = "caf\xE9.draw";
  ASSERT_TRUE(write_file(path_utf8(dir_) + "/" + name, kDoc));
  EXPECT_FALSE(is_error(call("check", args({{"path", name}}))));
  Json written = call("render", args({{"path", name}, {"format", "none"}, {"output", "out\xE9.svg"}}));
  ASSERT_FALSE(is_error(written)) << text(written);
  EXPECT_TRUE(fs::exists(dir_ / "out\xE9.svg"));
}

TEST_F(Mcp, DoesNotFollowLinkedFoldersInAnInclude) {
  // Two links back to the folder itself would make the walk endless if links were followed.
  fs::create_directories(dir_ / "parts");
  ASSERT_TRUE(write_file(path_utf8(dir_ / "parts" / "u.dsl"), "u = person \"U\"\n"));
  fs::create_directory_symlink(".", dir_ / "parts" / "a");
  fs::create_directory_symlink(".", dir_ / "parts" / "b");
  ASSERT_TRUE(write_file(path_utf8(dir_ / "w.dsl"), "workspace {\n  model {\n    !include parts\n  }\n}\n"));
  Json result = call("import", args({{"path", "w.dsl"}}));
  ASSERT_FALSE(is_error(result)) << text(result);
  EXPECT_NE(text(result).find("u = person"), std::string::npos) << text(result);
}
#endif

TEST_F(Mcp, RepliesInValidUtf8WhateverTheFileHolds) {
  ASSERT_TRUE(write_file(path_utf8(dir_ / "latin1.draw"), "model {\n  a = person \"Zo\xEB\"\n}\n"));
  Json params = Json::object();
  params.set("name", "format");
  params.set("arguments", args({{"path", "latin1.draw"}}));
  auto reply = server_->handle(request(1, "tools/call", params));
  ASSERT_TRUE(reply);
  std::string line = McpServer::serialize(*reply);
  EXPECT_EQ(to_valid_utf8(line), line);
  EXPECT_NE(line.find("Zo\xEF\xBF\xBD"), std::string::npos) << line;
  EXPECT_TRUE(Json::parse(line));
}

TEST_F(Mcp, TakesALibraryNameAsANameOnly) {
  Json result = call("reference", args({{"library", "c4\" as x\nmodel { "}}));
  EXPECT_TRUE(is_error(result));
  EXPECT_EQ(text(result).rfind("no library \"c4\" as x", 0), 0u) << text(result);
}

TEST_F(Mcp, IncludesAFolderDepthFirstInNameOrder) {
  // `people/u.dsl` must come before `people-rel.dsl`, which uses `u` (a plain string sort puts
  // `-` before `/`).
  fs::create_directories(dir_ / "parts" / "people");
  ASSERT_TRUE(write_file(path_utf8(dir_ / "parts" / "people" / "u.dsl"), "u = person \"U\"\n"));
  ASSERT_TRUE(write_file(path_utf8(dir_ / "parts" / "people-rel.dsl"), "u -> s \"uses\"\n"));
  ASSERT_TRUE(write_file(path_utf8(dir_ / "w.dsl"),
                         "workspace {\n  model {\n    s = softwareSystem \"S\"\n    !include parts\n  }\n}\n"));
  Json result = call("import", args({{"path", "w.dsl"}}));
  ASSERT_FALSE(is_error(result)) << text(result);
  EXPECT_EQ(text(result).find("dropped"), std::string::npos) << text(result);
  EXPECT_NE(text(result).find("u -> s"), std::string::npos) << text(result);
}

}  // namespace
