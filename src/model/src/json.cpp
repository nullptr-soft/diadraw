#include "diadraw/model/json.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace dd {

namespace {

template <class... F>
struct overloaded : F... {
  using F::operator()...;
};
template <class... F>
overloaded(F...) -> overloaded<F...>;

// ── Enum names, as the TypeScript string unions spell them ─────────────────────────────────────

template <class E, std::size_t N>
struct EnumNames {
  std::array<std::string_view, N> names;
  [[nodiscard]] std::string_view name(E e) const { return names[static_cast<std::size_t>(e)]; }
  [[nodiscard]] std::optional<E> parse(std::string_view s) const {
    for (std::size_t i = 0; i < N; ++i)
      if (names[i] == s) return static_cast<E>(i);
    return std::nullopt;
  }
};

constexpr EnumNames<PortSide, 4> kPortSide{{"north", "south", "east", "west"}};
constexpr EnumNames<LineStyle, 2> kLineStyle{{"solid", "dashed"}};
constexpr EnumNames<Direction, 4> kDirection{{"right", "left", "down", "up"}};
constexpr EnumNames<Placement, 4> kPlacement{{"below", "above", "left-of", "right-of"}};
constexpr EnumNames<Axis, 2> kAxis{{"horizontal", "vertical"}};
constexpr EnumNames<MessageStyle, 3> kMessageStyle{{"sync", "async", "reply"}};
constexpr EnumNames<FrameKind, 7> kFrameKind{
    {"alt", "opt", "loop", "par", "critical", "break", "group"}};
constexpr EnumNames<NotePosition, 3> kNotePosition{{"over", "left", "right"}};
constexpr EnumNames<DecorationKind, 3> kDecorationKind{{"note", "text", "box"}};

// ── Writing ────────────────────────────────────────────────────────────────────────────────────

Json strings(const std::vector<std::string>& v) {
  Json out = Json::array();
  for (const auto& s : v) out.push(s);
  return out;
}

Json string_map(const StringMap& m) {
  Json out = Json::object();
  for (const auto& [k, v] : m) out.set(k, v);
  return out;
}

Json point(const Point2& p) { return Json(Json::Object{{"x", p.x}, {"y", p.y}}); }

void set_opt(Json& j, const char* key, const std::optional<std::string>& v) {
  if (v) j.set(key, *v);
}

Json endpoint(const Endpoint& e) {
  Json j = Json::object();
  j.set("element", e.element);
  set_opt(j, "port", e.port);
  return j;
}

Json layout_options(const LayoutOptions& l) {
  Json j = Json::object();
  j.set("algorithm", l.algorithm);
  j.set("direction", kDirection.name(l.direction));
  if (l.spacing) j.set("spacing", *l.spacing);
  return j;
}

Json library_use(const LibraryUse& u) {
  return Json(Json::Object{{"name", u.name}, {"range", u.range}, {"alias", u.alias}});
}

// ── Reading ────────────────────────────────────────────────────────────────────────────────────

/// Reads fields of one JSON object, remembering the first problem. Missing optional fields read
/// as absent; a wrong type is an error.
class Reader {
 public:
  Reader(const Json& j, std::string what) : j_(j), what_(std::move(what)) {
    if (!j.is_object()) problem("is not an object");
  }

  [[nodiscard]] bool ok() const { return !error_; }
  [[nodiscard]] Error error() const { return *error_; }

  std::string str(const char* key) {
    auto v = opt_str(key);
    if (!v && ok()) problem(std::string("needs \"") + key + "\"");
    return v.value_or("");
  }

  std::optional<std::string> opt_str(const char* key) {
    const Json* v = field(key);
    if (!v) return std::nullopt;
    if (!v->is_string()) {
      problem(std::string("\"") + key + "\" must be a string");
      return std::nullopt;
    }
    return v->as_string();
  }

  double num(const char* key) {
    auto v = opt_num(key);
    if (!v && ok()) problem(std::string("needs \"") + key + "\"");
    return v.value_or(0);
  }

  std::optional<double> opt_num(const char* key) {
    const Json* v = field(key);
    if (!v) return std::nullopt;
    if (!v->is_number()) {
      problem(std::string("\"") + key + "\" must be a number");
      return std::nullopt;
    }
    return v->as_number();
  }

  std::optional<std::size_t> opt_index(const char* key) {
    auto v = opt_num(key);
    if (!v) return std::nullopt;
    if (*v < 0 || *v != std::floor(*v)) {
      problem(std::string("\"") + key + "\" must be a whole number");
      return std::nullopt;
    }
    return static_cast<std::size_t>(*v);
  }

  std::optional<bool> opt_bool(const char* key) {
    const Json* v = field(key);
    if (!v) return std::nullopt;
    if (!v->is_bool()) {
      problem(std::string("\"") + key + "\" must be true or false");
      return std::nullopt;
    }
    return v->as_bool();
  }

  std::vector<std::string> strings(const char* key) {
    std::vector<std::string> out;
    const Json* v = array(key);
    if (!v) return out;
    for (const auto& item : v->as_array()) {
      if (!item.is_string()) {
        problem(std::string("\"") + key + "\" must hold strings");
        return out;
      }
      out.push_back(item.as_string());
    }
    return out;
  }

