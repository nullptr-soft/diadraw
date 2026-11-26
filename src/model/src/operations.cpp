#include "diadraw/model/operations.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

namespace dd {

namespace {

using IdSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

template <class... F>
struct overloaded : F... {
  using F::operator()...;
};
template <class... F>
overloaded(F...) -> overloaded<F...>;

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

/// `[A-Za-z_][A-Za-z0-9_]*`: one name, as a view id is written after `view`.
bool identifier(std::string_view s) {
  if (s.empty()) return false;
  auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
  if (!letter(s[0])) return false;
  return std::all_of(s.begin(), s.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

/// Keys are written as `key value`, so they must be names.
std::optional<Error> check_keys(const Style& style, std::string_view what) {
  for (const auto& [k, v] : style)
    if (!identifier(k)) return Error{"Key " + quoted(k) + " of " + std::string(what) + " is not a name"};
  return std::nullopt;
}

bool has(const IdSet& set, std::string_view id) { return set.find(id) != set.end(); }

bool any_in(const std::vector<ElementId>& ids, const IdSet& set) {
  return std::any_of(ids.begin(), ids.end(), [&](const auto& id) { return has(set, id); });
}

bool mentions(const LayoutHint& hint, const IdSet& ids, const IdSet& relationships) {
  return std::visit(overloaded{
                        [&](const RouteHint& h) { return has(relationships, h.relationship); },
                        [&](const PinHint& h) { return has(ids, h.element) || has(ids, h.reference); },
                        [&](const PinAtHint& h) { return has(ids, h.element); },
                        [&](const AlignHint& h) { return any_in(h.elements, ids); },
                        [&](const OrderHint& h) { return any_in(h.elements, ids); },
                        [&](const SizeHint& h) { return has(ids, h.element); },
                        [&](const RotateHint& h) { return has(ids, h.element); },
                    },
                    hint);
}

bool mentions(const Decoration& d, const IdSet& ids) {
  return (d.reference && has(ids, *d.reference)) || any_in(d.around, ids);
}

/// Sequence steps without those that involve any of `ids`; nullopt when none do.
std::optional<std::vector<SequenceItem>> strip_sequence(const std::vector<SequenceItem>& items,
                                                        const IdSet& ids) {
  bool changed = false;
  std::vector<SequenceItem> out;
  out.reserve(items.size());
  for (const auto& item : items) {
    bool involved = std::visit(
        overloaded{
            [&](const SequenceMessage& m) { return has(ids, m.from) || has(ids, m.to); },
            [&](const SequenceActivation& a) { return has(ids, a.element); },
            [&](const SequenceNote& n) { return any_in(n.over, ids); },
            [&](const SequenceRef& r) { return any_in(r.over, ids); },
            [&](const auto&) { return false; },
        },
        static_cast<const SequenceItem::variant&>(item));
    if (involved) {
      changed = true;
      continue;
    }
    if (const auto* frame = std::get_if<SequenceFrame>(&item)) {
      SequenceFrame copy = *frame;
      for (auto& section : copy.sections) {
        if (auto stripped = strip_sequence(section.items, ids)) {
          section.items = std::move(*stripped);
          changed = true;
        }
      }
      out.emplace_back(std::move(copy));
    } else {
      out.push_back(item);
    }
  }
  if (!changed) return std::nullopt;
  return out;
}

/// The view without selectors and hints that name any of `ids`; nullopt when nothing changes.
std::optional<View> strip_references(const View& view, const IdSet& ids,
                                     const IdSet& relationships) {
  View out = view;
  auto drop = [&](std::vector<Selector>& v) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& s) { return has(ids, s); }),
            v.end());
  };
  drop(out.include);
  drop(out.exclude);
  out.hints.erase(std::remove_if(out.hints.begin(), out.hints.end(),
                                 [&](const auto& h) { return mentions(h, ids, relationships); }),
                  out.hints.end());
  out.decorations.erase(std::remove_if(out.decorations.begin(), out.decorations.end(),
                                       [&](const auto& d) { return mentions(d, ids); }),
                        out.decorations.end());
  std::optional<std::vector<SequenceItem>> sequence;
  if (view.sequence) sequence = strip_sequence(*view.sequence, ids);
  if (!sequence && out.include.size() == view.include.size() &&
      out.exclude.size() == view.exclude.size() && out.hints.size() == view.hints.size() &&
      out.decorations.size() == view.decorations.size()) {
    return std::nullopt;
  }
  if (sequence) out.sequence = std::move(*sequence);
  return out;
}

