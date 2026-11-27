#include "diadraw/dsl/json.hpp"

#include <algorithm>

#include "diadraw/base/text.hpp"
#include "diadraw/dsl/highlight.hpp"
#include "diadraw/model/json.hpp"

namespace dd::dsl {

namespace {

/// Converts byte offsets to UTF-16 offsets through a table built once per source, since spans
/// are looked up in id order, not position order.
class Utf16 {
 public:
  explicit Utf16(std::string_view s) : table_(s.size() + 1, 0) {
    std::uint32_t units = 0;
    for (std::size_t i = 0; i < s.size();) {
      auto c = static_cast<unsigned char>(s[i]);
      std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
      for (std::size_t k = 0; k < len && i + k < s.size(); ++k) table_[i + k] = units;
      units += len == 4 ? 2 : 1;
      i += len;
    }
    table_[s.size()] = units;
  }

  double operator()(std::uint32_t byte) const {
    return static_cast<double>(table_[std::min<std::size_t>(byte, table_.size() - 1)]);
  }

 private:
  std::vector<std::uint32_t> table_;
};

Json span_json(const Utf16& u, Span s) { return Json(Json::Object{{"from", u(s.from)}, {"to", u(s.to)}}); }

}  // namespace

Json diagnostics_json(const std::vector<Diagnostic>& diagnostics, std::string_view source) {
  Utf16 u(source);
  Json out = Json::array();
  for (const auto& d : diagnostics) {
    out.push(Json(Json::Object{{"severity", d.severity == Severity::Error ? "error" : "warning"},
                               {"message", d.message},
                               {"from", u(d.span.from)},
                               {"to", u(d.span.to)},
                               {"line", d.line},
                               {"column", d.column}}));
  }
  return out;
}

Json source_map_json(const SourceMap& map, std::string_view source) {
  Utf16 u(source);
  Json out = Json::object();
  if (map.model) out.set("model", span_json(u, *map.model));
  Json elements = Json::object();
  for (const auto& [id, e] : map.elements) {
    Json j(Json::Object{{"decl", span_json(u, e.decl)}, {"name", span_json(u, e.name)}, {"kind", span_json(u, e.kind)}});
    if (e.label) j.set("label", span_json(u, *e.label));
    if (e.body) j.set("body", span_json(u, *e.body));
    elements.add(id, std::move(j));
  }
  out.set("elements", std::move(elements));
  Json relationships = Json::object();
  for (const auto& [id, r] : map.relationships) {
    Json j(Json::Object{
        {"decl", span_json(u, r.decl)}, {"source", span_json(u, r.source)}, {"target", span_json(u, r.target)}});
    if (r.label) j.set("label", span_json(u, *r.label));
    relationships.add(id, std::move(j));
  }
  out.set("relationships", std::move(relationships));
  Json groups = Json::object();
  for (const auto& [id, g] : map.groups)
    groups.add(id, Json(Json::Object{{"decl", span_json(u, g.decl)}, {"body", span_json(u, g.body)}}));
  out.set("groups", std::move(groups));
  Json views = Json::object();
  for (const auto& [id, v] : map.views) {
    Json hints = Json::array();
    for (const auto& h : v.hints) hints.push(span_json(u, h));
    Json selectors = Json::array();
    for (const auto& s : v.selectors) {
      selectors.push(Json(Json::Object{{"list", s.include ? "include" : "exclude"},
                                       {"selector", s.selector},
                                       {"span", span_json(u, s.span)},
                                       {"statement", span_json(u, s.statement)}}));
    }
    Json j(Json::Object{{"decl", span_json(u, v.decl)},
                        {"body", span_json(u, v.body)},
                        {"hints", hints},
                        {"selectors", selectors}});
    if (v.layout) j.set("layout", span_json(u, *v.layout));
    if (!v.decorations.empty()) {
      Json decorations = Json::array();
      for (const auto& d : v.decorations) decorations.push(span_json(u, d));
      j.set("decorations", std::move(decorations));
    }
    views.add(id, std::move(j));
  }
  out.set("views", std::move(views));
  if (!map.libraries.empty()) {
    Json libs = Json::object();
    for (const auto& [alias, s] : map.libraries) libs.add(alias, span_json(u, s));
    out.set("libraries", std::move(libs));
  }
  return out;
}

Json to_json(const ParseResult& result, std::string_view source) {
  Json ops = Json::array();
  for (const auto& op : result.operations) ops.push(dd::to_json(op));
  return Json(Json::Object{{"model", dd::to_json(result.model)},
                           {"diagnostics", diagnostics_json(result.diagnostics, source)},
                           {"sourceMap", source_map_json(result.source_map, source)},
                           {"operations", ops}});
}

Json to_json(const SourceUpdate& update, std::string_view before) {
  Utf16 u(before);
  Json edits = Json::array();
  for (const auto& e : update.edits)
    edits.push(Json(Json::Object{{"from", u(e.from)}, {"to", u(e.to)}, {"insert", e.insert}}));
  return Json(Json::Object{{"source", update.source},
                           {"edits", edits},
                           {"model", dd::to_json(update.model)},
                           {"minimal", update.minimal},
                           {"diagnostics", diagnostics_json(update.result.diagnostics, update.source)}});
}

Json highlight_json(std::string_view text) {
  Json::Array tokens;
  std::size_t byte = 0, unit = 0;
  // Tokens come in order, so one pass converts every offset.
  auto to16 = [&](std::size_t target) {
    for (; byte < target && byte < text.size(); ++byte) {
      auto c = static_cast<unsigned char>(text[byte]);
      if ((c & 0xC0) != 0x80) unit += c >= 0xF0 ? 2 : 1;
    }
    return static_cast<double>(unit);
  };
  for (const auto& t : highlight(text)) {
    double from = to16(t.from);
    double to = to16(t.to);
    tokens.push_back(Json(Json::Array{Json(from), Json(to), Json(std::string(token_kind_name(t.kind)))}));
  }
  return Json(std::move(tokens));
}

Json fold_ranges_json(std::string_view text) {
  Utf16 u(text);
  Json::Array out;
  for (const auto& f : fold_ranges(text))
    out.push_back(Json(Json::Object{{"from", Json(u(f.from))},
                                    {"to", Json(u(f.to))},
                                    {"startLine", Json(static_cast<double>(f.start_line))},
                                    {"endLine", Json(static_cast<double>(f.end_line))}}));
  return Json(std::move(out));
}

}  // namespace dd::dsl