  std::optional<StringMap> opt_map(const char* key) {
    const Json* v = field(key);
    if (!v) return std::nullopt;
    if (!v->is_object()) {
      problem(std::string("\"") + key + "\" must be an object");
      return std::nullopt;
    }
    StringMap m;
    for (const auto& [k, item] : v->as_object()) {
      if (!item.is_string()) {
        problem(std::string("\"") + key + "." + k + "\" must be a string");
        return std::nullopt;
      }
      m.set(k, item.as_string());
    }
    return m;
  }

  /// The array at `key`, or nullptr when absent (an error when present but not an array).
  const Json* array(const char* key) {
    const Json* v = field(key);
    if (v && !v->is_array()) {
      problem(std::string("\"") + key + "\" must be an array");
      return nullptr;
    }
    return v;
  }

  const Json* object(const char* key) {
    const Json* v = field(key);
    if (v && !v->is_object()) {
      problem(std::string("\"") + key + "\" must be an object");
      return nullptr;
    }
    return v;
  }

  template <class E, std::size_t N>
  std::optional<E> opt_enum(const char* key, const EnumNames<E, N>& names) {
    auto s = opt_str(key);
    if (!s) return std::nullopt;
    auto e = names.parse(*s);
    if (!e) problem(std::string("\"") + key + "\" has unknown value \"" + *s + "\"");
    return e;
  }

  template <class E, std::size_t N>
  E enumeration(const char* key, const EnumNames<E, N>& names, E fallback) {
    return opt_enum(key, names).value_or(fallback);
  }

  /// Carries an error from a nested reader or result.
  void problem(std::string message) {
    if (!error_) error_ = Error{what_ + " " + message};
  }
  void nested(const Error& e) {
    if (!error_) error_ = e;
  }

 private:
  const Json* field(const char* key) const { return j_.is_object() ? j_.get(key) : nullptr; }

  const Json& j_;
  std::string what_;
  std::optional<Error> error_;
};

std::optional<Point2> read_point(Reader& r, const Json* j, const char* what) {
  if (!j) return std::nullopt;
  Reader p(*j, what);
  Point2 out{p.num("x"), p.num("y")};
  if (!p.ok()) {
    r.nested(p.error());
    return std::nullopt;
  }
  return out;
}

Endpoint read_endpoint(Reader& r, const Json* j, const char* what) {
  if (!j) {
    r.problem(std::string("needs \"") + what + "\"");
    return {};
  }
  Reader e(*j, what);
  Endpoint out{e.str("element"), e.opt_str("port")};
  if (!e.ok()) r.nested(e.error());
  return out;
}

}  // namespace

// ── Model items ────────────────────────────────────────────────────────────────────────────────

Json to_json(const Element& e) {
  Json j = Json::object();
  j.set("id", e.id);
  j.set("kind", e.kind);
  j.set("name", e.name);
  set_opt(j, "parent", e.parent);
  set_opt(j, "group", e.group);
  set_opt(j, "technology", e.technology);
  set_opt(j, "description", e.description);
  set_opt(j, "shape", e.shape);
  j.set("tags", strings(e.tags));
  Json ports = Json::array();
  for (const auto& p : e.ports) {
    Json pj = Json::object();
    pj.set("id", p.id);
    if (p.side) pj.set("side", kPortSide.name(*p.side));
    ports.push(std::move(pj));
  }
  j.set("ports", std::move(ports));
  j.set("properties", string_map(e.properties));
  if (e.style) j.set("style", string_map(*e.style));
  return j;
}

Result<Element> element_from_json(const Json& j) {
  Reader r(j, "element");
  Element e;
  e.id = r.str("id");
  e.kind = r.str("kind");
  e.name = r.opt_str("name").value_or(e.id);
  e.parent = r.opt_str("parent");
  e.group = r.opt_str("group");
  e.technology = r.opt_str("technology");
  e.description = r.opt_str("description");
  e.shape = r.opt_str("shape");
  e.tags = r.strings("tags");
  if (const Json* ports = r.array("ports")) {
    for (const auto& pj : ports->as_array()) {
      Reader p(pj, "port");
      Port port{p.str("id"), p.opt_enum("side", kPortSide)};
      if (!p.ok()) r.nested(p.error());
      e.ports.push_back(std::move(port));
    }
  }
  e.properties = r.opt_map("properties").value_or(StringMap{});
  e.style = r.opt_map("style");
  if (!r.ok()) return r.error();
  return e;
}

Json to_json(const Relationship& rel) {
  Json j = Json::object();
  j.set("id", rel.id);
  j.set("source", endpoint(rel.source));
  j.set("target", endpoint(rel.target));
  set_opt(j, "label", rel.label);
  set_opt(j, "technology", rel.technology);
  j.set("line", kLineStyle.name(rel.line));
  j.set("bidirectional", rel.bidirectional);
  j.set("tags", strings(rel.tags));
  j.set("properties", string_map(rel.properties));
  if (rel.style) j.set("style", string_map(*rel.style));
  if (rel.implied) j.set("implied", true);
  return j;
}