/// Applies `element` (old id → new) to every element a sequence's steps name, frames included.
template <class Rename>
void rename_in_sequence(std::vector<SequenceItem>& items, const Rename& element) {
  for (auto& item : items) {
    std::visit(overloaded{
                   [&](SequenceMessage& m) {
                     m.from = element(m.from);
                     m.to = element(m.to);
                   },
                   [&](SequenceActivation& a) { a.element = element(a.element); },
                   [&](SequenceNote& n) {
                     for (auto& id : n.over) id = element(id);
                   },
                   [&](SequenceRef& r) {
                     for (auto& id : r.over) id = element(id);
                   },
                   [&](SequenceFrame& f) {
                     for (auto& section : f.sections) rename_in_sequence(section.items, element);
                   },
                   [](auto&) {},
               },
               static_cast<SequenceItem::variant&>(item));
  }
}

Operation one_or_batch(std::vector<Operation> ops) {
  if (ops.size() == 1) return std::move(ops.front());
  return op::Batch{std::move(ops)};
}

const View* find_view(const Model& m, std::string_view id) { return m.views.find(id); }

class Applier {
 public:
  explicit Applier(Model& m) : m_(m) {}

  Result<Operation> operator()(const op::AddElement& o) {
    const auto& e = o.element;
    if (m_.elements.contains(e.id)) return fail("Element ", quoted(e.id), " already exists");
    if (e.parent && !m_.elements.contains(*e.parent))
      return fail("Parent ", quoted(*e.parent), " of ", quoted(e.id), " not found");
    if (e.group && !m_.groups.contains(*e.group))
      return fail("Group ", quoted(*e.group), " of ", quoted(e.id), " not found");
    m_.elements.set(e.id, e);
    return Operation{op::RemoveElement{e.id}};
  }

  Result<Operation> operator()(const op::RemoveElement& o) {
    if (!m_.elements.contains(o.id)) return fail("Element ", quoted(o.id), " not found");
    // Removing an element removes its descendants and every relationship touching them.
    auto ids = descendants_of(m_, o.id);
    IdSet id_set(ids.begin(), ids.end());
    std::vector<OrderedMap<Relationship>::Ptr> touching;
    for (const auto& r : m_.relationships) {
      if (has(id_set, r.source.element) || has(id_set, r.target.element))
        touching.push_back(m_.relationships.get(r.id));
    }
    IdSet rel_ids;
    for (const auto& r : touching) rel_ids.insert(r->id);

    // Restore parents before children, then relationships once their endpoints exist, then views.
    std::vector<Operation> restore;
    for (const auto& id : ids) restore.emplace_back(op::AddElement{*m_.elements.find(id)});
    for (const auto& r : touching) restore.emplace_back(op::AddRelationship{*r});

    // Views drop every reference to the removed elements; a view scoped to one goes away.
    std::vector<std::pair<std::string, std::optional<View>>> view_changes;
    for (const auto& view : m_.views) {
      if (view.scope && has(id_set, *view.scope)) {
        restore.emplace_back(op::AddView{view});
        view_changes.emplace_back(view.id, std::nullopt);
        continue;
      }
      if (auto stripped = strip_references(view, id_set, rel_ids)) {
        restore.emplace_back(op::ReplaceView{view});
        view_changes.emplace_back(view.id, std::move(stripped));
      }
    }

    for (const auto& id : ids) m_.elements.erase(id);
    for (const auto& r : touching) m_.relationships.erase(r->id);
    for (auto& [id, change] : view_changes) {
      if (change)
        m_.views.set(id, std::move(*change));
      else
        m_.views.erase(id);
    }
    return one_or_batch(std::move(restore));
  }

  Result<Operation> operator()(const op::RenameElement& o) {
    const Element* existing = m_.elements.find(o.id);
    if (!existing) return fail("Element ", quoted(o.id), " not found");
    Element updated = *existing;
    Operation inverse = op::RenameElement{o.id, existing->name};
    updated.name = o.name;
    m_.elements.set(o.id, std::move(updated));
    return inverse;
  }

