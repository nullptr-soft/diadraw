// Port of packages/editor/src/index.ts, plus the Session facade.

#include "diadraw/editor/editor.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "diadraw/base/json.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/model/views.hpp"

namespace dd::editor {

namespace {

Point center(const Box& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

}  // namespace

bool hint_places(const LayoutHint& hint, std::string_view id) {
  if (const auto* h = std::get_if<PinHint>(&hint)) return h->element == id;
  if (const auto* h = std::get_if<PinAtHint>(&hint)) return h->element == id;
  if (const auto* h = std::get_if<AlignHint>(&hint))
    return std::find(h->elements.begin(), h->elements.end(), id) != h->elements.end();
  return false;
}

std::optional<LayoutHint> hint_for_drop(const Geometry& geometry, const ElementId& id, const Box& dropped,
                                        const std::vector<ElementId>& candidates, bool freeform) {
  if (freeform) return PinAtHint{id, js_round(dropped.x), js_round(dropped.y), std::nullopt, std::nullopt};
  Point c = center(dropped);
  struct Other {
    ElementId id;
    Point c;
    double d;
  };
  std::vector<Other> others;
  for (const auto& o : candidates) {
    if (o == id) continue;
    const Box* box = geometry.nodes.find(o);
    if (!box) continue;
    Point oc = center(*box);
    others.push_back({o, oc, std::hypot(oc.x - c.x, oc.y - c.y)});
  }
  std::stable_sort(others.begin(), others.end(), [](const Other& a, const Other& b) { return a.d < b.d; });
  if (others.empty()) return std::nullopt;

  const Other* align_h = nullptr;
  const Other* align_v = nullptr;
  for (const auto& o : others)
    if (!align_h && std::fabs(o.c.y - c.y) <= kAlignTolerance) align_h = &o;
  for (const auto& o : others)
    if (!align_v && std::fabs(o.c.x - c.x) <= kAlignTolerance) align_v = &o;
  // The nearer of the two alignments wins; on a tie, horizontal (listed first).
  if (align_h && (!align_v || align_h->d <= align_v->d)) return AlignHint{{align_h->id, id}, Axis::Horizontal};
  if (align_v) return AlignHint{{align_v->id, id}, Axis::Vertical};

  const Other& nearest = others.front();
  double dx = c.x - nearest.c.x;
  double dy = c.y - nearest.c.y;
  Placement placement = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? Placement::RightOf : Placement::LeftOf)
                        : dy > 0                     ? Placement::Below
                                                     : Placement::Above;
  return PinHint{id, placement, nearest.id};
}

AdornmentMove adornment_move(const Model& model, const Geometry& geometry, const ElementId& id, Point to) {
  const Element* e = model.elements.find(id);
  std::optional<Anchor> anchor = e ? anchor_of(*e) : std::nullopt;
  const Box* parent = anchor ? geometry.nodes.find(*e->parent) : nullptr;
  if (!parent || !geometry.nodes.contains(id)) return {};
  const double dx = to.x - (parent->x + anchor->at.x * parent->width);
  const double dy = to.y - (parent->y + anchor->at.y * parent->height);
  // Where it is already, an imported fractional offset stays as it is.
  if (!std::isfinite(dx) || !std::isfinite(dy) ||
      (std::fabs(dx - anchor->offset.x) < 0.5 && std::fabs(dy - anchor->offset.y) < 0.5))
    return {true, std::nullopt};
  Element changed = *e;
  changed.style->set("anchorOffset", js_number_string(js_round(dx)) + "," + js_number_string(js_round(dy)));
  return {true, Operation{op::UpdateElement{std::move(changed)}}};
}

Result<Operation> place_operation(const Model& model, std::string_view view_id, const ElementId& id, LayoutHint hint) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  std::vector<std::size_t> placing;
  for (std::size_t i = 0; i < view->hints.size(); ++i)
    if (hint_places(view->hints[i], id)) placing.push_back(i);
  if (auto* at = std::get_if<PinAtHint>(&hint); at && !at->width)
    for (std::size_t i : placing)
      if (const auto* was = std::get_if<PinAtHint>(&view->hints[i]); was && was->width && was->height) {
        at->width = was->width;
        at->height = was->height;
      }
  op::Batch batch;
  // A hint of the same sort (a `pin … at` moved or resized) is rewritten where it stands; the
  // others are removed, last first so the indices hold, and a new sort of hint is appended.
  bool in_place = !placing.empty() && view->hints[placing.back()].index() == hint.index();
  if (in_place) batch.operations.emplace_back(op::ReplaceHint{std::string(view_id), placing.back(), hint});
  for (std::size_t i = placing.size() - (in_place ? 1 : 0); i-- > 0;)
    batch.operations.emplace_back(op::RemoveHint{std::string(view_id), placing[i]});
  if (!in_place) batch.operations.emplace_back(op::AddHint{std::string(view_id), std::move(hint), std::nullopt});
  return Operation{std::move(batch)};
}