Result<Relationship> relationship_from_json(const Json& j) {
  Reader r(j, "relationship");
  Relationship rel;
  rel.id = r.str("id");
  rel.source = read_endpoint(r, r.object("source"), "source");
  rel.target = read_endpoint(r, r.object("target"), "target");
  rel.label = r.opt_str("label");
  rel.technology = r.opt_str("technology");
  rel.line = r.enumeration("line", kLineStyle, LineStyle::Solid);
  rel.bidirectional = r.opt_bool("bidirectional").value_or(false);
  rel.tags = r.strings("tags");
  rel.properties = r.opt_map("properties").value_or(StringMap{});
  rel.style = r.opt_map("style");
  rel.implied = r.opt_bool("implied").value_or(false);
  if (!r.ok()) return r.error();
  return rel;
}

Json to_json(const Group& g) {
  Json j = Json::object();
  j.set("id", g.id);
  j.set("name", g.name);
  set_opt(j, "parent", g.parent);
  return j;
}

Result<Group> group_from_json(const Json& j) {
  Reader r(j, "group");
  Group g{r.str("id"), r.str("name"), r.opt_str("parent")};
  if (!r.ok()) return r.error();
  return g;
}

// ── Hints and sequence items ───────────────────────────────────────────────────────────────────

Json to_json(const LayoutHint& hint) {
  return std::visit(
      overloaded{
          [](const PinHint& h) {
            return Json(Json::Object{{"kind", "pin"},
                                     {"element", h.element},
                                     {"placement", kPlacement.name(h.placement)},
                                     {"reference", h.reference}});
          },
          [](const PinAtHint& h) {
            Json j(Json::Object{{"kind", "pinAt"}, {"element", h.element}, {"x", h.x}, {"y", h.y}});
            if (h.width) j.set("width", *h.width);
            if (h.height) j.set("height", *h.height);
            return j;
          },
          [](const RouteHint& h) {
            Json pts = Json::array();
            for (const auto& p : h.points) pts.push(point(p));
            Json j(Json::Object{{"kind", "route"}, {"relationship", h.relationship}, {"points", pts}});
            if (h.exit) j.set("exit", point(*h.exit));
            if (h.entry) j.set("entry", point(*h.entry));
            return j;
          },
          [](const AlignHint& h) {
            return Json(Json::Object{{"kind", "align"},
                                     {"elements", strings(h.elements)},
                                     {"axis", kAxis.name(h.axis)}});
          },
          [](const OrderHint& h) {
            return Json(Json::Object{{"kind", "order"}, {"elements", strings(h.elements)}});
          },
          [](const SizeHint& h) {
            return Json(Json::Object{
                {"kind", "size"}, {"element", h.element}, {"width", h.width}, {"height", h.height}});
          },
          [](const RotateHint& h) {
            return Json(Json::Object{{"kind", "rotate"}, {"element", h.element}, {"degrees", h.degrees}});
          },
      },
      hint);
}

Result<LayoutHint> hint_from_json(const Json& j) {
  Reader r(j, "hint");
  std::string kind = r.str("kind");
  std::optional<LayoutHint> hint;
  if (kind == "pin") {
    hint = PinHint{r.str("element"), r.enumeration("placement", kPlacement, Placement::Below),
                   r.str("reference")};
  } else if (kind == "pinAt") {
    hint = PinAtHint{r.str("element"), r.num("x"), r.num("y"), r.opt_num("width"),
                     r.opt_num("height")};
  } else if (kind == "route") {
    RouteHint h{r.str("relationship"), {}, {}, {}};
    if (const Json* pts = r.array("points"))
      for (const auto& p : pts->as_array())
        if (auto pt = read_point(r, &p, "point")) h.points.push_back(*pt);
    h.exit = read_point(r, r.object("exit"), "exit");
    h.entry = read_point(r, r.object("entry"), "entry");
    hint = std::move(h);
  } else if (kind == "align") {
    hint = AlignHint{r.strings("elements"), r.enumeration("axis", kAxis, Axis::Horizontal)};
  } else if (kind == "order") {
    hint = OrderHint{r.strings("elements")};
  } else if (kind == "size") {
    hint = SizeHint{r.str("element"), r.num("width"), r.num("height")};
  } else if (kind == "rotate") {
    hint = RotateHint{r.str("element"), r.num("degrees")};
  } else if (r.ok()) {
    r.problem("has unknown kind \"" + kind + "\"");
  }
  if (!r.ok()) return r.error();
  return std::move(*hint);
}

Json to_json(const SequenceItem& item) {
  return std::visit(
      overloaded{
          [](const SequenceMessage& m) {
            Json j(Json::Object{{"kind", "message"}, {"from", m.from}, {"to", m.to}});
            set_opt(j, "label", m.label);
            j.set("style", kMessageStyle.name(m.style));
            return j;
          },
          [](const SequenceActivation& a) {
            return Json(Json::Object{{"kind", a.activate ? "activate" : "deactivate"},
                                     {"element", a.element}});
          },
          [](const SequenceNote& n) {
            return Json(Json::Object{{"kind", "note"},
                                     {"position", kNotePosition.name(n.position)},
                                     {"over", strings(n.over)},
                                     {"text", n.text}});
          },
          [](const SequenceDivider& d) {
            Json j(Json::Object{{"kind", "divider"}});
            set_opt(j, "label", d.label);
            return j;
          },
          [](const SequenceDelay& d) {
            Json j(Json::Object{{"kind", "delay"}});
            set_opt(j, "label", d.label);
            return j;
          },
          [](const SequenceRef& r) {
            return Json(
                Json::Object{{"kind", "ref"}, {"over", strings(r.over)}, {"label", r.label}});
          },
          [](const SequenceFrame& f) {
            Json sections = Json::array();
            for (const auto& s : f.sections) {
              Json sj = Json::object();
              set_opt(sj, "label", s.label);
              Json items = Json::array();
              for (const auto& i : s.items) items.push(to_json(i));
              sj.set("items", std::move(items));
              sections.push(std::move(sj));
            }
            return Json(Json::Object{
                {"kind", "frame"}, {"frame", kFrameKind.name(f.frame)}, {"sections", sections}});
          },
      },
      static_cast<const SequenceItem::variant&>(item));
}

