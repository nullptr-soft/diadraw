// The language server (see server.hpp), a port of packages/language-server/src/server.ts.

#include "diadraw/lsp/server.hpp"

#include <algorithm>

#include "diadraw/base/text.hpp"
#include "diadraw/diadraw.h"
#include "diadraw/dsl/format.hpp"
#include "diadraw/dsl/highlight.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/library/builtin.hpp"

namespace dd::lsp {

namespace {

// ── Positions ───────────────────────────────────────────────────────────────────────────────

/// Byte offset of an LSP position (line, UTF-16 character), clamped to the text.
std::size_t offset_at(const std::string& text, const Json* position) {
  double line = position && position->get("line") ? position->get("line")->as_number() : 0;
  double character = position && position->get("character") ? position->get("character")->as_number() : 0;
  std::size_t start = 0;
  for (int l = 0; l < static_cast<int>(line); ++l) {
    auto nl = text.find('\n', start);
    if (nl == std::string::npos) return text.size();
    start = nl + 1;
  }
  auto end = text.find('\n', start);
  std::string_view row = std::string_view(text).substr(start, (end == std::string::npos ? text.size() : end) - start);
  if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
  return start + byte_offset(row, static_cast<std::size_t>(std::max(0.0, character)));
}

Json position_at(const std::string& text, std::size_t offset) {
  offset = std::min(offset, text.size());
  std::size_t line = 0, start = 0;
  for (std::size_t i = 0; i < offset; ++i)
    if (text[i] == '\n') {
      ++line;
      start = i + 1;
    }
  return Json(Json::Object{{"line", Json(line)}, {"character", Json(utf16_length(std::string_view(text).substr(start, offset - start)))}});
}

Json range_of(const std::string& text, const dsl::Span& span) {
  return Json(Json::Object{{"start", position_at(text, span.from)}, {"end", position_at(text, span.to)}});
}

/// The file a document URI names; nullopt for unsaved documents.
std::optional<std::string> path_of_uri_impl(const std::string& uri) {
  if (uri.rfind("file://", 0) != 0) return std::nullopt;
  std::string path;
  for (std::size_t i = 7; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size()) {
      path.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
      i += 2;
    } else {
      path.push_back(uri[i]);
    }
  }
  return path;
}

std::vector<LibraryContext::Available> builtin_available() {
  std::vector<LibraryContext::Available> out;
  for (const auto& b : library::builtin_bundles().as_array()) {
    LibraryContext::Available a{b.get("name")->as_string(), b.get("version")->as_string(), std::nullopt};
    if (const Json* d = b.get("description"); d && d->is_string()) a.description = d->as_string();
    out.push_back(a);
  }
  return out;
}

int symbol_kind(const std::string& kind) {
  if (kind == "element") return 5;  // Class
  if (kind == "view") return 3;     // Namespace
  if (kind == "group") return 4;    // Package
  return 2;                         // Module
}

Json symbol_json(const std::string& text, const lsp::Symbol& s) {
  Json::Array children;
  for (const auto& c : s.children) children.push_back(symbol_json(text, c));
  return Json(Json::Object{{"name", Json(s.name)},
                           {"detail", Json(s.detail)},
                           {"kind", Json(symbol_kind(s.kind))},
                           {"range", range_of(text, s.range)},
                           {"selectionRange", range_of(text, s.selection)},
                           {"children", Json(std::move(children))}});
}

int completion_kind(const std::string& kind) {
  if (kind == "keyword") return 14;
  if (kind == "element") return 18;  // Reference
  if (kind == "kind") return 7;      // Class
  if (kind == "value") return 12;
  if (kind == "property") return 10;
  return 15;  // Snippet
}

std::string text_of(const Json* v) { return v && v->is_string() ? v->as_string() : ""; }

}  // namespace

std::optional<std::string> path_of_uri(const std::string& uri) { return path_of_uri_impl(uri); }

LibraryResolver memory_resolver(std::vector<library::LibrarySource> extra) {
  return [extra = std::move(extra)](const std::vector<LibraryUse>& uses, const std::optional<std::string>&) {
    std::vector<library::LibrarySource> sources = extra;
    sources.push_back(library::builtin_source());
    return DocumentLibraries{library::resolve_uses(uses, sources), builtin_available()};
  };
}