  Result<Operation> operator()(const op::ChangeElementId& o) {
    if (!m_.elements.contains(o.id)) return fail("Element ", quoted(o.id), " not found");
    // Only the last part changes: the element stays where it is in the hierarchy.
    auto dot = o.id.rfind('.');
    std::string_view head = dot == std::string::npos ? std::string_view{} : std::string_view(o.id).substr(0, dot + 1);
    std::string_view to(o.to);
    if (to.substr(0, head.size()) != head || to.find('.', head.size()) != std::string_view::npos)
      return fail(quoted(o.to), " must keep the place of ", quoted(o.id), " in the model");
    if (!identifier(to.substr(head.size()))) return fail(quoted(to.substr(head.size())), " is not an element name");

    // The element and its descendants, each with its new id.
    auto moved = renamed_ids(m_, o.id, o.to);
    for (const auto& [from, now] : moved)
      if (now != from && m_.elements.contains(now) && !moved.count(now))
        return fail("Element ", quoted(now), " already exists");
    for (const auto& [from, now] : o.relationships) {
      if (!m_.relationships.contains(from)) return fail("Relationship ", quoted(from), " not found");
      if (now.empty()) return fail("Relationship ", quoted(from), " cannot get an empty id");
    }
    for (const auto& [from, now] : o.groups) {
      if (!m_.groups.contains(from)) return fail("Group ", quoted(from), " not found");
      if (now.empty()) return fail("Group ", quoted(from), " cannot get an empty id");
    }

    auto element = [&](const ElementId& id) -> const ElementId& {
      auto it = moved.find(id);
      return it == moved.end() ? id : it->second;
    };
    auto element_opt = [&](std::optional<ElementId>& id) {
      if (id) *id = element(*id);
    };
    auto elements = [&](std::vector<ElementId>& ids) {
      for (auto& id : ids) id = element(id);
    };
    auto renamed = [](const std::map<std::string, std::string>& renames, const std::string& id) -> const std::string& {
      auto it = renames.find(id);
      return it == renames.end() ? id : it->second;
    };

    // Everything is rebuilt in the same order: derived ids count on the declaration order.
    OrderedMap<Group> groups;
    for (const auto& g : m_.groups) {
      const GroupId& id = renamed(o.groups, g.id);
      if (groups.contains(id)) return fail("Group ", quoted(id), " already exists");
      if (id == g.id && !(g.parent && o.groups.count(*g.parent))) {
        groups.set(id, m_.groups.get(g.id));
        continue;
      }
      Group copy = g;
      copy.id = id;
      if (copy.parent) copy.parent = renamed(o.groups, *copy.parent);
      groups.set(id, std::move(copy));
    }
    OrderedMap<Relationship> relationships;
    for (const auto& r : m_.relationships) {
      const RelationshipId& id = renamed(o.relationships, r.id);
      if (relationships.contains(id)) return fail("Relationship ", quoted(id), " already exists");
      if (id == r.id && !moved.count(r.source.element) && !moved.count(r.target.element)) {
        relationships.set(id, m_.relationships.get(r.id));
        continue;
      }
      Relationship copy = r;
      copy.id = id;
      copy.source.element = element(r.source.element);
      copy.target.element = element(r.target.element);
      relationships.set(id, std::move(copy));
    }
    OrderedMap<Element> elements_after;
    for (const auto& e : m_.elements) {
      bool regrouped = e.group && o.groups.count(*e.group);
      if (!moved.count(e.id) && !regrouped) {
        elements_after.set(e.id, m_.elements.get(e.id));
        continue;
      }
      Element copy = e;
      copy.id = element(e.id);
      if (e.id == o.id && o.name) copy.name = *o.name;
      element_opt(copy.parent);
      if (copy.group) copy.group = renamed(o.groups, *copy.group);
      std::string id = copy.id;
      elements_after.set(std::move(id), std::move(copy));
    }
    OrderedMap<View> views;
    for (const auto& v : m_.views) {
      View copy = v;
      element_opt(copy.scope);
      for (auto* selectors : {&copy.include, &copy.exclude})
        for (auto& s : *selectors)
          if (s != "*") s = element(s);
      for (auto& hint : copy.hints) {
        std::visit(overloaded{
                       [&](RouteHint& h) { h.relationship = renamed(o.relationships, h.relationship); },
                       [&](PinHint& h) {
                         h.element = element(h.element);
                         h.reference = element(h.reference);
                       },
                       [&](AlignHint& h) { elements(h.elements); },
                       [&](OrderHint& h) { elements(h.elements); },
                       [&](auto& h) { h.element = element(h.element); },
                   },
                   hint);
      }
      if (copy.sequence) rename_in_sequence(*copy.sequence, element);
      for (auto& d : copy.decorations) {
        element_opt(d.reference);
        elements(d.around);
      }
      if (copy == v)
        views.set(v.id, m_.views.get(v.id));
      else
        views.set(v.id, std::move(copy));
    }

    op::ChangeElementId inverse{o.to, o.id, {}, {}, {}};
    if (o.name) inverse.name = m_.elements.find(o.id)->name;
    m_.elements = std::move(elements_after);
    m_.relationships = std::move(relationships);
    m_.groups = std::move(groups);
    m_.views = std::move(views);
    for (const auto& [from, now] : o.relationships) inverse.relationships[now] = from;
    for (const auto& [from, now] : o.groups) inverse.groups[now] = from;
    return Operation{std::move(inverse)};
  }

