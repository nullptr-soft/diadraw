// The language server without a transport, and offline library resolution over a fake disk.

#include <gtest/gtest.h>

#include <map>
#include <memory>

#include "diadraw/diadraw.h"
#include "diadraw/library/disk.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/lsp/server.hpp"

using namespace dd;

namespace {

/// Files in memory, with '/' paths.
class FakeFileSystem final : public library::FileSystem {
 public:
  std::map<std::string, std::string> files;
  std::map<std::string, std::string> vars;

  std::optional<std::string> read(const std::string& path) const override {
    auto it = files.find(path);
    return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> out;
    std::string prefix = dir + "/";
    for (const auto& [path, _] : files)
      if (path.rfind(prefix, 0) == 0) {
        std::string name = path.substr(prefix.size());
        name = name.substr(0, name.find('/'));
        if (out.empty() || out.back() != name) out.push_back(name);
      }
    return out;
  }
  std::string join(const std::string& dir, const std::string& name) const override { return dir + "/" + name; }
  std::string parent(const std::string& path) const override {
    auto slash = path.rfind('/');
    return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
  }
  std::string env(const std::string& name) const override {
    auto it = vars.find(name);
    return it == vars.end() ? "" : it->second;
  }
};

std::string bundle(const std::string& name, const std::string& version) {
  return R"({"format":"diadraw-library","formatVersion":1,"name":")" + name + R"(","version":")" + version +
         R"(","license":"MIT","shapes":{"box":{"svg":"<rect width=\"10\" height=\"10\"/>","viewBox":"0 0 10 10"}}})";
}

Json message(const std::string& text) { return *Json::parse(text); }

}  // namespace

TEST(DiskLibraries, ResolvesThroughTheLockfileAndTheCache) {
  auto fs = std::make_shared<FakeFileSystem>();
  fs->vars["HOME"] = "/home/me";
  fs->files["/home/me/.cache/diadraw/libraries/acme/1.0.0.json"] = bundle("acme", "1.0.0");
  fs->files["/home/me/.cache/diadraw/libraries/acme/1.2.0.json"] = bundle("acme", "1.2.0");
  fs->files["/work/diadraw-lock.json"] =
      R"({"lockfileVersion":1,"libraries":{"acme@^1":{"version":"1.0.0","resolved":"x"}}})";
  library::DiskLibraries disk(fs);

  EXPECT_EQ(disk.cache_dir(), "/home/me/.cache/diadraw/libraries");
  EXPECT_EQ(disk.find_lockfile("/work/docs/deep"), "/work/diadraw-lock.json");
  auto listed = disk.list_cache(disk.cache_dir());
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].second, (std::vector<std::string>{"1.2.0", "1.0.0"}));

  std::vector<LibraryUse> uses{{"acme", "^1", "acme"}, {"missing", "*", "missing"}};
  auto pinned = disk.resolve_for_file(uses, "/work/docs/a.draw");
  ASSERT_EQ(pinned.set.libraries().size(), 1u);
  EXPECT_EQ(pinned.set.libraries()[0].bundle->get("version")->as_string(), "1.0.0");
  ASSERT_EQ(pinned.unresolved.size(), 1u);
  auto newest = disk.resolve_for_file(uses, "/elsewhere/a.draw");
  EXPECT_EQ(newest.set.libraries()[0].bundle->get("version")->as_string(), "1.2.0");
}

TEST(LanguageServer, AnswersAndPublishesThroughItsCallback) {
  std::vector<Json> sent;
  lsp::Server server([&](const Json& m) { sent.push_back(m); });

  server.handle(message(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_TRUE(sent[0].get("result")->get("capabilities")->get("hoverProvider")->as_bool());
  EXPECT_EQ(sent[0].get("result")->get("serverInfo")->get("version")->as_string(), dd_version());

  server.handle(message(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"untitled:a","version":1,"text":"use \"nope@^1\" as n\nmodel {\n  a = process \"Ä\"\n  b = process\n  a -> b\n}\n"}}})"));
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[1].get("method")->as_string(), "textDocument/publishDiagnostics");
  const auto& diagnostics = sent[1].get("params")->get("diagnostics")->as_array();
  ASSERT_EQ(diagnostics.size(), 1u);  // the missing library
  EXPECT_EQ(diagnostics[0].get("severity")->as_number(), 2);

  server.handle(message(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":"untitled:a"},"position":{"line":4,"character":8}}})"));
  const Json* range = sent.back().get("result")->get("range");
  ASSERT_NE(range, nullptr);
  EXPECT_EQ(range->get("start")->get("line")->as_number(), 3);

  server.handle(message(R"({"jsonrpc":"2.0","id":3,"method":"nope"})"));
  EXPECT_EQ(sent.back().get("error")->get("code")->as_number(), -32601);

  EXPECT_FALSE(server.exit_code());
  server.handle(message(R"({"jsonrpc":"2.0","id":4,"method":"shutdown"})"));
  server.handle(message(R"({"jsonrpc":"2.0","method":"exit"})"));
  EXPECT_EQ(server.exit_code(), 0);
}

