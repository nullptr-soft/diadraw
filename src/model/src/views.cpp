#include "diadraw/model/views.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace dd {

namespace {

using IdSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

}  // namespace

Result<ResolvedView> resolve_view(const Model& model, std::string_view view_id) {
  const View* view = model.views.find(view_id);
  if (!view) return fail("View \"", view_id, "\" not found");

  // A scoped view shows the scope's direct children (as a C4 container view shows containers,
  // not their components); deeper relationships are lifted to them.
  std::vector<ElementId> in_scope;
  for (const auto& e : model.elements) {
    if (!view->scope || (e.parent && *e.parent == *view->scope)) in_scope.push_back(e.id);
  }

  auto expand = [&](const std::vector<Selector>& selectors) {
    IdSet ids;
    for (const auto& s : selectors) {
      if (s == "*")
        ids.insert(in_scope.begin(), in_scope.end());
      else if (model.elements.contains(s))
        ids.insert(s);
    }
    return ids;
  };

  IdSet included = expand(view->include);
  for (const auto& id : expand(view->exclude)) included.erase(id);

  ResolvedView out;
  out.view = *view;
  out.theme = view->theme ? view->theme : model.theme;
  out.styles = model.styles;
  out.themes = model.themes;
  for (const auto& e : model.elements)
    if (included.count(e.id)) out.elements.push_back(e);

  auto parent_of = [&](std::string_view id) -> const std::optional<ElementId>* {
    const Element* e = model.elements.find(id);
    return e ? &e->parent : nullptr;
  };
  auto visible_ancestor = [&](const ElementId& id) -> std::optional<ElementId> {
    std::optional<ElementId> cur = id;
    while (cur) {
      if (included.count(*cur)) return cur;
      const auto* p = parent_of(*cur);
      cur = p ? *p : std::nullopt;
    }
    return std::nullopt;
  };
  auto is_ancestor = [&](const ElementId& a, const ElementId& b) {
    const auto* p = parent_of(b);
    std::optional<ElementId> cur = p ? *p : std::nullopt;
    while (cur) {
      if (*cur == a) return true;
      p = parent_of(*cur);
      cur = p ? *p : std::nullopt;
    }
    return false;
  };

  // Direct relationships first, then implied ones; the first relationship for a pair wins.
  IdSet pairs;
  auto pair_key = [](const ElementId& s, const ElementId& t) { return s + '\0' + t; };
  for (const auto& r : model.relationships) {
    auto source = visible_ancestor(r.source.element);
    auto target = visible_ancestor(r.target.element);
    if (!source || !target || *source == *target) continue;
    if (*source == r.source.element && *target == r.target.element) {
      out.relationships.push_back(r);
      pairs.insert(pair_key(*source, *target));
    }
  }
  for (const auto& r : model.relationships) {
    auto source = visible_ancestor(r.source.element);
    auto target = visible_ancestor(r.target.element);
    if (!source || !target || *source == *target) continue;
    if (*source == r.source.element && *target == r.target.element) continue;
    // An edge between an element and its own ancestor says nothing at this level.
    if (is_ancestor(*source, *target) || is_ancestor(*target, *source)) continue;
    auto key = pair_key(*source, *target);
    if (pairs.count(key)) continue;
    pairs.insert(key);
    Relationship lifted = r;
    lifted.source = Endpoint{*source, std::nullopt};
    lifted.target = Endpoint{*target, std::nullopt};
    lifted.implied = true;
    out.relationships.push_back(std::move(lifted));
  }

  IdSet used;
  for (const auto& e : out.elements) {
    std::optional<GroupId> g = e.group;
    while (g && !used.count(*g)) {
      used.insert(*g);
      const Group* group = model.groups.find(*g);
      g = group ? group->parent : std::nullopt;
    }
  }
  auto depth = [&](const Group& g) {
    int d = 0;
    for (auto p = g.parent; p; ++d) {
      const Group* parent = model.groups.find(*p);
      p = parent ? parent->parent : std::nullopt;
    }
    return d;
  };
  for (const auto& g : model.groups)
    if (used.count(g.id)) out.groups.push_back(g);
  std::stable_sort(out.groups.begin(), out.groups.end(),
                   [&](const Group& a, const Group& b) { return depth(a) < depth(b); });
  return out;
}

Model with_default_view(const Model& model) { return with_default_view(Model(model)); }

Model with_default_view(Model&& model) {
  if (model.views.empty()) model.views.set(std::string(kDefaultViewId), make_view(std::string(kDefaultViewId)));
  return std::move(model);
}

}  // namespace dd