Result<std::optional<Operation>> route_operation(const Model& model, std::string_view view_id,
                                                 const RelationshipId& relationship, const std::vector<Point>& points) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  if (!model.relationships.contains(relationship)) return fail("Relationship \"", relationship, "\" not found");
  std::vector<std::size_t> routes;
  for (std::size_t i = 0; i < view->hints.size(); ++i)
    if (const auto* r = std::get_if<RouteHint>(&view->hints[i]); r && r->relationship == relationship)
      routes.push_back(i);
  // The last route wins in a fixed layout, so it is the one whose `from` and `to` stay.
  const RouteHint* was = routes.empty() ? nullptr : &std::get<RouteHint>(view->hints[routes.back()]);
  RouteHint route{relationship, {}, was ? was->exit : std::nullopt, was ? was->entry : std::nullopt};
  for (const auto& p : points) route.points.push_back({js_round(p.x), js_round(p.y)});
  bool empty = route.points.empty() && !route.exit && !route.entry;
  if (was && routes.size() == 1 && *was == route) return std::optional<Operation>{};
  if (!was && empty) return std::optional<Operation>{};
  op::Batch batch;
  if (!empty) {
    if (was) batch.operations.emplace_back(op::ReplaceHint{std::string(view_id), routes.back(), std::move(route)});
    else batch.operations.emplace_back(op::AddHint{std::string(view_id), std::move(route), std::nullopt});
  }
  for (std::size_t i = routes.size(); i-- > 0;)
    if (empty || i + 1 < routes.size())
      batch.operations.emplace_back(op::RemoveHint{std::string(view_id), routes[i]});
  if (batch.operations.size() == 1) return std::optional<Operation>{std::move(batch.operations.front())};
  return std::optional<Operation>{Operation{std::move(batch)}};
}

Result<std::optional<Operation>> move_operation(const Model& model, std::string_view view_id, const Geometry& geometry,
                                                const std::vector<ElementId>& ids, Point offset) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  // A NaN or infinite offset would be printed into the text as `null`.
  if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) return fail("The offset must be finite");
  if (offset.x == 0 && offset.y == 0) return std::optional<Operation>{};
  const auto& hints = view->hints;
  // What places each element, found in one pass (an `align` places all of its elements).
  std::map<std::string_view, std::vector<std::size_t>, std::less<>> placing;
  for (std::size_t i = 0; i < hints.size(); ++i) {
    if (const auto* pin = std::get_if<PinHint>(&hints[i])) placing[pin->element].push_back(i);
    else if (const auto* at = std::get_if<PinAtHint>(&hints[i])) placing[at->element].push_back(i);
    else if (const auto* align = std::get_if<AlignHint>(&hints[i]))
      for (const auto& e : align->elements) placing[e].push_back(i);
  }
  // The same hints `place_operation` would leave, planned against the view as it stands: every
  // rewrite first (a rewrite keeps every index), then the removals last first, then the new hints.
  // A run of rewrites patches the text in one pass.
  std::vector<Operation> replaced, added;
  std::set<std::size_t, std::greater<>> removed;
  std::set<std::string_view, std::less<>> moving;
  for (const auto& id : ids)
    if (geometry.nodes.contains(id) && model.elements.contains(id)) moving.insert(id);
  // The drawn children of each container (only a fixed view wraps unpinned containers round them).
  bool fixed = view->layout.algorithm == "fixed";
  std::map<std::string_view, std::vector<std::string_view>, std::less<>> children;
  // Adornments ride on their parents and do not count: they neither make nor size a container.
  if (fixed)
    for (const auto& e : model.elements)
      if (e.parent && !anchor_of(e) && geometry.nodes.contains(e.id) && geometry.nodes.contains(*e.parent))
        children[*e.parent].push_back(e.id);
  std::set<std::string_view, std::less<>> done;
  for (const auto& id : ids) {
    if (!moving.count(id) || !done.insert(id).second) continue;
    const Box* box = geometry.nodes.find(id);
    if (fixed) {
      // An adornment rides on its parent: moved with it, it needs nothing; moved alone, it keeps
      // its parent and its spot on it, offset to where it was dropped.
      AdornmentMove adorned = adornment_move(model, geometry, id, {box->x + offset.x, box->y + offset.y});
      if (adorned.adornment) {
        // An adornment exists and has a drawn parent.
        const Element* e = model.elements.find(id);
        if (!moving.count(*e->parent) && adorned.change) added.push_back(std::move(*adorned.change));
        continue;
      }
    }
    PinAtHint pin{id, js_round(box->x + offset.x), js_round(box->y + offset.y), std::nullopt, std::nullopt};
    auto found = placing.find(std::string_view(id));
    std::vector<std::size_t> own = found == placing.end() ? std::vector<std::size_t>{} : found->second;
    bool pinned_at = std::any_of(own.begin(), own.end(),
                                 [&](std::size_t i) { return std::holds_alternative<PinAtHint>(hints[i]); });
    if (auto kids = children.find(std::string_view(id)); fixed && kids != children.end() && !pinned_at) {
      // A fixed view wraps an unpinned container round its children: when they all move, it
      // follows them as it is; otherwise it is pinned at its drawn size, as the drag showed it.
      if (std::all_of(kids->second.begin(), kids->second.end(), [&](std::string_view k) { return moving.count(k); }))
        continue;
      pin.width = std::max(1.0, js_round(box->width));
      pin.height = std::max(1.0, js_round(box->height));
    }
    for (std::size_t i : own)
      if (const auto* was = std::get_if<PinAtHint>(&hints[i]); was && was->width && was->height) {
        pin.width = was->width;
        pin.height = was->height;
      }
    bool in_place = !own.empty() && std::holds_alternative<PinAtHint>(hints[own.back()]);
    if (in_place) {
      // A move that rounds to nothing (a fraction of a unit at a high zoom) leaves its pin alone.
      if (!(own.size() == 1 && std::get<PinAtHint>(hints[own.back()]) == pin))
        replaced.emplace_back(op::ReplaceHint{std::string(view_id), own.back(), std::move(pin)});
      own.pop_back();
    } else {
      added.emplace_back(op::AddHint{std::string(view_id), std::move(pin), std::nullopt});
    }
    removed.insert(own.begin(), own.end());
  }
  if (fixed) {
    // A line whose two ends both move keeps its shape: its bend points move with them. Its last
    // route is the one a fixed layout draws; earlier ones go, as `route_operation` leaves them.
    std::map<std::string_view, std::vector<std::size_t>, std::less<>> routes;
    for (std::size_t i = 0; i < hints.size(); ++i)
      if (const auto* route = std::get_if<RouteHint>(&hints[i])) routes[route->relationship].push_back(i);
    for (const auto& [id, at] : routes) {
      const auto& last = std::get<RouteHint>(hints[at.back()]);
      const Relationship* r = model.relationships.find(id);
      if (last.points.empty() || !r || !moving.count(r->source.element) || !moving.count(r->target.element)) continue;
      RouteHint shifted{last.relationship, {}, last.exit, last.entry};
      shifted.points.reserve(last.points.size());
      for (const auto& p : last.points) shifted.points.push_back({js_round(p.x + offset.x), js_round(p.y + offset.y)});
      if (!(at.size() == 1 && shifted == last))
        replaced.emplace_back(op::ReplaceHint{std::string(view_id), at.back(), std::move(shifted)});
      removed.insert(at.begin(), at.end() - 1);
    }
  }
  std::vector<Operation> steps = std::move(replaced);
  for (std::size_t i : removed) steps.emplace_back(op::RemoveHint{std::string(view_id), i});
  for (auto& step : added) steps.push_back(std::move(step));
  if (steps.empty()) return std::optional<Operation>{};
  if (steps.size() == 1) return std::optional<Operation>{std::move(steps.front())};
  return std::optional<Operation>{Operation{op::Batch{std::move(steps)}}};
}