Result<SequenceItem> sequence_item_from_json(const Json& j) {
  Reader r(j, "sequence item");
  std::string kind = r.str("kind");
  std::optional<SequenceItem> item;
  if (kind == "message") {
    item = SequenceMessage{r.str("from"), r.str("to"), r.opt_str("label"),
                           r.enumeration("style", kMessageStyle, MessageStyle::Sync)};
  } else if (kind == "activate" || kind == "deactivate") {
    item = SequenceActivation{kind == "activate", r.str("element")};
  } else if (kind == "note") {
    item = SequenceNote{r.enumeration("position", kNotePosition, NotePosition::Over),
                        r.strings("over"), r.str("text")};
  } else if (kind == "divider") {
    item = SequenceDivider{r.opt_str("label")};
  } else if (kind == "delay") {
    item = SequenceDelay{r.opt_str("label")};
  } else if (kind == "ref") {
    item = SequenceRef{r.strings("over"), r.str("label")};
  } else if (kind == "frame") {
    SequenceFrame f{r.enumeration("frame", kFrameKind, FrameKind::Group), {}};
    if (const Json* sections = r.array("sections")) {
      for (const auto& sj : sections->as_array()) {
        Reader s(sj, "frame section");
        FrameSection section{s.opt_str("label"), {}};
        if (const Json* items = s.array("items")) {
          for (const auto& ij : items->as_array()) {
            auto child = sequence_item_from_json(ij);
            if (!child) {
              s.nested(child.error());
              break;
            }
            section.items.push_back(std::move(*child));
          }
        }
        if (!s.ok()) r.nested(s.error());
        f.sections.push_back(std::move(section));
      }
    }
    item = std::move(f);
  } else if (r.ok()) {
    r.problem("has unknown kind \"" + kind + "\"");
  }
  if (!r.ok()) return r.error();
  return std::move(*item);
}

// ── Decorations ────────────────────────────────────────────────────────────────────────────────

Json to_json(const Decoration& d) {
  Json j(Json::Object{{"kind", kDecorationKind.name(d.kind)}});
  set_opt(j, "name", d.name);
  set_opt(j, "text", d.text);
  if (d.placement) j.set("placement", kPlacement.name(*d.placement));
  set_opt(j, "reference", d.reference);
  if (!d.around.empty()) j.set("around", strings(d.around));
  if (d.at) j.set("at", point(*d.at));
  if (d.width) j.set("width", *d.width);
  if (d.height) j.set("height", *d.height);
  if (d.style) j.set("style", string_map(*d.style));
  return j;
}

Result<Decoration> decoration_from_json(const Json& j) {
  Reader r(j, "decoration");
  Decoration d;
  d.kind = r.enumeration("kind", kDecorationKind, DecorationKind::Note);
  d.name = r.opt_str("name");
  d.text = r.opt_str("text");
  d.placement = r.opt_enum("placement", kPlacement);
  d.reference = r.opt_str("reference");
  d.around = r.strings("around");
  d.at = read_point(r, r.object("at"), "at");
  d.width = r.opt_num("width");
  d.height = r.opt_num("height");
  d.style = r.opt_map("style");
  if (!r.ok()) return r.error();
  return d;
}

// ── Views and the model ────────────────────────────────────────────────────────────────────────

Json to_json(const View& v) {
  Json j = Json::object();
  j.set("id", v.id);
  set_opt(j, "type", v.type);
  set_opt(j, "scope", v.scope);
  j.set("include", strings(v.include));
  j.set("exclude", strings(v.exclude));
  j.set("layout", layout_options(v.layout));
  Json hints = Json::array();
  for (const auto& h : v.hints) hints.push(to_json(h));
  j.set("hints", std::move(hints));
  if (v.sequence) {
    Json seq = Json::array();
    for (const auto& i : *v.sequence) seq.push(to_json(i));
    j.set("sequence", std::move(seq));
  }
  if (v.autonumber) j.set("autonumber", *v.autonumber);
  set_opt(j, "theme", v.theme);
  if (!v.decorations.empty()) {
    Json decorations = Json::array();
    for (const auto& d : v.decorations) decorations.push(to_json(d));
    j.set("decorations", std::move(decorations));
  }
  return j;
}