  Result<Operation> operator()(const op::UpdateElement& o) {
    const Element& e = o.element;
    const Element* existing = m_.elements.find(e.id);
    if (!existing) return fail("Element ", quoted(e.id), " not found");
    if (e.parent != existing->parent || e.group != existing->group || e.ports != existing->ports)
      return fail("Updating ", quoted(e.id), " cannot move it or change its ports");
    if (!is_qualified_name(e.kind)) return fail("Kind ", quoted(e.kind), " of ", quoted(e.id), " is not a name");
    if (e.shape && !is_qualified_name(*e.shape))
      return fail("Shape ", quoted(*e.shape), " of ", quoted(e.id), " is not a name");
    for (const auto& tag : e.tags)
      if (tag.empty()) return fail("Element ", quoted(e.id), " has an empty tag");
    if (e.style)
      if (auto why = inline_style_problem(*e.style)) return fail("Element ", quoted(e.id), ": ", *why);
    Operation inverse = op::UpdateElement{*existing};
    m_.elements.set(e.id, e);
    return inverse;
  }

  Result<Operation> operator()(const op::AddRelationship& o) {
    const auto& r = o.relationship;
    if (m_.relationships.contains(r.id))
      return fail("Relationship ", quoted(r.id), " already exists");
    if (auto bad = check_endpoints(r.id, r.source, r.target)) return *bad;
    m_.relationships.set(r.id, r);
    return Operation{op::RemoveRelationship{r.id}};
  }

  Result<Operation> operator()(const op::ReconnectRelationship& o) {
    const Relationship* existing = m_.relationships.find(o.id);
    if (!existing) return fail("Relationship ", quoted(o.id), " not found");
    if (auto bad = check_endpoints(o.id, o.source, o.target)) return *bad;
    auto renamed = [&](const RelationshipId& id) {
      auto it = o.renames.find(id);
      return it == o.renames.end() ? id : it->second;
    };
    for (const auto& [from, to] : o.renames) {
      if (!m_.relationships.contains(from)) return fail("Relationship ", quoted(from), " not found");
      if (to.empty()) return fail("Relationship ", quoted(from), " cannot get an empty id");
    }
    // The relationships in the same order, under their new ids.
    OrderedMap<Relationship> relationships;
    for (const auto& r : m_.relationships) {
      RelationshipId id = renamed(r.id);
      if (relationships.contains(id)) return fail("Relationship ", quoted(id), " already exists");
      if (r.id == o.id) {
        Relationship moved = r;
        moved.id = id;
        moved.source = o.source;
        moved.target = o.target;
        relationships.set(id, std::move(moved));
      } else if (id != r.id) {
        Relationship copy = r;
        copy.id = id;
        relationships.set(id, std::move(copy));
      } else {
        relationships.set(id, m_.relationships.get(r.id));
      }
    }
    op::ReconnectRelationship inverse{renamed(o.id), existing->source, existing->target, {}};
    for (const auto& [from, to] : o.renames)
      if (from != to) inverse.renames[to] = from;
    // Routes follow their relationships.
    std::vector<View> rerouted;
    if (!o.renames.empty()) {
      for (const auto& view : m_.views) {
        View copy = view;
        bool changed = false;
        for (auto& hint : copy.hints)
          if (auto* route = std::get_if<RouteHint>(&hint)) {
            RelationshipId id = renamed(route->relationship);
            if (id != route->relationship) {
              route->relationship = std::move(id);
              changed = true;
            }
          }
        if (changed) rerouted.push_back(std::move(copy));
      }
    }
    m_.relationships = std::move(relationships);
    for (auto& v : rerouted) {
      std::string id = v.id;
      m_.views.set(std::move(id), std::move(v));
    }
    return Operation{std::move(inverse)};
  }