namespace {

/// Rewrites the last hint of `T`'s sort about `id` where it stands (or appends one), and removes
/// the earlier ones; with nullopt removes them all. Nothing when that is how the view reads already.
template <class T>
Result<std::optional<Operation>> subject_operation(const Model& model, std::string_view view_id, const ElementId& id,
                                                   std::optional<T> hint) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  if (!model.elements.contains(id)) return fail("Element \"", id, "\" not found");
  std::vector<std::size_t> found;
  for (std::size_t i = 0; i < view->hints.size(); ++i)
    if (const auto* h = std::get_if<T>(&view->hints[i]); h && h->element == id) found.push_back(i);
  if (!hint && found.empty()) return std::optional<Operation>{};
  if (hint && found.size() == 1 && std::get<T>(view->hints[found[0]]) == *hint) return std::optional<Operation>{};
  op::Batch batch;
  if (hint) {
    if (!found.empty()) batch.operations.emplace_back(op::ReplaceHint{std::string(view_id), found.back(), *hint});
    else batch.operations.emplace_back(op::AddHint{std::string(view_id), *hint, std::nullopt});
  }
  // Last first, so the indices hold.
  for (std::size_t i = found.size(); i-- > 0;)
    if (!hint || i + 1 < found.size()) batch.operations.emplace_back(op::RemoveHint{std::string(view_id), found[i]});
  if (batch.operations.size() == 1) return std::optional<Operation>{std::move(batch.operations.front())};
  return std::optional<Operation>{Operation{std::move(batch)}};
}

}  // namespace

Result<std::optional<Operation>> size_operation(const Model& model, std::string_view view_id, const ElementId& id,
                                                std::optional<Size> size) {
  std::optional<SizeHint> hint;
  if (size) hint = SizeHint{id, std::max(1.0, js_round(size->width)), std::max(1.0, js_round(size->height))};
  return subject_operation(model, view_id, id, hint);
}

Result<std::optional<Operation>> fit_operation(const Model& model, std::string_view view_id,
                                               const std::vector<ElementId>& ids) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  for (const auto& id : ids)
    if (!model.elements.contains(id)) return fail("Element \"", id, "\" not found");
  const std::set<std::string_view> fitted(ids.begin(), ids.end());
  // In a fixed view a container without a pin wraps its children; with one it keeps its box.
  std::set<std::string, std::less<>> containers;
  if (view->layout.algorithm == "fixed") {
    auto resolved = resolve_view(model, view_id);
    if (!resolved) return resolved.error();
    // An adornment does not make its parent a container.
    for (const auto& e : resolved->elements)
      if (e.parent && !anchor_of(e) && fitted.contains(*e.parent)) containers.insert(*e.parent);
  }
  op::Batch batch;
  // Last first, so the indices hold.
  for (std::size_t i = view->hints.size(); i-- > 0;) {
    const LayoutHint& hint = view->hints[i];
    if (const auto* size = std::get_if<SizeHint>(&hint); size && fitted.contains(size->element)) {
      batch.operations.emplace_back(op::RemoveHint{std::string(view_id), i});
    } else if (const auto* pin = std::get_if<PinAtHint>(&hint); pin && fitted.contains(pin->element)) {
      if (containers.contains(pin->element)) {
        batch.operations.emplace_back(op::RemoveHint{std::string(view_id), i});
      } else if (pin->width || pin->height) {
        PinAtHint placed{pin->element, pin->x, pin->y, std::nullopt, std::nullopt};
        batch.operations.emplace_back(op::ReplaceHint{std::string(view_id), i, std::move(placed)});
      }
    }
  }
  if (batch.operations.empty()) return std::optional<Operation>{};
  if (batch.operations.size() == 1) return std::optional<Operation>{std::move(batch.operations.front())};
  return std::optional<Operation>{Operation{std::move(batch)}};
}