LibraryResolver disk_resolver(std::shared_ptr<const library::DiskLibraries> disk,
                              std::vector<library::LibrarySource> extra, std::string fallback_dir) {
  return [disk = std::move(disk), extra = std::move(extra), fallback_dir = std::move(fallback_dir)](
             const std::vector<LibraryUse>& uses, const std::optional<std::string>& path) {
    std::string file = path.value_or(disk->fs().join(fallback_dir, "untitled.draw"));
    DocumentLibraries out{disk->resolve_for_file(uses, file, extra), builtin_available()};
    for (const auto& [name, versions] : disk->list_cache(disk->cache_dir()))
      out.available.push_back({name, versions[0], std::nullopt});
    if (const Json* index = disk->drawio_index())
      if (const Json* sets = index->get("sets"); sets && sets->is_array())
        for (const auto& s : sets->as_array())
          out.available.push_back({s.get("name")->as_string(), s.get("version")->as_string(),
                                   "draw.io " + (s.get("source") ? s.get("source")->as_string() : "") + " (restricted)"});
    return out;
  };
}

Server::Server(Send send, LibraryResolver libraries) : send_(std::move(send)), libraries_(std::move(libraries)) {}

void Server::respond(const Json& id, Json result) {
  send_(Json(Json::Object{{"jsonrpc", Json("2.0")}, {"id", id}, {"result", std::move(result)}}));
}

void Server::respond_error(const Json& id, int code, const std::string& message) {
  send_(Json(Json::Object{{"jsonrpc", Json("2.0")},
                          {"id", id},
                          {"error", Json(Json::Object{{"code", Json(code)}, {"message", Json(message)}})}}));
}

void Server::notify(const std::string& method, Json params) {
  send_(Json(Json::Object{{"jsonrpc", Json("2.0")}, {"method", Json(method)}, {"params", std::move(params)}}));
}

/// Libraries resolve per set of `use` statements.
void Server::update_libraries(Document& doc, const std::string& uri) {
  const auto uses = doc.parsed.model.libraries.value_or(std::vector<LibraryUse>{});
  std::string key;
  for (const auto& u : uses) key += library_spec(u) + " " + u.alias + "|";
  if (doc.libraries && key == doc.library_key) return;
  auto resolved = libraries_(uses, path_of_uri(uri));
  doc.library_key = key;
  doc.libraries = LibraryContext{std::move(resolved.resolution.set), std::move(resolved.available)};
  doc.unresolved = std::move(resolved.resolution.unresolved);
}

void Server::publish(const std::string& uri, Document& doc) {
  update_libraries(doc, uri);
  const auto& result = doc.parsed;
  Json::Array diagnostics;
  auto diagnostic = [&](const dsl::Span& span, int severity, const std::string& message) {
    diagnostics.emplace_back(Json::Object{{"range", range_of(doc.text, span)},
                                          {"severity", Json(severity)},
                                          {"message", Json(message)},
                                          {"source", Json("diadraw")}});
  };
  for (const auto& d : result.diagnostics) diagnostic(d.span, d.severity == dsl::Severity::Error ? 1 : 2, d.message);
  for (const auto& u : result.model.libraries.value_or(std::vector<LibraryUse>{})) {
    auto span = result.source_map.libraries.find(u.alias);
    if (span == result.source_map.libraries.end()) continue;
    for (const auto& un : doc.unresolved)
      if (un.message.find(u.name) != std::string::npos) {
        diagnostic(span->second, 2, un.message);
        break;
      }
  }
  for (const auto& u : library::unknown_references(result.model, doc.libraries->set)) {
    auto src = result.source_map.elements.find(u.element);
    if (src == result.source_map.elements.end()) continue;
    diagnostic(u.kind ? src->second.kind : src->second.decl, 2, u.message());
  }
  // Library themes the document picks or extends.
  auto check_theme = [&](const std::optional<std::string>& ref, const std::optional<dsl::Span>& at) {
    if (!ref || !at) return;
    auto dot = ref->find('.');
    if (dot == std::string::npos || !doc.libraries->set.library(ref->substr(0, dot)) || doc.libraries->set.theme(*ref))
      return;
    diagnostic(*at, 2, "No theme \"" + *ref + "\" in its library");
  };
  check_theme(result.model.theme, result.source_map.theme);
  for (const auto& v : result.model.views) {
    auto src = result.source_map.views.find(v.id);
    if (src != result.source_map.views.end()) check_theme(v.theme, src->second.theme);
  }
  for (std::size_t i = 0; i < result.model.themes.size() && i < result.source_map.themes.size(); ++i)
    check_theme(result.model.themes[i].base, result.source_map.themes[i].name);
  notify("textDocument/publishDiagnostics",
         Json(Json::Object{{"uri", Json(uri)}, {"version", Json(doc.version)}, {"diagnostics", Json(std::move(diagnostics))}}));
}