TEST(LanguageServer, ReadsLibrariesNextToTheDocument) {
  auto fs = std::make_shared<FakeFileSystem>();
  fs->vars["DIADRAW_CACHE"] = "/cache";
  fs->files["/cache/acme/1.0.0.json"] = bundle("acme", "1.0.0");
  std::vector<Json> sent;
  lsp::Server server([&](const Json& m) { sent.push_back(m); },
                     lsp::disk_resolver(std::make_shared<library::DiskLibraries>(fs)));
  server.handle(message(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///w/a%20b.draw","version":1,"text":"use \"acme@^1\" as acme\nmodel {\n  a = component shape acme.box\n  b = component shape acme.nope\n}\n"}}})"));
  const auto& diagnostics = sent.back().get("params")->get("diagnostics")->as_array();
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_NE(diagnostics[0].get("message")->as_string().find("acme.nope"), std::string::npos);
  EXPECT_EQ(lsp::path_of_uri("file:///w/a%20b.draw"), "/w/a b.draw");
}

TEST(LanguageServer, RenamesAnElementAndEveryReference) {
  std::vector<Json> sent;
  lsp::Server server([&](const Json& m) { sent.push_back(m); });
  server.handle(message(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
  EXPECT_TRUE(sent[0].get("result")->get("capabilities")->get("renameProvider")->get("prepareProvider")->as_bool());
  // model {            line 0
  //   shop = system {  line 1
  //     web = container
  //     db = container
  //     web -> db      line 4
  //   }
  //   shop.web -> shop.db   line 6
  // }
  server.handle(message(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"untitled:r","version":1,"text":"model {\n  shop = system {\n    web = container\n    db = container\n    web -> db\n  }\n  shop.web -> shop.db\n}\n"}}})"));

  // On `web` in `shop.web`: the part under the cursor.
  server.handle(message(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"untitled:r"},"position":{"line":6,"character":8}}})"));
  const Json* prepared = sent.back().get("result");
  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(prepared->get("placeholder")->as_string(), "web");
  EXPECT_EQ(prepared->get("range")->get("start")->get("character")->as_number(), 7);

  server.handle(message(
      R"({"jsonrpc":"2.0","id":3,"method":"textDocument/rename","params":{"textDocument":{"uri":"untitled:r"},"position":{"line":6,"character":8},"newName":"store"}})"));
  const Json* changes = sent.back().get("result")->get("changes");
  ASSERT_NE(changes, nullptr);
  const auto& edits = changes->get("untitled:r")->as_array();
  ASSERT_EQ(edits.size(), 3u);  // the declaration, `web -> db` and `shop.web -> …`
  for (const auto& e : edits) EXPECT_EQ(e.get("newText")->as_string(), "store");
  EXPECT_EQ(edits[0].get("range")->get("start")->get("line")->as_number(), 2);

  // A taken name is refused with a message.
  server.handle(message(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/rename","params":{"textDocument":{"uri":"untitled:r"},"position":{"line":2,"character":5},"newName":"db"}})"));
  EXPECT_EQ(sent.back().get("error")->get("code")->as_number(), -32803);
  // Keywords are not names.
  server.handle(message(
      R"({"jsonrpc":"2.0","id":5,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"untitled:r"},"position":{"line":1,"character":11}}})"));
  EXPECT_NE(sent.back().get("error"), nullptr);
}

TEST(LanguageFeatures, RenameTargetsAreElementNamesNotPorts) {
  const std::string text = "model {\n  a = process {\n    port out\n  }\n  b = process\n  a.out -> b\n}\n";
  auto parsed = dsl::parse(text);
  auto at = [&](std::string_view needle, std::size_t skip = 0) { return text.find(needle) + skip; };
  auto decl = editor::rename_target(parsed, at("a = "));
  ASSERT_TRUE(decl.has_value());
  EXPECT_EQ(decl->element, "a");
  auto ref = editor::rename_target(parsed, at("a.out", 1));  // the end of `a` touches it
  ASSERT_TRUE(ref.has_value());
  EXPECT_EQ(ref->element, "a");
  EXPECT_FALSE(editor::rename_target(parsed, at("a.out", 3)).has_value());  // `out` is a port
  EXPECT_FALSE(editor::rename_target(parsed, at("process")).has_value());
  auto target = editor::rename_target(parsed, at("-> b", 3));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->element, "b");
}