Result<std::optional<Operation>> rotate_operation(const Model& model, std::string_view view_id, const ElementId& id,
                                                  double degrees) {
  double d = std::fmod(degrees, 360);
  if (d > 180) d -= 360;
  if (d <= -180) d += 360;
  d = js_round(d * 10) / 10;
  if (d == -180) d = 180;
  std::optional<RotateHint> hint;
  if (d != 0) hint = RotateHint{id, d};
  return subject_operation(model, view_id, id, hint);
}

ElementId fresh_element_id(const Model& model, std::string_view base, const std::optional<ElementId>& parent) {
  std::string local;
  for (char c : base)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') local.push_back(c);
  if (local.empty()) local = "node";
  std::string prefix = parent ? *parent + "." : "";
  for (int n = 1;; ++n) {
    std::string id = prefix + local + std::to_string(n);
    if (!model.elements.contains(id)) return id;
  }
}

Created create_operation(const Model& model, const std::string& kind, const std::optional<ElementId>& parent,
                         const std::optional<std::string>& name, const std::optional<std::string>& shape) {
  // A library kind (`alias.kind`) names its elements after the kind, not the alias.
  std::string base = kind.substr(kind.rfind('.') == std::string::npos ? 0 : kind.rfind('.') + 1);
  ElementId id = fresh_element_id(model, base, parent);
  Element e = make_element(id, kind, name.value_or("New " + base), parent);
  e.shape = shape;
  return {id, op::AddElement{std::move(e)}};
}

Operation connect_operation(const Model& model, const ElementId& source, const ElementId& target,
                            const std::optional<std::string>& label) {
  Endpoint from{source, std::nullopt}, to{target, std::nullopt};
  int index = 1;
  while (model.relationships.contains(dsl::relationship_id(from, to, index))) ++index;
  return op::AddRelationship{make_relationship(dsl::relationship_id(from, to, index), from, to, label)};
}

namespace {

/// The name a relationship had in the text: its id when that is a name, else nothing (derived).
std::optional<std::string> name_of(const Relationship& relationship) {
  if (dsl::is_identifier(relationship.id)) return relationship.id;
  return std::nullopt;
}

/// Why relationship `existing` cannot be given `name` (nothing: derive its id from the ends).
std::optional<Error> name_problem(const Model& model, const Relationship& existing,
                                  const std::optional<std::string>& name) {
  if (!name || name == name_of(existing)) return std::nullopt;
  if (!dsl::is_identifier(*name))
    return Error{"\"" + *name + "\" is not a name: use letters, digits and _, not starting with a digit"};
  if (model.relationships.contains(*name)) return Error{"Another relationship is already named \"" + *name + "\""};
  return std::nullopt;
}

}  // namespace

Result<std::optional<Operation>> update_relationship_operation(const Model& model, const Relationship& relationship,
                                                               const std::optional<std::string>& name,
                                                               RelationshipId* renamed) {
  const Relationship* existing = model.relationships.find(relationship.id);
  if (!existing) return fail("Relationship \"", relationship.id, "\" not found");
  if (renamed) *renamed = relationship.id;
  std::optional<std::string> was = name_of(*existing);

  op::RenameRelationships rename;
  RelationshipId id = relationship.id;
  if (name != was) {
    if (auto problem = name_problem(model, *existing, name)) return *problem;
    // Ids as reading the text back gives them: names stay, derived ids count the lines between
    // the same ends declared before them.
    std::set<std::string, std::less<>> assigned;
    for (const auto& r : model.relationships) {
      std::string now;
      if (r.id == relationship.id && name) {
        now = *name;
      } else if (r.id != relationship.id && dsl::is_identifier(r.id)) {
        now = r.id;
      } else {
        int index = 1;
        while (assigned.count(dsl::relationship_id(r.source, r.target, index))) ++index;
        now = dsl::relationship_id(r.source, r.target, index);
      }
      assigned.insert(now);
      if (r.id == relationship.id) id = now;
      if (now != r.id) rename.renames.push_back({r.id, now});
    }
  }

  Relationship updated = relationship;
  updated.id = id;
  Relationship renamed_existing = *existing;
  renamed_existing.id = id;
  std::vector<Operation> steps;
  if (!rename.renames.empty()) steps.emplace_back(std::move(rename));
  if (!(updated == renamed_existing)) steps.emplace_back(op::UpdateRelationship{std::move(updated)});
  if (renamed) *renamed = id;
  if (steps.empty()) return std::optional<Operation>{};
  if (steps.size() == 1) return std::optional<Operation>{std::move(steps.front())};
  return std::optional<Operation>{Operation{op::Batch{std::move(steps)}}};
}

std::optional<std::string> element_id_problem(std::string_view source, const dsl::ParseResult& parsed,
                                              const ElementId& id, std::string_view local_id) {
  // A dry run of the rename: it patches a copy of the text and reads it back, only for a name that
  // passes the cheap checks before it.
  auto rename = rename_element(source, parsed, id, local_id);
  if (rename) return std::nullopt;
  return std::move(rename.error().message);
}