Result<View> view_from_json(const Json& j) {
  Reader r(j, "view");
  View v;
  v.id = r.str("id");
  v.type = r.opt_str("type");
  v.scope = r.opt_str("scope");
  if (r.array("include")) v.include = r.strings("include");
  v.exclude = r.strings("exclude");
  if (const Json* layout = r.object("layout")) {
    Reader l(*layout, "layout");
    v.layout.algorithm = l.opt_str("algorithm").value_or("layered");
    v.layout.direction = l.enumeration("direction", kDirection, Direction::Down);
    v.layout.spacing = l.opt_num("spacing");
    if (!l.ok()) r.nested(l.error());
  }
  if (const Json* hints = r.array("hints")) {
    for (const auto& hj : hints->as_array()) {
      auto h = hint_from_json(hj);
      if (!h) return h.error();
      v.hints.push_back(std::move(*h));
    }
  }
  if (const Json* seq = r.array("sequence")) {
    v.sequence.emplace();
    for (const auto& ij : seq->as_array()) {
      auto item = sequence_item_from_json(ij);
      if (!item) return item.error();
      v.sequence->push_back(std::move(*item));
    }
  }
  v.autonumber = r.opt_bool("autonumber");
  v.theme = r.opt_str("theme");
  if (const Json* decorations = r.array("decorations")) {
    for (const auto& dj : decorations->as_array()) {
      auto d = decoration_from_json(dj);
      if (!d) return d.error();
      v.decorations.push_back(std::move(*d));
    }
  }
  if (!r.ok()) return r.error();
  return v;
}

Json to_json(const StyleRule& rule) {
  Json s = Json::object();
  s.set("target", rule.target == StyleTarget::Relationships ? "relationships" : "elements");
  set_opt(s, "kind", rule.kind);
  set_opt(s, "tag", rule.tag);
  s.set("style", string_map(rule.style));
  return s;
}

Result<StyleRule> style_rule_from_json(const Json& j) {
  Reader s(j, "style");
  StyleRule rule;
  rule.target = s.opt_str("target").value_or("elements") == "relationships" ? StyleTarget::Relationships
                                                                           : StyleTarget::Elements;
  rule.kind = s.opt_str("kind");
  rule.tag = s.opt_str("tag");
  rule.style = s.opt_map("style").value_or(Style{});
  if (!s.ok()) return s.error();
  return rule;
}

Json to_json(const ThemeDef& t) {
  Json j = Json::object();
  j.set("name", t.name);
  set_opt(j, "extends", t.base);
  for (const auto& [k, v] : t.values) j.set(k, v);
  if (!t.nodes.empty()) {
    Json nodes = Json::object();
    for (const auto& [kind, look] : t.nodes) nodes.set(kind, string_map(look));
    j.set("nodes", std::move(nodes));
  }
  return j;
}

Result<ThemeDef> theme_from_json(const Json& j) {
  if (!j.is_object()) return fail("theme is not an object");
  ThemeDef t;
  for (const auto& [k, v] : j.as_object()) {
    if (k == "name" && v.is_string()) {
      t.name = v.as_string();
    } else if (k == "extends" && v.is_string()) {
      t.base = v.as_string();
    } else if (k == "nodes" && v.is_object()) {
      for (const auto& [kind, look] : v.as_object()) {
        Style style;
        if (look.is_object()) {
          for (const auto& [sk, sv] : look.as_object()) {
            if (sv.is_string()) style.set(sk, sv.as_string());
            else if (sv.is_number()) style.set(sk, format_number(sv.as_number()));
          }
        }
        t.nodes.emplace_back(kind, std::move(style));
      }
    } else if (v.is_string()) {
      t.values.set(k, v.as_string());
    } else if (v.is_number()) {
      t.values.set(k, format_number(v.as_number()));
    }
  }
  if (t.name.empty()) return fail("theme has no name");
  return t;
}

Json to_json(const Model& m) {
  Json elements = Json::object(), relationships = Json::object(), groups = Json::object(),
       views = Json::object();
  // Map keys are unique ids, so members are appended without a duplicate scan.
  for (const auto& e : m.elements) elements.add(e.id, to_json(e));
  for (const auto& r : m.relationships) relationships.add(r.id, to_json(r));
  for (const auto& g : m.groups) groups.add(g.id, to_json(g));
  for (const auto& v : m.views) views.add(v.id, to_json(v));
  Json j(Json::Object{{"elements", elements},
                      {"relationships", relationships},
                      {"groups", groups},
                      {"views", views}});
  if (m.libraries) {
    Json libs = Json::array();
    for (const auto& u : *m.libraries) libs.push(library_use(u));
    j.set("libraries", std::move(libs));
  }
  set_opt(j, "theme", m.theme);
  if (!m.styles.empty()) {
    Json styles = Json::array();
    for (const auto& rule : m.styles) styles.push(to_json(rule));
    j.set("styles", std::move(styles));
  }
  if (!m.themes.empty()) {
    Json themes = Json::array();
    for (const auto& t : m.themes) themes.push(to_json(t));
    j.set("themes", std::move(themes));
  }
  return j;
}