  Result<Operation> operator()(const op::RemoveRelationship& o) {
    auto existing = m_.relationships.get(o.id);
    if (!existing) return fail("Relationship ", quoted(o.id), " not found");
    // Routes for the relationship go with it.
    std::vector<Operation> restore{op::AddRelationship{*existing}};
    IdSet none;
    IdSet rel{o.id};
    std::vector<View> stripped_views;
    for (const auto& view : m_.views) {
      if (auto stripped = strip_references(view, none, rel)) {
        restore.emplace_back(op::ReplaceView{view});
        stripped_views.push_back(std::move(*stripped));
      }
    }
    m_.relationships.erase(o.id);
    for (auto& v : stripped_views) {
      std::string id = v.id;  // before the move: argument order is unspecified
      m_.views.set(std::move(id), std::move(v));
    }
    return one_or_batch(std::move(restore));
  }

  Result<Operation> operator()(const op::SetRelationshipLabel& o) {
    const Relationship* existing = m_.relationships.find(o.id);
    if (!existing) return fail("Relationship ", quoted(o.id), " not found");
    Operation inverse = op::SetRelationshipLabel{o.id, existing->label};
    Relationship updated = *existing;
    updated.label = o.label;
    m_.relationships.set(o.id, std::move(updated));
    return inverse;
  }

  Result<Operation> operator()(const op::UpdateRelationship& o) {
    const Relationship& r = o.relationship;
    const Relationship* existing = m_.relationships.find(r.id);
    if (!existing) return fail("Relationship ", quoted(r.id), " not found");
    if (r.source != existing->source || r.target != existing->target)
      return fail("Updating ", quoted(r.id), " cannot reconnect it");
    if (r.implied) return fail("Relationship ", quoted(r.id), " cannot be implied in a model");
    for (const auto& tag : r.tags)
      if (tag.empty()) return fail("Relationship ", quoted(r.id), " has an empty tag");
    if (r.style)
      if (auto why = inline_style_problem(*r.style)) return fail("Relationship ", quoted(r.id), ": ", *why);
    Operation inverse = op::UpdateRelationship{*existing};
    m_.relationships.set(r.id, r);
    return inverse;
  }

  Result<Operation> operator()(const op::RenameRelationships& o) {
    std::map<std::string, std::string, std::less<>> renamed;
    for (const auto& r : o.renames) {
      if (!m_.relationships.contains(r.from)) return fail("Relationship ", quoted(r.from), " not found");
      if (r.to.empty()) return fail("Relationship ", quoted(r.from), " needs an id");
      if (!renamed.emplace(r.from, r.to).second) return fail("Relationship ", quoted(r.from), " is renamed twice");
    }
    std::set<std::string, std::less<>> taken;
    for (const auto& rel : m_.relationships) {
      auto it = renamed.find(rel.id);
      const std::string& id = it == renamed.end() ? rel.id : it->second;
      if (!taken.insert(id).second) return fail("Relationship ", quoted(id), " already exists");
    }
    // Rebuilt in the same order: the order is the declaration order, which derived ids count on.
    OrderedMap<Relationship> relationships;
    for (const auto& rel : m_.relationships) {
      auto it = renamed.find(rel.id);
      if (it == renamed.end()) {
        relationships.set(rel.id, rel);
        continue;
      }
      Relationship moved = rel;
      moved.id = it->second;
      relationships.set(it->second, std::move(moved));
    }
    m_.relationships = std::move(relationships);
    for (const auto& view : m_.views) {
      bool changed = false;
      View updated = view;
      for (auto& hint : updated.hints) {
        auto* route = std::get_if<RouteHint>(&hint);
        if (!route) continue;
        if (auto it = renamed.find(route->relationship); it != renamed.end()) {
          route->relationship = it->second;
          changed = true;
        }
      }
      if (changed) {
        std::string id = updated.id;
        m_.views.set(std::move(id), std::move(updated));
      }
    }
    op::RenameRelationships inverse;
    for (const auto& r : o.renames) inverse.renames.push_back({r.to, r.from});
    return Operation{std::move(inverse)};
  }

  Result<Operation> operator()(const op::AddGroup& o) {
    const auto& g = o.group;
    if (m_.groups.contains(g.id)) return fail("Group ", quoted(g.id), " already exists");
    if (g.parent && !m_.groups.contains(*g.parent))
      return fail("Parent group ", quoted(*g.parent), " not found");
    m_.groups.set(g.id, g);
    return Operation{op::RemoveGroup{g.id}};
  }