std::vector<FieldProblem> element_problems(std::string_view source, const dsl::ParseResult& parsed,
                                           const Element& edited, std::string_view local_id) {
  std::vector<FieldProblem> out;
  if (!parsed.model.elements.contains(edited.id)) return out;
  if (auto problem = element_id_problem(source, parsed, edited.id, local_id)) out.push_back({"id", std::move(*problem)});
  auto name_problem = [](std::string_view what, const std::string& value) {
    return "\"" + value + "\" is not a " + std::string(what) +
           ": use letters, digits and _, or alias.name from a library";
  };
  if (edited.kind.empty())
    out.push_back({"kind", "Every element has a kind: container, person, or one from a library"});
  else if (!is_qualified_name(edited.kind))
    out.push_back({"kind", name_problem("kind", edited.kind)});
  if (edited.shape && !is_qualified_name(*edited.shape)) out.push_back({"shape", name_problem("shape", *edited.shape)});
  if (edited.style)
    if (auto why = inline_style_problem(*edited.style)) out.push_back({"style", std::move(*why)});
  return out;
}

std::vector<FieldProblem> relationship_problems(const Model& model, const Relationship& edited,
                                                const std::optional<std::string>& name) {
  std::vector<FieldProblem> out;
  const Relationship* existing = model.relationships.find(edited.id);
  if (!existing) return out;
  if (auto problem = name_problem(model, *existing, name)) out.push_back({"name", std::move(problem->message)});
  if (edited.style)
    if (auto why = inline_style_problem(*edited.style)) out.push_back({"style", std::move(*why)});
  return out;
}

Result<std::optional<Operation>> reconnect_operation(const dsl::ParseResult& parsed, const RelationshipId& id, End end,
                                                     const Endpoint& to) {
  const Model& model = parsed.model;
  const Relationship* existing = model.relationships.find(id);
  if (!existing) return Error{"Relationship \"" + id + "\" not found"};
  op::ReconnectRelationship op{id, existing->source, existing->target, {}};
  (end == End::Source ? op.source : op.target) = to;
  if (op.source == existing->source && op.target == existing->target) return std::optional<Operation>{};
  // Derive every unnamed id again in declaration order, as reading the text back will: the moved
  // line takes its new endpoints' id, and parallel lines on either pair may renumber.
  auto named = [&](const RelationshipId& rid) {
    auto source = parsed.source_map.relationships.find(rid);
    return source == parsed.source_map.relationships.end() || source->second.id.has_value();
  };
  std::set<std::string, std::less<>> taken;
  for (const auto& r : model.relationships) {
    if (named(r.id)) continue;
    const Endpoint& source = r.id == id ? op.source : r.source;
    const Endpoint& target = r.id == id ? op.target : r.target;
    int index = 1;
    while (taken.count(dsl::relationship_id(source, target, index))) ++index;
    std::string derived = dsl::relationship_id(source, target, index);
    taken.insert(derived);
    if (derived != r.id) op.renames[r.id] = derived;
  }
  return std::optional<Operation>{std::move(op)};
}

std::optional<Operation> delete_operation(const Model& model, const Selection& selection) {
  std::set<std::string, std::less<>> selected;
  for (const auto& id : selection.elements)
    if (model.elements.contains(id)) selected.insert(id);
  auto covered = [&](const ElementId& id) {
    const Element* e = model.elements.find(id);
    for (auto p = e ? e->parent : std::nullopt; p;) {
      if (selected.count(*p)) return true;
      const Element* parent = model.elements.find(*p);
      p = parent ? parent->parent : std::nullopt;
    }
    return false;
  };
  // Roots in selection order (as the TypeScript Set keeps them).
  std::vector<ElementId> roots;
  std::set<std::string, std::less<>> seen;
  for (const auto& id : selection.elements)
    if (selected.count(id) && !seen.count(id) && !covered(id)) {
      roots.push_back(id);
      seen.insert(id);
    }
  std::set<std::string, std::less<>> doomed;
  for (const auto& root : roots)
    for (const auto& id : descendants_of(model, root)) doomed.insert(id);
  op::Batch batch;
  for (const auto& rid : selection.relationships) {
    const Relationship* r = model.relationships.find(rid);
    if (r && !doomed.count(r->source.element) && !doomed.count(r->target.element))
      batch.operations.emplace_back(op::RemoveRelationship{rid});
  }
  for (const auto& id : roots) batch.operations.emplace_back(op::RemoveElement{id});
  if (batch.operations.empty()) return std::nullopt;
  if (batch.operations.size() == 1) return batch.operations.front();
  return Operation{std::move(batch)};
}

ViewId fresh_view_id(const Model& model) {
  for (int n = 1;; ++n) {
    std::string id = "view" + std::to_string(n);
    if (!model.views.contains(id)) return id;
  }
}

namespace {

/// Why `id` cannot name a new view of `model`, or nothing when it can.
std::optional<Error> bad_view_id(const Model& model, std::string_view id) {
  if (!dsl::is_identifier(id))
    return fail("\"", id, "\" is not a view name: use letters, digits and _, not starting with a digit");
  if (model.views.contains(id)) return fail("View \"", id, "\" already exists");
  return std::nullopt;
}

}  // namespace