Result<Model> model_from_json(const Json& j) {
  Reader r(j, "model");
  Model m;
  auto each = [&](const char* key, auto&& add) -> std::optional<Error> {
    const Json* obj = r.object(key);
    if (!obj) return std::nullopt;
    for (const auto& [_, item] : obj->as_object())
      if (auto err = add(item)) return err;
    return std::nullopt;
  };
  auto err = each("elements", [&](const Json& item) -> std::optional<Error> {
    auto e = element_from_json(item);
    if (!e) return e.error();
    std::string id = e->id;  // before the move: argument order is unspecified
    m.elements.set(std::move(id), std::move(*e));
    return std::nullopt;
  });
  if (!err)
    err = each("relationships", [&](const Json& item) -> std::optional<Error> {
      auto rel = relationship_from_json(item);
      if (!rel) return rel.error();
      std::string id = rel->id;  // before the move: argument order is unspecified
      m.relationships.set(std::move(id), std::move(*rel));
      return std::nullopt;
    });
  if (!err)
    err = each("groups", [&](const Json& item) -> std::optional<Error> {
      auto g = group_from_json(item);
      if (!g) return g.error();
      std::string id = g->id;  // before the move: argument order is unspecified
      m.groups.set(std::move(id), std::move(*g));
      return std::nullopt;
    });
  if (!err)
    err = each("views", [&](const Json& item) -> std::optional<Error> {
      auto v = view_from_json(item);
      if (!v) return v.error();
      std::string id = v->id;  // before the move: argument order is unspecified
      m.views.set(std::move(id), std::move(*v));
      return std::nullopt;
    });
  if (err) return *err;
  if (const Json* libs = r.array("libraries")) {
    m.libraries.emplace();
    for (const auto& lj : libs->as_array()) {
      Reader l(lj, "library");
      m.libraries->push_back({l.str("name"), l.opt_str("range").value_or("*"), l.str("alias")});
      if (!l.ok()) return l.error();
    }
  }
  m.theme = r.opt_str("theme");
  if (const Json* styles = r.array("styles")) {
    for (const auto& sj : styles->as_array()) {
      auto rule = style_rule_from_json(sj);
      if (!rule) return rule.error();
      m.styles.push_back(std::move(*rule));
    }
  }
  if (const Json* themes = r.array("themes")) {
    for (const auto& tj : themes->as_array()) {
      auto t = theme_from_json(tj);
      if (!t) return t.error();
      m.themes.push_back(std::move(*t));
    }
  }
  if (!r.ok()) return r.error();
  return m;
}

// ── Operations ─────────────────────────────────────────────────────────────────────────────────

Json to_json(const Operation& operation) {
  return std::visit(
      overloaded{
          [](const op::AddElement& o) {
            return Json(Json::Object{{"type", "addElement"}, {"element", to_json(o.element)}});
          },
          [](const op::RemoveElement& o) {
            return Json(Json::Object{{"type", "removeElement"}, {"id", o.id}});
          },
          [](const op::RenameElement& o) {
            return Json(Json::Object{{"type", "renameElement"}, {"id", o.id}, {"name", o.name}});
          },
          [](const op::ChangeElementId& o) {
            Json j(Json::Object{{"type", "changeElementId"}, {"id", o.id}, {"to", o.to}});
            for (const auto& [key, renames] : {std::pair{"relationships", &o.relationships}, std::pair{"groups", &o.groups}}) {
              if (renames->empty()) continue;
              Json list = Json::array();
              for (const auto& [from, to] : *renames) list.push(Json(Json::Object{{"from", from}, {"to", to}}));
              j.set(key, std::move(list));
            }
            set_opt(j, "name", o.name);
            return j;
          },
          [](const op::UpdateElement& o) {
            return Json(Json::Object{{"type", "updateElement"}, {"element", to_json(o.element)}});
          },
          [](const op::AddRelationship& o) {
            return Json(Json::Object{{"type", "addRelationship"},
                                     {"relationship", to_json(o.relationship)}});
          },
          [](const op::RemoveRelationship& o) {
            return Json(Json::Object{{"type", "removeRelationship"}, {"id", o.id}});
          },
          [](const op::ReconnectRelationship& o) {
            Json j(Json::Object{{"type", "reconnectRelationship"},
                                {"id", o.id},
                                {"source", endpoint(o.source)},
                                {"target", endpoint(o.target)}});
            if (!o.renames.empty()) {
              Json renames = Json::object();
              for (const auto& [from, to] : o.renames) renames.set(from, to);
              j.set("renames", std::move(renames));
            }
            return j;
          },
          [](const op::SetRelationshipLabel& o) {
            Json j(Json::Object{{"type", "setRelationshipLabel"}, {"id", o.id}});
            set_opt(j, "label", o.label);
            return j;
          },
          [](const op::UpdateRelationship& o) {
            return Json(Json::Object{{"type", "updateRelationship"}, {"relationship", to_json(o.relationship)}});
          },
          [](const op::RenameRelationships& o) {
            Json renames = Json::array();
            for (const auto& r : o.renames) renames.push(Json(Json::Object{{"from", r.from}, {"to", r.to}}));
            return Json(Json::Object{{"type", "renameRelationships"}, {"renames", renames}});
          },
          [](const op::AddGroup& o) {
            return Json(Json::Object{{"type", "addGroup"}, {"group", to_json(o.group)}});
          },
          [](const op::RemoveGroup& o) {
            return Json(Json::Object{{"type", "removeGroup"}, {"id", o.id}});
          },
          [](const op::AddView& o) {
            return Json(Json::Object{{"type", "addView"}, {"view", to_json(o.view)}});
          },
          [](const op::RemoveView& o) {
            return Json(Json::Object{{"type", "removeView"}, {"id", o.id}});
          },
          [](const op::RenameView& o) {
            return Json(Json::Object{{"type", "renameView"}, {"id", o.id}, {"to", o.to}});
          },
          [](const op::ReplaceView& o) {
            return Json(Json::Object{{"type", "replaceView"}, {"view", to_json(o.view)}});
          },
          [](const op::SetLayout& o) {
            return Json(Json::Object{
                {"type", "setLayout"}, {"view", o.view}, {"layout", layout_options(o.layout)}});
          },
          [](const op::AddHint& o) {
            Json j(Json::Object{{"type", "addHint"}, {"view", o.view}, {"hint", to_json(o.hint)}});
            if (o.index) j.set("index", *o.index);
            return j;
          },
          [](const op::RemoveHint& o) {
            return Json(Json::Object{{"type", "removeHint"}, {"view", o.view}, {"index", o.index}});
          },
          [](const op::ReplaceHint& o) {
            return Json(Json::Object{
                {"type", "replaceHint"}, {"view", o.view}, {"index", o.index}, {"hint", to_json(o.hint)}});
          },
          [](const op::UseLibrary& o) {
            Json j(Json::Object{{"type", "useLibrary"}, {"use", library_use(o.use)}});
            if (o.index) j.set("index", *o.index);
            return j;
          },
          [](const op::UnuseLibrary& o) {
            return Json(Json::Object{{"type", "unuseLibrary"}, {"alias", o.alias}});
          },
          [](const op::AddStyleRule& o) {
            Json j(Json::Object{{"type", "addStyleRule"}, {"rule", to_json(o.rule)}});
            if (o.index) j.set("index", *o.index);
            return j;
          },
          [](const op::RemoveStyleRule& o) {
            return Json(Json::Object{{"type", "removeStyleRule"}, {"index", o.index}});
          },
          [](const op::UpdateStyleRule& o) {
            return Json(Json::Object{{"type", "updateStyleRule"}, {"index", o.index}, {"rule", to_json(o.rule)}});
          },
          [](const op::SetTheme& o) {
            Json j(Json::Object{{"type", "setTheme"}});
            set_opt(j, "view", o.view);
            set_opt(j, "theme", o.theme);
            return j;
          },
          [](const op::AddTheme& o) {
            Json j(Json::Object{{"type", "addTheme"}, {"theme", to_json(o.theme)}});
            if (o.index) j.set("index", *o.index);
            return j;
          },
          [](const op::RemoveTheme& o) {
            return Json(Json::Object{{"type", "removeTheme"}, {"name", o.name}});
          },
          [](const op::UpdateTheme& o) {
            return Json(Json::Object{{"type", "updateTheme"}, {"theme", to_json(o.theme)}});
          },
          [](const op::Batch& o) {
            Json ops = Json::array();
            for (const auto& step : o.operations) ops.push(to_json(step));
            return Json(Json::Object{{"type", "batch"}, {"operations", ops}});
          },
      },
      static_cast<const Operation::variant&>(operation));
}