  Result<Operation> operator()(const op::RemoveGroup& o) {
    const Group* existing = m_.groups.find(o.id);
    if (!existing) return fail("Group ", quoted(o.id), " not found");
    bool used =
        std::any_of(m_.elements.begin(), m_.elements.end(),
                    [&](const Element& e) { return e.group && *e.group == o.id; }) ||
        std::any_of(m_.groups.begin(), m_.groups.end(),
                    [&](const Group& g) { return g.parent && *g.parent == o.id; });
    if (used) return fail("Group ", quoted(o.id), " is not empty");
    Operation inverse = op::AddGroup{*existing};
    m_.groups.erase(o.id);
    return inverse;
  }

  Result<Operation> operator()(const op::AddView& o) {
    const auto& v = o.view;
    if (m_.views.contains(v.id)) return fail("View ", quoted(v.id), " already exists");
    if (v.scope && !m_.elements.contains(*v.scope))
      return fail("Scope ", quoted(*v.scope), " of view ", quoted(v.id), " not found");
    m_.views.set(v.id, v);
    return Operation{op::RemoveView{v.id}};
  }

  Result<Operation> operator()(const op::RemoveView& o) {
    const View* existing = find_view(m_, o.id);
    if (!existing) return fail("View ", quoted(o.id), " not found");
    Operation inverse = op::AddView{*existing};
    m_.views.erase(o.id);
    return inverse;
  }

  Result<Operation> operator()(const op::RenameView& o) {
    if (!m_.views.contains(o.id)) return fail("View ", quoted(o.id), " not found");
    // The id is written bare after `view`, so it must read back as one name.
    if (!identifier(o.to)) return fail(quoted(o.to), " is not a view name");
    if (o.to != o.id && m_.views.contains(o.to)) return fail("View ", quoted(o.to), " already exists");
    View renamed = *m_.views.find(o.id);
    renamed.id = o.to;
    m_.views.rename(o.id, o.to);
    m_.views.set(o.to, std::move(renamed));  // replaces the value where the key now stands
    return Operation{op::RenameView{o.to, o.id}};
  }

  Result<Operation> operator()(const op::ReplaceView& o) {
    const View* existing = find_view(m_, o.view.id);
    if (!existing) return fail("View ", quoted(o.view.id), " not found");
    if (o.view.scope && !m_.elements.contains(*o.view.scope))
      return fail("Scope ", quoted(*o.view.scope), " of view ", quoted(o.view.id), " not found");
    Operation inverse = op::ReplaceView{*existing};
    m_.views.set(o.view.id, o.view);
    return inverse;
  }

  Result<Operation> operator()(const op::SetLayout& o) {
    const View* view = find_view(m_, o.view);
    if (!view) return fail("View ", quoted(o.view), " not found");
    Operation inverse = op::SetLayout{o.view, view->layout};
    View updated = *view;
    updated.layout = o.layout;
    m_.views.set(o.view, std::move(updated));
    return inverse;
  }

  Result<Operation> operator()(const op::AddHint& o) {
    const View* view = find_view(m_, o.view);
    if (!view) return fail("View ", quoted(o.view), " not found");
    std::size_t index = o.index.value_or(view->hints.size());
    if (index > view->hints.size())
      return fail("Hint index ", std::to_string(index), " out of range");
    View updated = *view;
    updated.hints.insert(updated.hints.begin() + static_cast<std::ptrdiff_t>(index), o.hint);
    m_.views.set(o.view, std::move(updated));
    return Operation{op::RemoveHint{o.view, index}};
  }

  Result<Operation> operator()(const op::RemoveHint& o) {
    const View* view = find_view(m_, o.view);
    if (!view) return fail("View ", quoted(o.view), " not found");
    if (o.index >= view->hints.size())
      return fail("Hint index ", std::to_string(o.index), " out of range");
    View updated = *view;
    Operation inverse = op::AddHint{o.view, updated.hints[o.index], o.index};
    updated.hints.erase(updated.hints.begin() + static_cast<std::ptrdiff_t>(o.index));
    m_.views.set(o.view, std::move(updated));
    return inverse;
  }

  Result<Operation> operator()(const op::ReplaceHint& o) {
    const View* view = find_view(m_, o.view);
    if (!view) return fail("View ", quoted(o.view), " not found");
    if (o.index >= view->hints.size())
      return fail("Hint index ", std::to_string(o.index), " out of range");
    View updated = *view;
    Operation inverse = op::ReplaceHint{o.view, o.index, updated.hints[o.index]};
    updated.hints[o.index] = o.hint;
    m_.views.set(o.view, std::move(updated));
    return inverse;
  }