Result<Operation> add_view_operation(const Model& declared, const ViewId& id, const std::optional<ElementId>& scope) {
  Model drawn = with_default_view(declared);
  if (auto bad = bad_view_id(drawn, id)) return *bad;
  if (scope && !declared.elements.contains(*scope)) return fail("Element \"", *scope, "\" not found");
  Operation add = op::AddView{make_view(id, scope)};
  if (!declared.views.empty()) return add;
  return Operation{op::Batch{{op::AddView{make_view(std::string(kDefaultViewId))}, std::move(add)}}};
}

Result<Operation> remove_view_operation(const Model& declared, const ViewId& id) {
  if (!declared.views.contains(id)) return fail("View \"", id, "\" not found");
  if (declared.views.size() == 1) return fail("View \"", id, "\" is the only view; a document keeps one");
  return Operation{op::RemoveView{id}};
}

Result<std::optional<Operation>> rename_view_operation(const Model& declared, const ViewId& id, const ViewId& to) {
  Model drawn = with_default_view(declared);
  if (!drawn.views.contains(id)) return fail("View \"", id, "\" not found");
  if (to == id) return std::optional<Operation>{};
  if (auto bad = bad_view_id(drawn, to)) return *bad;
  // The implied view is written out and then renamed, so the session moves its layout along.
  if (declared.views.empty())
    return std::optional<Operation>{op::Batch{
        {op::AddView{make_view(std::string(kDefaultViewId))}, op::RenameView{std::string(kDefaultViewId), to}}}};
  return std::optional<Operation>{op::RenameView{id, to}};
}

std::vector<ElementId> drop_candidates(const Model& model, const std::vector<ElementId>& visible) {
  std::set<std::string, std::less<>> parents;
  for (const auto& id : visible)
    if (const Element* e = model.elements.find(id); e && e->parent) parents.insert(*e->parent);
  std::vector<ElementId> out;
  for (const auto& id : visible)
    if (!parents.count(id)) out.push_back(id);
  return out;
}

// ── Session ─────────────────────────────────────────────────────────────────────────────────────

Session::Session(std::string source, ShapeProvider shapes)
    : provider_(std::move(shapes)), source_(std::move(source)), measure_(create_measure()) {
  reparse();
}

void Session::update_shapes(bool force) {
  if (!provider_) return;
  if (!force && resolved_ && uses_ == model_.libraries) return;
  uses_ = model_.libraries;
  resolved_ = true;
  art_ = provider_(model_);
  measure_ = create_measure(default_theme(), art_.shapes, art_.kinds);
}

void Session::refresh_libraries() {
  update_shapes(true);
  if (listener_) listener_(SessionEvent{SessionEvent::Kind::Libraries, {}, nullptr});
}

void Session::reparse() {
  parsed_ = dsl::parse(source_);
  model_ = with_default_view(parsed_.model);
  update_shapes();
}

void Session::set_source(std::string source) {
  std::string before = std::exchange(source_, std::move(source));
  reparse();
  if (listener_) listener_(SessionEvent{SessionEvent::Kind::Source, before, nullptr});
}

Result<ViewRender> Session::render(std::string_view view_id) {
  auto view = resolve_view(model_, view_id);
  if (!view) return view.error();
  // Labels are measured in the theme's font sizes.
  Theme theme = theme_for(*view, art_.themes);
  const Theme& base = default_theme();
  bool own_fonts = theme.font_size != base.font_size || theme.small_font_size != base.small_font_size;
  Measure themed = own_fonts ? create_measure(theme, art_.shapes, art_.kinds) : Measure{};
  const Measure& measure = own_fonts ? themed : measure_;
  auto last = last_.find(view_id);
  Geometry geometry = layout_nodes(*view, measure, last == last_.end() ? nullptr : &last->second);
  place_decorations(geometry, *view, measure_);
  last_.insert_or_assign(std::string(view_id), geometry);
  RenderOptions options;
  options.shapes = art_.shapes;
  options.kinds = art_.kinds;
  options.theme = &theme;
  SvgScene scene = render_scene(*view, geometry, options);
  return ViewRender{std::move(*view), std::move(geometry), std::move(scene)};
}

namespace {

/// Whether an operation (or any step of a batch) changes the view with this id.
bool touches_view(const Operation& op, std::string_view id) {
  return std::visit(
      [&](const auto& o) -> bool {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, op::Batch>) {
          return std::any_of(o.operations.begin(), o.operations.end(),
                             [&](const Operation& step) { return touches_view(step, id); });
        } else if constexpr (requires { o.view; }) {
          if constexpr (std::is_convertible_v<decltype(o.view), std::string_view>)
            return std::string_view(o.view) == id;
          else
            return false;
        } else {
          return false;
        }
      },
      static_cast<const Operation::variant&>(op));
}

/// The view renames an operation makes (a batch's in order), as (from, to) pairs.
void view_renames(const Operation& op, std::vector<std::pair<ViewId, ViewId>>& out) {
  if (const auto* batch = std::get_if<op::Batch>(&op)) {
    for (const auto& step : batch->operations) view_renames(step, out);
  } else if (const auto* rename = std::get_if<op::RenameView>(&op)) {
    out.emplace_back(rename->id, rename->to);
  }
}

}  // namespace