Result<Operation> operation_from_json(const Json& j) {
  Reader r(j, "operation");
  std::string type = r.str("type");
  if (!r.ok()) return r.error();

  auto nested = [&](const char* key, auto parse) -> decltype(parse(j)) {
    const Json* v = r.object(key);
    if (!v) {
      if (r.ok()) r.problem(std::string("needs \"") + key + "\"");
      return r.error();
    }
    return parse(*v);
  };

  if (type == "addElement") {
    auto e = nested("element", element_from_json);
    if (!e) return e.error();
    return Operation{op::AddElement{std::move(*e)}};
  }
  if (type == "removeElement") {
    auto id = r.str("id");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveElement{std::move(id)}};
  }
  if (type == "renameElement") {
    op::RenameElement o{r.str("id"), r.str("name")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "changeElementId") {
    op::ChangeElementId o{r.str("id"), r.str("to"), {}, {}, r.opt_str("name")};
    // `[{ from, to }]`, as renameRelationships writes its renames.
    for (auto [key, into] : {std::pair{"relationships", &o.relationships}, std::pair{"groups", &o.groups}}) {
      const Json* renames = r.array(key);
      if (!renames) continue;
      for (const auto& item : renames->as_array()) {
        Reader ri(item, "rename");
        std::string from = ri.str("from"), to = ri.str("to");
        if (!ri.ok()) return ri.error();
        (*into)[std::move(from)] = std::move(to);
      }
    }
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "updateElement") {
    auto e = nested("element", element_from_json);
    if (!e) return e.error();
    return Operation{op::UpdateElement{std::move(*e)}};
  }
  if (type == "addRelationship") {
    auto rel = nested("relationship", relationship_from_json);
    if (!rel) return rel.error();
    return Operation{op::AddRelationship{std::move(*rel)}};
  }
  if (type == "removeRelationship") {
    auto id = r.str("id");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveRelationship{std::move(id)}};
  }
  if (type == "reconnectRelationship") {
    op::ReconnectRelationship o;
    o.id = r.str("id");
    o.source = read_endpoint(r, r.object("source"), "source");
    o.target = read_endpoint(r, r.object("target"), "target");
    for (const auto& [from, to] : r.opt_map("renames").value_or(StringMap{})) o.renames[from] = to;
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "setRelationshipLabel") {
    op::SetRelationshipLabel o{r.str("id"), r.opt_str("label")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "updateRelationship") {
    auto rel = nested("relationship", relationship_from_json);
    if (!rel) return rel.error();
    return Operation{op::UpdateRelationship{std::move(*rel)}};
  }
  if (type == "renameRelationships") {
    op::RenameRelationships o;
    if (const Json* renames = r.array("renames")) {
      for (const auto& item : renames->as_array()) {
        Reader ri(item, "rename");
        op::RenameRelationships::Rename rename{ri.str("from"), ri.str("to")};
        if (!ri.ok()) return ri.error();
        o.renames.push_back(std::move(rename));
      }
    }
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "addGroup") {
    auto g = nested("group", group_from_json);
    if (!g) return g.error();
    return Operation{op::AddGroup{std::move(*g)}};
  }
  if (type == "removeGroup") {
    auto id = r.str("id");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveGroup{std::move(id)}};
  }
  if (type == "addView" || type == "replaceView") {
    auto v = nested("view", view_from_json);
    if (!v) return v.error();
    if (type == "addView") return Operation{op::AddView{std::move(*v)}};
    return Operation{op::ReplaceView{std::move(*v)}};
  }
  if (type == "removeView") {
    auto id = r.str("id");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveView{std::move(id)}};
  }
  if (type == "renameView") {
    auto id = r.str("id");
    auto to = r.str("to");
    if (!r.ok()) return r.error();
    return Operation{op::RenameView{std::move(id), std::move(to)}};
  }
  if (type == "setLayout") {
    op::SetLayout o{r.str("view"), {}};
    if (const Json* l = r.object("layout")) {
      Reader lr(*l, "layout");
      o.layout.algorithm = lr.opt_str("algorithm").value_or("layered");
      o.layout.direction = lr.enumeration("direction", kDirection, Direction::Down);
      o.layout.spacing = lr.opt_num("spacing");
      if (!lr.ok()) return lr.error();
    }
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "addHint") {
    auto h = nested("hint", hint_from_json);
    if (!h) return h.error();
    op::AddHint o{r.str("view"), std::move(*h), r.opt_index("index")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "removeHint") {
    op::RemoveHint o{r.str("view"), r.opt_index("index").value_or(0)};
    if (!r.opt_num("index") && r.ok()) r.problem("needs \"index\"");
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "replaceHint") {
    auto h = nested("hint", hint_from_json);
    if (!h) return h.error();
    op::ReplaceHint o{r.str("view"), r.opt_index("index").value_or(0), std::move(*h)};
    if (!r.opt_num("index") && r.ok()) r.problem("needs \"index\"");
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "useLibrary") {
    const Json* u = r.object("use");
    if (!u) return fail("operation needs \"use\"");
    Reader ur(*u, "use");
    LibraryUse use{ur.str("name"), ur.opt_str("range").value_or("*"), ur.str("alias")};
    if (!ur.ok()) return ur.error();
    op::UseLibrary o{std::move(use), r.opt_index("index")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "unuseLibrary") {
    auto alias = r.str("alias");
    if (!r.ok()) return r.error();
    return Operation{op::UnuseLibrary{std::move(alias)}};
  }
  if (type == "addStyleRule" || type == "updateStyleRule") {
    auto rule = nested("rule", style_rule_from_json);
    if (!rule) return rule.error();
    auto index = r.opt_index("index");
    if (type == "addStyleRule") {
      if (!r.ok()) return r.error();
      return Operation{op::AddStyleRule{std::move(*rule), index}};
    }
    if (!index && r.ok()) r.problem("needs \"index\"");
    if (!r.ok()) return r.error();
    return Operation{op::UpdateStyleRule{*index, std::move(*rule)}};
  }
  if (type == "removeStyleRule") {
    auto index = r.opt_index("index");
    if (!index && r.ok()) r.problem("needs \"index\"");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveStyleRule{*index}};
  }
  if (type == "setTheme") {
    op::SetTheme o{r.opt_str("view"), r.opt_str("theme")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "addTheme" || type == "updateTheme") {
    auto t = nested("theme", theme_from_json);
    if (!t) return t.error();
    if (type == "updateTheme") return Operation{op::UpdateTheme{std::move(*t)}};
    op::AddTheme o{std::move(*t), r.opt_index("index")};
    if (!r.ok()) return r.error();
    return Operation{std::move(o)};
  }
  if (type == "removeTheme") {
    auto name = r.str("name");
    if (!r.ok()) return r.error();
    return Operation{op::RemoveTheme{std::move(name)}};
  }
  if (type == "batch") {
    op::Batch batch;
    if (const Json* ops = r.array("operations")) {
      for (const auto& oj : ops->as_array()) {
        auto step = operation_from_json(oj);
        if (!step) return step.error();
        batch.operations.push_back(std::move(*step));
      }
    }
    if (!r.ok()) return r.error();
    return Operation{std::move(batch)};
  }
  return fail("operation has unknown type \"", type, "\"");
}

Json to_json(const ResolvedView& rv) {
  Json elements = Json::array(), relationships = Json::array(), groups = Json::array();
  for (const auto& e : rv.elements) elements.push(to_json(e));
  for (const auto& r : rv.relationships) relationships.push(to_json(r));
  for (const auto& g : rv.groups) groups.push(to_json(g));
  return Json(Json::Object{{"view", to_json(rv.view)},
                           {"elements", elements},
                           {"relationships", relationships},
                           {"groups", groups}});
}

}  // namespace dd