  Result<Operation> operator()(const op::UseLibrary& o) {
    auto uses = m_.libraries.value_or(std::vector<LibraryUse>{});
    if (std::any_of(uses.begin(), uses.end(),
                    [&](const LibraryUse& u) { return u.alias == o.use.alias; }))
      return fail("Library alias ", quoted(o.use.alias), " is already in use");
    std::size_t at = std::min(o.index.value_or(uses.size()), uses.size());
    uses.insert(uses.begin() + static_cast<std::ptrdiff_t>(at), o.use);
    m_.libraries = std::move(uses);
    return Operation{op::UnuseLibrary{o.use.alias}};
  }

  Result<Operation> operator()(const op::UnuseLibrary& o) {
    auto uses = m_.libraries.value_or(std::vector<LibraryUse>{});
    auto it = std::find_if(uses.begin(), uses.end(),
                           [&](const LibraryUse& u) { return u.alias == o.alias; });
    if (it == uses.end()) return fail("Library alias ", quoted(o.alias), " not found");
    auto index = static_cast<std::size_t>(it - uses.begin());
    Operation inverse = op::UseLibrary{*it, index};
    uses.erase(it);
    if (uses.empty())
      m_.libraries.reset();
    else
      m_.libraries = std::move(uses);
    return inverse;
  }

  Result<Operation> operator()(const op::AddStyleRule& o) {
    if (auto err = check(o.rule)) return *err;
    std::size_t at = std::min(o.index.value_or(m_.styles.size()), m_.styles.size());
    m_.styles.insert(m_.styles.begin() + static_cast<std::ptrdiff_t>(at), o.rule);
    return Operation{op::RemoveStyleRule{at}};
  }

  Result<Operation> operator()(const op::RemoveStyleRule& o) {
    if (o.index >= m_.styles.size()) return fail("Style block ", std::to_string(o.index), " not found");
    Operation inverse = op::AddStyleRule{m_.styles[o.index], o.index};
    m_.styles.erase(m_.styles.begin() + static_cast<std::ptrdiff_t>(o.index));
    return inverse;
  }

  Result<Operation> operator()(const op::UpdateStyleRule& o) {
    if (o.index >= m_.styles.size()) return fail("Style block ", std::to_string(o.index), " not found");
    if (auto err = check(o.rule)) return *err;
    Operation inverse = op::UpdateStyleRule{o.index, m_.styles[o.index]};
    m_.styles[o.index] = o.rule;
    return inverse;
  }

  Result<Operation> operator()(const op::SetTheme& o) {
    if (o.theme && !is_qualified_name(*o.theme)) return fail("Theme ", quoted(*o.theme), " is not a name");
    if (o.view) {
      const View* view = m_.views.find(*o.view);
      if (!view) return fail("View ", quoted(*o.view), " not found");
      Operation inverse = op::SetTheme{o.view, view->theme};
      View updated = *view;
      updated.theme = o.theme;
      m_.views.set(*o.view, std::move(updated));
      return inverse;
    }
    Operation inverse = op::SetTheme{std::nullopt, m_.theme};
    m_.theme = o.theme;
    return inverse;
  }

  Result<Operation> operator()(const op::AddTheme& o) {
    if (auto err = check(o.theme)) return *err;
    if (find_theme(o.theme.name)) return fail("Theme ", quoted(o.theme.name), " is already declared");
    std::size_t at = std::min(o.index.value_or(m_.themes.size()), m_.themes.size());
    m_.themes.insert(m_.themes.begin() + static_cast<std::ptrdiff_t>(at), o.theme);
    return Operation{op::RemoveTheme{o.theme.name}};
  }

  Result<Operation> operator()(const op::RemoveTheme& o) {
    auto it = find_theme(o.name);
    if (!it) return fail("Theme ", quoted(o.name), " not found");
    auto index = static_cast<std::size_t>(it - m_.themes.data());
    Operation inverse = op::AddTheme{*it, index};
    m_.themes.erase(m_.themes.begin() + static_cast<std::ptrdiff_t>(index));
    return inverse;
  }

  Result<Operation> operator()(const op::UpdateTheme& o) {
    ThemeDef* it = find_theme(o.theme.name);
    if (!it) return fail("Theme ", quoted(o.theme.name), " not found");
    if (auto err = check(o.theme)) return *err;
    Operation inverse = op::UpdateTheme{*it};
    *it = o.theme;
    return inverse;
  }