Result<dsl::SourceUpdate> Session::apply(const Operation& requested) {
  // A document without views is drawn in an implied `view default`, which is not in the text. A
  // change to that view (a drag, a resize) writes the view into the text first, so it has
  // somewhere to go.
  Operation op = requested;
  if (parsed_.model.views.empty() && touches_view(requested, kDefaultViewId)) {
    op::Batch batch;
    batch.operations.push_back(op::AddView{make_view(std::string(kDefaultViewId))});
    if (const auto* inner = std::get_if<op::Batch>(&requested))
      batch.operations.insert(batch.operations.end(), inner->operations.begin(), inner->operations.end());
    else
      batch.operations.push_back(requested);
    op = std::move(batch);
  }
  auto update = dsl::update_source(source_, op, &parsed_);
  if (!update) return update.error();
  return adopt(op, std::move(*update));
}

dsl::SourceUpdate Session::adopt(const Operation& op, dsl::SourceUpdate update) {
  std::string before = std::exchange(source_, update.source);
  parsed_ = update.result;
  // A renamed view keeps its last layout, so it is drawn as it was and no later view of the old
  // name starts from it.
  std::vector<std::pair<ViewId, ViewId>> renames;
  view_renames(op, renames);
  for (const auto& [from, to] : renames) {
    auto node = last_.extract(from);
    last_.erase(to);
    if (node) {
      node.key() = to;
      last_.insert(std::move(node));
    }
  }
  model_ = with_default_view(parsed_.model);
  update_shapes();
  if (listener_) listener_(SessionEvent{SessionEvent::Kind::Source, before, &update});
  return update;
}

Result<const Geometry*> Session::last_geometry(std::string_view view_id) {
  auto last = last_.find(view_id);
  if (last == last_.end()) {
    auto rendered = render(view_id);
    if (!rendered) return rendered.error();
    last = last_.find(view_id);
    if (last == last_.end()) return fail("View \"", view_id, "\" has no layout");
  }
  return &last->second;
}

Result<std::optional<LayoutHint>> Session::drop_hint(std::string_view view_id, const ElementId& id,
                                                     const Box& dropped, bool freeform) {
  auto last = last_geometry(view_id);
  if (!last) return last.error();
  auto view = resolve_view(model_, view_id);
  if (!view) return view.error();
  // A fixed layout ignores relative hints, so a node goes exactly where it is dropped; an
  // adornment gets no hint at all, it keeps riding on its parent.
  if (view->view.layout.algorithm == "fixed") {
    if (adornment_move(model_, **last, id, {dropped.x, dropped.y}).adornment) return std::optional<LayoutHint>{};
    freeform = true;
  }
  std::vector<ElementId> visible;
  for (const auto& e : view->elements) visible.push_back(e.id);
  return hint_for_drop(**last, id, dropped, drop_candidates(model_, visible), freeform);
}

Result<std::string> Session::export_svg(std::string_view view_id) {
  auto r = render(view_id);
  if (!r) return r.error();
  RenderOptions options;
  options.shapes = art_.shapes;
  options.kinds = art_.kinds;
  options.themes = art_.themes;
  return render_svg(r->view, r->geometry, options);
}

