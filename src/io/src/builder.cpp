// Port of packages/import/src/builder.ts.

#include "diadraw/io/builder.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "diadraw/base/text.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/model/json.hpp"

namespace dd::io {

namespace {

const std::unordered_set<std::string_view> kKeywords{
    "model", "view", "of", "group", "port", "tech", "shape", "tag", "description", "include", "exclude", "layout",
    "direction", "spacing", "pin", "at", "below", "above", "left", "right", "up", "down", "align", "horizontally",
    "vertically", "order", "north", "south", "east", "west", "style", "prop", "size", "route", "via", "from", "to",
    "sequence", "activate", "deactivate", "over", "divider", "delay", "ref", "autonumber", "alt", "opt", "loop",
    "par", "critical", "break", "else"};

struct Decomposition {
  std::uint32_t cp;
  const char* text;
};

/// NFKD of a code point without combining marks, where it can change an identifier.
const Decomposition kNfkd[] = {
#include "nfkd_table.inc"
};

const char* decompose(std::uint32_t cp) {
  static const std::unordered_map<std::uint32_t, const char*> table = [] {
    std::unordered_map<std::uint32_t, const char*> m;
    for (const auto& d : kNfkd) m.emplace(d.cp, d.text);
    return m;
  }();
  auto it = table.find(cp);
  return it == table.end() ? nullptr : it->second;
}

bool is_word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

/// The value `/<tag\s*\/?>/i`-style helpers below look for: does `s` at `i` start a tag named `name`?
bool tag_at(std::string_view s, std::size_t i, std::string_view name) {
  return s.size() >= i + name.size() && iequals(s.substr(i, name.size()), name);
}

}  // namespace

std::string to_identifier(std::string_view text, std::string_view fallback) {
  // normalize('NFKD').replace(/[̀-ͯ]/g, '')
  std::string decomposed;
  for (std::size_t i = 0; i < text.size();) {
    std::uint32_t cp = next_code_point(text, i);
    if (cp >= 0x300 && cp <= 0x36F) continue;
    if (const char* d = decompose(cp))
      decomposed += d;
    else
      append_utf8(decomposed, cp);
  }
  // .replace(/[^A-Za-z0-9_]+/g, '_').replace(/^_+|_+$/g, '')
  std::string id;
  bool in_run = false;
  for (char c : decomposed) {
    if (is_word(c)) {
      id.push_back(c);
      in_run = false;
    } else if (!in_run) {
      id.push_back('_');
      in_run = true;
    }
  }
  std::size_t start = id.find_first_not_of('_');
  if (start == std::string::npos) {
    id.clear();
  } else {
    id = id.substr(start, id.find_last_not_of('_') - start + 1);
  }
  if (id.empty()) id = std::string(fallback);
  if (id.front() >= '0' && id.front() <= '9') id = "n" + id;
  if (kKeywords.count(id)) id += "_";
  return id;
}

namespace {

/// The named HTML entities labels use beyond the five of XML: quotes, dashes, arrows, signs and
/// the Latin-1 symbols. Names are case-sensitive (`&rarr;` is not `&rArr;`).
const std::unordered_map<std::string_view, std::uint32_t>& named_entities() {
  static const std::unordered_map<std::string_view, std::uint32_t> m{
      {"laquo", 0xAB},   {"raquo", 0xBB},   {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"lsquo", 0x2018},
      {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"bdquo", 0x201E},
      {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026}, {"bull", 0x2022},   {"middot", 0xB7},
      {"prime", 0x2032}, {"Prime", 0x2033}, {"dagger", 0x2020}, {"Dagger", 0x2021}, {"permil", 0x2030},
      {"copy", 0xA9},    {"reg", 0xAE},     {"trade", 0x2122},  {"deg", 0xB0},      {"plusmn", 0xB1},
      {"times", 0xD7},   {"divide", 0xF7},  {"micro", 0xB5},    {"para", 0xB6},     {"sect", 0xA7},
      {"cent", 0xA2},    {"pound", 0xA3},   {"yen", 0xA5},      {"euro", 0x20AC},   {"curren", 0xA4},
      {"iexcl", 0xA1},   {"iquest", 0xBF},  {"not", 0xAC},      {"shy", 0xAD},      {"macr", 0xAF},
      {"acute", 0xB4},   {"cedil", 0xB8},   {"ordf", 0xAA},     {"ordm", 0xBA},     {"brvbar", 0xA6},
      {"uml", 0xA8},     {"sup1", 0xB9},    {"sup2", 0xB2},     {"sup3", 0xB3},     {"frac14", 0xBC},
      {"frac12", 0xBD},  {"frac34", 0xBE},  {"larr", 0x2190},   {"uarr", 0x2191},   {"rarr", 0x2192},
      {"darr", 0x2193},  {"harr", 0x2194},  {"lArr", 0x21D0},   {"uArr", 0x21D1},   {"rArr", 0x21D2},
      {"dArr", 0x21D3},  {"hArr", 0x21D4},  {"le", 0x2264},     {"ge", 0x2265},     {"ne", 0x2260},
      {"asymp", 0x2248}, {"equiv", 0x2261}, {"infin", 0x221E},  {"minus", 0x2212},  {"forall", 0x2200},
      {"exist", 0x2203}, {"isin", 0x2208},  {"notin", 0x2209},  {"sum", 0x2211},    {"prod", 0x220F},
      {"radic", 0x221A}, {"and", 0x2227},   {"or", 0x2228},     {"cap", 0x2229},    {"cup", 0x222A},
      {"empty", 0x2205}, {"nabla", 0x2207}, {"part", 0x2202},   {"sdot", 0x22C5},   {"loz", 0x25CA},
      {"spades", 0x2660}, {"clubs", 0x2663}, {"hearts", 0x2665}, {"diams", 0x2666}, {"ensp", 0x2002},
      {"emsp", 0x2003},  {"thinsp", 0x2009}, {"zwnj", 0x200C},  {"zwj", 0x200D},    {"alpha", 0x3B1},
      {"beta", 0x3B2},   {"gamma", 0x3B3},  {"delta", 0x3B4},   {"epsilon", 0x3B5}, {"lambda", 0x3BB},
      {"mu", 0x3BC},     {"pi", 0x3C0},     {"sigma", 0x3C3},   {"tau", 0x3C4},     {"phi", 0x3C6},
      {"omega", 0x3C9},  {"Delta", 0x394},  {"Sigma", 0x3A3},   {"Omega", 0x3A9},
  };
  return m;
}

}  // namespace

std::string decode_entities(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      auto semi = text.find(';', i + 1);
      if (semi != std::string_view::npos) {
        std::string_view e = text.substr(i + 1, semi - i - 1);
        std::string lower;
        for (char c : e) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        auto all = [&](std::size_t from, auto pred) {
          return lower.size() > from && std::all_of(lower.begin() + static_cast<std::ptrdiff_t>(from), lower.end(), pred);
        };
        std::optional<std::string> replacement;
        if (lower.rfind("#x", 0) == 0 && all(2, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
          std::string r;
          append_utf8(r, static_cast<std::uint32_t>(std::stoul(lower.substr(2), nullptr, 16)));
          replacement = r;
        } else if (lower.rfind("#", 0) == 0 && all(1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
          std::string r;
          append_utf8(r, static_cast<std::uint32_t>(std::stoul(lower.substr(1), nullptr, 10)));
          replacement = r;
        } else if (lower == "amp") replacement = "&";
        else if (lower == "lt") replacement = "<";
        else if (lower == "gt") replacement = ">";
        else if (lower == "quot") replacement = "\"";
        else if (lower == "apos") replacement = "'";
        else if (lower == "nbsp") replacement = " ";
        else if (auto it = named_entities().find(e); it != named_entities().end()) {
          std::string r;
          append_utf8(r, it->second);
          replacement = r;
        }
        if (replacement) {
          out += *replacement;
          i = semi + 1;
          continue;
        }
      }
    }
    out.push_back(text[i++]);
  }
  return out;
}

std::string plain_text(std::string_view html) {
  // .replace(/<br\s*\/?>/gi, ' ').replace(/<\/(div|p)>/gi, ' ').replace(/<[^>]*>/g, '')
  std::string s;
  for (std::size_t i = 0; i < html.size();) {
    if (html[i] == '<') {
      // <br\s*/?>
      if (tag_at(html, i + 1, "br")) {
        std::size_t j = i + 3;
        while (j < html.size() && std::strchr(" \t\n\r\f\v", html[j])) ++j;
        if (j < html.size() && html[j] == '/') ++j;
        if (j < html.size() && html[j] == '>') {
          s.push_back(' ');
          i = j + 1;
          continue;
        }
      }
      // </div> or </p>
      if (tag_at(html, i, "</div>") || tag_at(html, i, "</p>")) {
        s.push_back(' ');
        i += tag_at(html, i, "</p>") ? 4 : 6;
        continue;
      }
      // <[^>]*>
      auto close = html.find('>', i);
      if (close != std::string_view::npos) {
        i = close + 1;
        continue;
      }
    }
    s.push_back(html[i++]);
  }
  return std::string(js_trim(collapse_whitespace(decode_entities(s))));
}

// ── Report JSON ───────────────────────────────────────────────────────────────────────────────

Json to_json(const ImportReport& report) {
  Json dropped = Json::array();
  for (const auto& d : report.dropped) {
    Json j(Json::Object{{"message", d.message}});
    if (d.line) j.set("line", *d.line);
    if (d.construct) j.set("construct", *d.construct);
    dropped.push(std::move(j));
  }
  Json notes = Json::array();
  for (const auto& n : report.notes) notes.push(n);
  Json out(Json::Object{{"dropped", dropped}, {"notes", notes}});
  auto counts = [](const auto& list) {
    Json j = Json::object();
    for (const auto& [k, v] : list) j.set(k, v);
    return j;
  };
  if (report.unknown_shapes) out.set("unknownShapes", counts(*report.unknown_shapes));
  if (report.unknown_styles) out.set("unknownStyles", counts(*report.unknown_styles));
  if (report.stats) out.set("stats", counts(*report.stats));
  return out;
}

Json to_json(const ImportResult& result) {
  return Json(Json::Object{{"model", dd::to_json(result.model)}, {"report", to_json(result.report)}});
}

// ── ModelBuilder ──────────────────────────────────────────────────────────────────────────────

void ModelBuilder::apply(const Operation& op) {
  // Importers only issue operations they have checked; a refusal is a bug in the importer.
  auto r = apply_in_place(model_, op);
  if (!r) drop("Internal: " + r.error().message);
}

void ModelBuilder::drop(std::string message, std::optional<int> line, std::optional<std::string> construct) {
  dropped_.push_back({line, std::move(message), std::move(construct)});
}

void ModelBuilder::note(const std::string& message) {
  if (std::find(notes_.begin(), notes_.end(), message) == notes_.end()) notes_.push_back(message);
}

std::string ModelBuilder::use_library(const std::string& name, const std::string& range, const std::string& alias) {
  if (model_.libraries)
    for (const auto& u : *model_.libraries)
      if (u.name == name) return u.alias;
  apply(op::UseLibrary{{name, range, alias}, std::nullopt});
  return alias;
}

std::optional<ElementId> ModelBuilder::id(std::string_view source_id) const {
  auto it = ids_.find(source_id);
  if (it == ids_.end()) return std::nullopt;
  return it->second;
}

bool ModelBuilder::has(std::string_view source_id) const { return ids_.find(source_id) != ids_.end(); }

std::optional<std::string> ModelBuilder::key_of(std::string_view id) const {
  auto it = keys_.find(id);
  if (it == keys_.end()) return std::nullopt;
  return it->second;
}

ElementId ModelBuilder::add_element(const std::string& source_id, ElementInit init) {
  if (auto existing = id(source_id)) return *existing;
  std::optional<ElementId> parent = init.parent ? id(*init.parent) : std::nullopt;
  std::string local = to_identifier(init.local ? *init.local : source_id);
  std::string prefix = parent ? *parent + "." : "";
  std::string element_id = prefix + local;
  for (int n = 2; model_.elements.contains(element_id); ++n) element_id = prefix + local + std::to_string(n);
  Element e = make_element(element_id, init.kind, init.name, parent);
  e.group = init.group;
  e.technology = init.technology;
  e.description = init.description;
  e.shape = init.shape;
  if (init.tags) e.tags = *init.tags;
  e.style = init.style;
  if (init.properties) e.properties = *init.properties;
  apply(op::AddElement{std::move(e)});
  ids_[source_id] = element_id;
  keys_[element_id] = source_id;
  return element_id;
}

void ModelBuilder::set_property(std::string_view source_id, const std::string& key, const std::string& value) {
  patch(source_id, [&](Element& e) { e.properties.set(key, value); });
}

void ModelBuilder::add_port(std::string_view source_id, const std::string& port) {
  patch(source_id, [&](Element& e) {
    if (std::none_of(e.ports.begin(), e.ports.end(), [&](const Port& p) { return p.id == port; }))
      e.ports.push_back({port, std::nullopt});
  });
}

void ModelBuilder::set_view_spacing(std::string_view view_id, double spacing) {
  const View* v = model_.views.find(view_id);
  if (!v) return;
  View copy = *v;
  copy.layout.spacing = spacing;
  std::string id = copy.id;  // before the move: argument order is unspecified
  model_.views.set(std::move(id), std::move(copy));
}

GroupId ModelBuilder::add_group(const std::string& name, const std::optional<GroupId>& parent) {
  std::string base = parent ? *parent + "/" + name : name;
  std::string group_id = base;
  for (int n = 2; model_.groups.contains(group_id); ++n) group_id = base + "#" + std::to_string(n);
  apply(op::AddGroup{{group_id, name, parent}});
  return group_id;
}

std::optional<RelationshipId> ModelBuilder::add_relationship(const std::string& from, const std::string& to,
                                                             RelationshipInit init, std::optional<int> line) {
  auto source = id(from);
  auto target = id(to);
  if (!source || !target) {
    drop("Relationship " + from + " \xE2\x86\x92 " + to + ": unknown " + (source ? to : from), line);
    return std::nullopt;
  }
  Endpoint s{*source, std::nullopt}, t{*target, std::nullopt};
  int index = 1;
  while (model_.relationships.contains(dsl::relationship_id(s, t, index))) ++index;
  std::string rid = dsl::relationship_id(s, t, index);
  Relationship r = make_relationship(rid, s, t);
  if (init.label && !init.label->empty()) r.label = init.label;
  if (init.technology && !init.technology->empty()) r.technology = init.technology;
  if (init.line) r.line = *init.line;
  r.bidirectional = init.bidirectional;
  r.style = init.style;
  if (init.properties) r.properties = *init.properties;
  apply(op::AddRelationship{std::move(r)});
  return rid;
}

ViewId ModelBuilder::add_view(const std::string& name, ViewInit init) {
  std::string base = to_identifier(name, "main");
  std::string view_id = base;
  for (int n = 2; model_.views.contains(view_id); ++n) view_id = base + std::to_string(n);
  View v = make_view(view_id, init.scope ? id(*init.scope) : std::nullopt);
  if (init.include) {
    v.include.clear();
    for (const auto& s : *init.include) {
      if (s == "*")
        v.include.push_back("*");
      else if (auto mapped = id(s))
        v.include.push_back(*mapped);
    }
    if (v.include.empty()) v.include = {"*"};
  }
  v.layout.algorithm = init.algorithm.value_or("layered");
  v.layout.direction = init.direction.value_or(Direction::Down);
  v.hints = std::move(init.hints);
  v.sequence = std::move(init.sequence);
  if (init.autonumber) v.autonumber = true;
  apply(op::AddView{std::move(v)});
  return view_id;
}

}  // namespace dd::io