  Result<Operation> operator()(const op::Batch& o) {
    // All or nothing: a failing step restores the snapshot taken before the first one. (Replaying
    // inverses would also work, but re-added items would move to the end of their collections.)
    Model before = m_;  // cheap: collections share their values
    std::vector<Operation> inverses;
    inverses.reserve(o.operations.size());
    for (const auto& step : o.operations) {
      auto result = std::visit(*this, static_cast<const Operation::variant&>(step));
      if (!result) {
        m_ = std::move(before);
        return result.error();
      }
      inverses.push_back(std::move(*result));
    }
    std::reverse(inverses.begin(), inverses.end());
    return Operation{op::Batch{std::move(inverses)}};
  }

 private:
  ThemeDef* find_theme(std::string_view name) {
    auto it = std::find_if(m_.themes.begin(), m_.themes.end(), [&](const ThemeDef& t) { return t.name == name; });
    return it == m_.themes.end() ? nullptr : &*it;
  }

  static std::optional<Error> check(const StyleRule& rule) { return style_rule_problem(rule); }

  static std::optional<Error> check(const ThemeDef& theme) {
    std::string what = "theme " + quoted(theme.name);
    if (!identifier(theme.name))
      return Error{"Theme name " + quoted(theme.name) + " is not a name"};
    if (theme.base && !is_qualified_name(*theme.base))
      return Error{"Theme " + quoted(*theme.base) + " that " + what + " extends is not a name"};
    if (theme.base && *theme.base == theme.name) return Error{"Theme " + quoted(theme.name) + " extends itself"};
    if (auto err = check_keys(theme.values, what)) return err;
    for (const auto& [kind, look] : theme.nodes) {
      if (!identifier(kind))
        return Error{"Node " + quoted(kind) + " of " + what + " is not a name"};
      if (auto err = check_keys(look, what)) return err;
    }
    return std::nullopt;
  }

  /// An error when an end names a missing element or port.
  std::optional<Error> check_endpoints(const RelationshipId& id, const Endpoint& source, const Endpoint& target) {
    for (const Endpoint* end : {&source, &target}) {
      const Element* el = m_.elements.find(end->element);
      if (!el) return fail("Endpoint ", quoted(end->element), " of ", quoted(id), " not found");
      if (end->port &&
          std::none_of(el->ports.begin(), el->ports.end(),
                       [&](const Port& p) { return p.id == *end->port; }))
        return fail("Port ", quoted(end->element + "." + *end->port), " not found");
    }
    return std::nullopt;
  }

  Model& m_;
};

}  // namespace

Result<Operation> apply_in_place(Model& model, const Operation& operation) {
  Applier applier(model);
  return std::visit(applier, static_cast<const Operation::variant&>(operation));
}

bool is_qualified_name(std::string_view s) {
  auto dot = s.find('.');
  if (dot == std::string_view::npos) return identifier(s);
  return identifier(s.substr(0, dot)) && identifier(s.substr(dot + 1));
}

std::optional<std::string> inline_style_problem(const Style& style) {
  for (const auto& [k, v] : style) {
    if (k.empty()) return std::string("A style key is empty");
    if (k.find_first_of(";=") != std::string::npos) return "Style key " + quoted(k) + " cannot hold \"=\" or \";\"";
    if (v.find(';') != std::string::npos) return "The value of style key " + quoted(k) + " cannot hold \";\"";
  }
  return std::nullopt;
}

std::optional<Error> style_rule_problem(const StyleRule& rule) {
  if (rule.target == StyleTarget::Relationships && rule.kind)
    return Error{"A relationship style block cannot name a kind"};
  if (rule.kind && (!identifier(*rule.kind) || *rule.kind == "relationship" || *rule.kind == "tag"))
    return Error{"Kind " + quoted(*rule.kind) + " of a style block is not a name"};
  if (rule.tag && rule.tag->empty()) return Error{"A style block has an empty tag"};
  return check_keys(rule.style, "a style block");
}

Result<ApplyResult> apply_operation(const Model& model, const Operation& operation) {
  Model copy = model;  // cheap: collections share their values
  auto inverse = apply_in_place(copy, operation);
  if (!inverse) return inverse.error();
  return ApplyResult{std::move(copy), std::move(*inverse)};
}

}  // namespace dd