Result<std::optional<dsl::SourceUpdate>> Session::drop(std::string_view view_id, const ElementId& id,
                                                       const Box& dropped, bool freeform) {
  if (const View* view = model_.views.find(view_id); view && view->layout.algorithm == "fixed") {
    auto last = last_geometry(view_id);
    if (!last) return last.error();
    if (AdornmentMove adorned = adornment_move(model_, **last, id, {dropped.x, dropped.y}); adorned.adornment) {
      if (!adorned.change) return std::optional<dsl::SourceUpdate>{};
      auto update = apply(*adorned.change);
      if (!update) return update.error();
      return std::optional<dsl::SourceUpdate>{std::move(*update)};
    }
  }
  auto hint = drop_hint(view_id, id, dropped, freeform);
  if (!hint) return hint.error();
  if (!*hint) return std::optional<dsl::SourceUpdate>{};
  auto op = place_operation(model_, view_id, id, std::move(**hint));
  if (!op) return op.error();
  auto update = apply(*op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<dsl::SourceUpdate> Session::connect(const ElementId& source, const ElementId& target,
                                           const std::optional<std::string>& label) {
  return apply(connect_operation(model_, source, target, label));
}

Result<std::pair<ElementId, dsl::SourceUpdate>> Session::create(const std::string& kind,
                                                                const std::optional<ElementId>& parent,
                                                                const std::optional<std::string>& name,
                                                                const std::optional<std::string>& shape) {
  Created c = create_operation(model_, kind, parent, name, shape);
  auto update = apply(c.operation);
  if (!update) return update.error();
  return std::pair{c.id, std::move(*update)};
}

Result<std::optional<dsl::SourceUpdate>> Session::move(std::string_view view_id, const std::vector<ElementId>& ids,
                                                       Point offset) {
  auto last = last_geometry(view_id);
  if (!last) return last.error();
  auto op = move_operation(model_, view_id, **last, ids, offset);
  if (!op) return op.error();
  if (!*op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<dsl::SourceUpdate>> Session::remove(const Selection& selection) {
  auto op = delete_operation(model_, selection);
  if (!op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(*op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

namespace {

Result<const View*> fixed_view(const Model& model, std::string_view view_id, std::string_view what) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");
  if (view->layout.algorithm != "fixed")
    return fail(what, " works in views with `layout fixed`; this one is laid out automatically");
  return view;
}

}  // namespace

Result<std::optional<dsl::SourceUpdate>> Session::resize(std::string_view view_id, const ElementId& id,
                                                         const Box& box) {
  const View* found = model_.views.find(view_id);
  if (!found) return Error{"View \"" + std::string(view_id) + "\" not found"};
  if (!model_.elements.contains(id)) return Error{"Element \"" + id + "\" not found"};
  if (found->layout.algorithm != "fixed") {
    auto op = size_operation(model_, view_id, id, Size{box.width, box.height});
    if (!op) return op.error();
    if (!*op) return std::optional<dsl::SourceUpdate>{};
    auto update = apply(**op);
    if (!update) return update.error();
    return std::optional<dsl::SourceUpdate>{std::move(*update)};
  }
  auto view = fixed_view(model_, view_id, "Resizing");
  if (!view) return view.error();
  if (const Element* e = model_.elements.find(id); e && anchor_of(*e)) {
    // An adornment keeps its spot on its parent: the new corner goes into its anchor offset and
    // the new size into `size`.
    auto last = last_geometry(view_id);
    if (!last) return last.error();
    if (AdornmentMove adorned = adornment_move(model_, **last, id, {box.x, box.y}); adorned.adornment) {
      auto sized = size_operation(model_, view_id, id, Size{box.width, box.height});
      if (!sized) return sized.error();
      std::vector<Operation> steps;
      if (adorned.change) steps.push_back(std::move(*adorned.change));
      if (*sized) steps.push_back(std::move(**sized));
      if (steps.empty()) return std::optional<dsl::SourceUpdate>{};
      auto update = apply(steps.size() == 1 ? std::move(steps.front()) : Operation{op::Batch{std::move(steps)}});
      if (!update) return update.error();
      return std::optional<dsl::SourceUpdate>{std::move(*update)};
    }
  }
  PinAtHint pin{id, js_round(box.x), js_round(box.y), std::max(1.0, js_round(box.width)),
                std::max(1.0, js_round(box.height))};
  const auto& hints = (*view)->hints;
  for (std::size_t i = hints.size(); i-- > 0;)
    if (hint_places(hints[i], id)) {
      if (hints[i] == LayoutHint{pin}) return std::optional<dsl::SourceUpdate>{};
      break;
    }
  auto op = place_operation(model_, view_id, id, std::move(pin));
  if (!op) return op.error();
  auto update = apply(*op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<dsl::SourceUpdate>> Session::fit(std::string_view view_id, const std::vector<ElementId>& ids) {
  auto op = fit_operation(model_, view_id, ids);
  if (!op) return op.error();
  if (!*op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<dsl::SourceUpdate>> Session::rotate(std::string_view view_id, const ElementId& id,
                                                         double degrees) {
  auto op = rotate_operation(model_, view_id, id, degrees);
  if (!op) return op.error();
  if (!*op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<dsl::SourceUpdate>> Session::reroute(std::string_view view_id, const RelationshipId& relationship,
                                                          const std::vector<Point>& points) {
  auto view = fixed_view(model_, view_id, "Moving bend points");
  if (!view) return view.error();
  auto op = route_operation(model_, view_id, relationship, points);
  if (!op) return op.error();
  if (!*op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<Session::Reconnected>> Session::reconnect(const RelationshipId& id, End end, const Endpoint& to) {
  auto op = reconnect_operation(parsed_, id, end, to);
  if (!op) return op.error();
  if (!*op) return std::optional<Reconnected>{};
  const auto& reconnect = std::get<op::ReconnectRelationship>(**op);
  auto renamed = reconnect.renames.find(id);
  RelationshipId after = renamed == reconnect.renames.end() ? id : renamed->second;
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<Reconnected>{Reconnected{std::move(after), std::move(*update)}};
}

Result<std::optional<dsl::SourceUpdate>> Session::update_element(const Element& element) {
  const Element* existing = model_.elements.find(element.id);
  if (!existing) return Error{"Element \"" + element.id + "\" not found"};
  if (*existing == element) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(op::UpdateElement{element});
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

Result<std::optional<ElementRename>> Session::rename_element(const ElementId& id, std::string_view to) {
  auto renamed = editor::rename_element(source_, parsed_, id, to);
  if (!renamed) return renamed.error();
  if (!*renamed) return std::optional<ElementRename>{};
  // Last layouts follow the new ids, so the next layout starts from where everything was.
  const op::ChangeElementId& change = (*renamed)->operation;
  std::vector<std::pair<std::string, std::string>> nodes;
  for (auto& [from, now] : renamed_ids(parsed_.model, id, change.to)) nodes.emplace_back(from, std::move(now));
  for (const auto& [from, now] : change.groups) nodes.emplace_back(group_node_id(from), group_node_id(now));
  std::vector<std::pair<std::string, std::string>> edges(change.relationships.begin(), change.relationships.end());
  for (auto& [view, geometry] : last_) {
    // At once: groups may renumber in a chain (`z/G` becomes `z/G#2` as `a/G` becomes `z/G`).
    geometry.nodes.rename_all(nodes);
    geometry.rotations.rename_all(nodes);
    geometry.edges.rename_all(edges);
  }
  (*renamed)->update = adopt(change, std::move((*renamed)->update));
  return renamed;
}

Result<std::optional<dsl::SourceUpdate>> Session::update_relationship(const Relationship& relationship,
                                                                      const std::optional<std::string>& name,
                                                                      RelationshipId* renamed) {
  auto op = update_relationship_operation(model_, relationship, name, renamed);
  if (!op) return op.error();
  if (!*op) return std::optional<dsl::SourceUpdate>{};
  auto update = apply(**op);
  if (!update) return update.error();
  return std::optional<dsl::SourceUpdate>{std::move(*update)};
}

}  // namespace dd::editor