void Server::handle(const Json& msg) {
  const Json* message = &msg;
  const Json* id = message->get("id");
  std::string method = text_of(message->get("method"));
  const Json* params = message->get("params");
  auto doc_of = [&](const Json* p) -> std::pair<std::string, Document*> {
    const Json* td = p ? p->get("textDocument") : nullptr;
    std::string uri = text_of(td ? td->get("uri") : nullptr);
    auto it = documents_.find(uri);
    return {uri, it == documents_.end() ? nullptr : &it->second};
  };

  if (method == "initialize") {
    respond(*id, Json(Json::Object{
                     {"capabilities",
                      Json(Json::Object{{"textDocumentSync", Json(2)},
                                        {"documentSymbolProvider", Json(true)},
                                        {"foldingRangeProvider", Json(true)},
                                        {"definitionProvider", Json(true)},
                                        {"hoverProvider", Json(true)},
                                        {"documentFormattingProvider", Json(true)},
                                        {"renameProvider", Json(Json::Object{{"prepareProvider", Json(true)}})},
                                        {"completionProvider",
                                         Json(Json::Object{{"triggerCharacters", Json(Json::Array{Json("."), Json(" "), Json("=")})}})}})},
                     {"serverInfo", Json(Json::Object{{"name", Json("diadraw-language-server")}, {"version", Json(dd_version())}})}}));
  } else if (method == "shutdown") {
    shutdown_ = true;
    respond(*id, Json());
  } else if (method == "exit") {
    exit_code_ = shutdown_ ? 0 : 1;
  } else if (method == "textDocument/didOpen") {
    const Json* td = params->get("textDocument");
    std::string uri = text_of(td->get("uri"));
    Document doc;
    doc.text = text_of(td->get("text"));
    doc.version = td->get("version") ? static_cast<int>(td->get("version")->as_number()) : 0;
    doc.parsed = dsl::parse(doc.text);
    publish(uri, documents_[uri] = std::move(doc));
  } else if (method == "textDocument/didChange") {
    auto [uri, doc] = doc_of(params);
    if (!doc) return;
    if (const Json* changes = params->get("contentChanges"); changes && changes->is_array())
      for (const auto& change : changes->as_array()) {
        std::string text = text_of(change.get("text"));
        if (const Json* range = change.get("range")) {
          std::size_t from = offset_at(doc->text, range->get("start"));
          std::size_t to = offset_at(doc->text, range->get("end"));
          doc->text.replace(from, std::max(from, to) - from, text);
        } else {
          doc->text = text;
        }
      }
    if (const Json* v = params->get("textDocument")->get("version")) doc->version = static_cast<int>(v->as_number());
    doc->parsed = dsl::parse(doc->text);
    publish(uri, *doc);
  } else if (method == "textDocument/didClose") {
    auto [uri, doc] = doc_of(params);
    documents_.erase(uri);
    notify("textDocument/publishDiagnostics", Json(Json::Object{{"uri", Json(uri)}, {"diagnostics", Json::array()}}));
  } else if (method == "textDocument/documentSymbol") {
    auto [uri, doc] = doc_of(params);
    Json::Array out;
    if (doc)
      for (const auto& s : lsp::document_symbols(doc->parsed)) out.push_back(symbol_json(doc->text, s));
    respond(*id, Json(std::move(out)));
  } else if (method == "textDocument/foldingRange") {
    // The `{` … `}` blocks; the `}` line stays visible, as in the shells and the web app.
    auto [uri, doc] = doc_of(params);
    Json::Array out;
    if (doc)
      for (const auto& f : dsl::fold_ranges(doc->text))
        out.push_back(Json(Json::Object{{"startLine", Json(static_cast<double>(f.start_line))},
                                        {"endLine", Json(static_cast<double>(f.end_line))}}));
    respond(*id, Json(std::move(out)));
  } else if (method == "textDocument/definition") {
    auto [uri, doc] = doc_of(params);
    std::optional<dsl::Span> span;
    if (doc) span = lsp::definition(doc->text, offset_at(doc->text, params->get("position")), doc->parsed);
    respond(*id, span ? Json(Json::Object{{"uri", Json(uri)}, {"range", range_of(doc->text, *span)}}) : Json());
  } else if (method == "textDocument/hover") {
    auto [uri, doc] = doc_of(params);
    std::optional<lsp::Hover> h;
    if (doc) h = lsp::hover(doc->text, offset_at(doc->text, params->get("position")), doc->parsed);
    respond(*id, h ? Json(Json::Object{{"contents", Json(Json::Object{{"kind", Json("markdown")}, {"value", Json(h->markdown)}})},
                                       {"range", range_of(doc->text, h->span)}})
                   : Json());
  } else if (method == "textDocument/prepareRename") {
    auto [uri, doc] = doc_of(params);
    std::optional<editor::RenameTarget> target;
    if (doc) target = editor::rename_target(doc->parsed, offset_at(doc->text, params->get("position")));
    if (!target) {
      respond_error(*id, -32803, "Only an element's name can be renamed");
      return;
    }
    respond(*id, Json(Json::Object{
                     {"range", range_of(doc->text, target->span)},
                     {"placeholder", Json(doc->text.substr(target->span.from, target->span.to - target->span.from))}}));
  } else if (method == "textDocument/rename") {
    auto [uri, doc] = doc_of(params);
    std::optional<editor::RenameTarget> target;
    if (doc) target = editor::rename_target(doc->parsed, offset_at(doc->text, params->get("position")));
    if (!target) {
      respond_error(*id, -32803, "Only an element's name can be renamed");
      return;
    }
    auto renamed = editor::rename_element(doc->text, doc->parsed, target->element, text_of(params->get("newName")));
    if (!renamed) {
      respond_error(*id, -32803, renamed.error().message);
      return;
    }
    Json::Array edits;
    if (*renamed)
      for (const auto& e : (*renamed)->update.edits)
        edits.emplace_back(Json::Object{{"range", range_of(doc->text, {e.from, e.to})}, {"newText", Json(e.insert)}});
    respond(*id, Json(Json::Object{{"changes", Json(Json::Object{{uri, Json(std::move(edits))}})}}));
  } else if (method == "textDocument/formatting") {
    // Format Document with the editor's indentation (its tab size), as one edit over the whole
    // text; none when it is formatted already.
    auto [uri, doc] = doc_of(params);
    Json::Array edits;
    if (doc) {
      const Json* options = params->get("options");
      const Json* tab_size = options ? options->get("tabSize") : nullptr;
      int indent = dsl::kDefaultIndentSize;
      // Clamped as a double first: a huge or NaN size would not convert to int.
      if (double size = tab_size && tab_size->is_number() ? tab_size->as_number() : 0; size >= dsl::kMinIndentSize)
        indent = static_cast<int>(std::min(size, static_cast<double>(dsl::kMaxIndentSize)));
      std::string formatted = dsl::format_source(doc->text, dsl::FormatOptions{indent});
      if (formatted != doc->text)
        edits.emplace_back(Json::Object{{"range", range_of(doc->text, {0, static_cast<std::uint32_t>(doc->text.size())})},
                                        {"newText", Json(std::move(formatted))}});
    }
    respond(*id, Json(std::move(edits)));
  } else if (method == "textDocument/completion") {
    auto [uri, doc] = doc_of(params);
    Json::Array items;
    if (doc) {
      update_libraries(*doc, uri);
      for (const auto& c : lsp::completions(doc->text, offset_at(doc->text, params->get("position")), doc->parsed,
                                            &*doc->libraries)) {
        Json item(Json::Object{{"label", Json(c.label)}, {"kind", Json(completion_kind(c.kind))}});
        if (c.detail) item.set("detail", Json(*c.detail));
        if (c.insert_text) item.set("insertText", Json(*c.insert_text));
        if (c.kind == "snippet") item.set("insertTextFormat", Json(2));
        items.push_back(std::move(item));
      }
    }
    respond(*id, Json(std::move(items)));
  } else if (id) {
    respond_error(*id, -32601, "Method not found: " + method);
  }
}

}  // namespace dd::lsp
