// Renaming an element: the operation, where a rename can start in the text, and the text it
// writes. Apart from editor.cpp so a program that only renames (the language server) links the
// rest of the editor only where it uses it.

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"

namespace dd::editor {

Result<std::optional<Operation>> change_element_id_operation(const dsl::ParseResult& parsed, const ElementId& id,
                                                             std::string_view to, ElementId* renamed) {
  const Model& model = parsed.model;
  const Element* element = model.elements.find(id);
  if (!element) return fail("Element \"", id, "\" not found");
  if (renamed) *renamed = id;
  if (!dsl::is_identifier(to))
    return fail("\"", to, "\" is not a name: use letters, digits and _, not starting with a digit");
  auto dot = id.rfind('.');
  std::string head = dot == std::string::npos ? std::string() : id.substr(0, dot + 1);
  if (id.compare(head.size(), std::string::npos, to) == 0) return std::optional<Operation>{};
  op::ChangeElementId change{id, head + std::string(to), {}, {}, {}};

  // New ids of the element and its descendants.
  auto moved = renamed_ids(model, id, change.to);
  for (const auto& [from, now] : moved)
    if (model.elements.contains(now) && !moved.count(now)) return fail("There is already an element \"", now, "\"");
  auto moved_end = [&](const Endpoint& end) {
    auto it = moved.find(end.element);
    return it == moved.end() ? end : Endpoint{it->second, end.port};
  };

  // Ids derived from ends, again in declaration order, as reading the text back gives them.
  auto named = [&](const RelationshipId& rid) {
    auto source = parsed.source_map.relationships.find(rid);
    return source == parsed.source_map.relationships.end() || source->second.id.has_value();
  };
  std::set<std::string, std::less<>> taken;
  for (const auto& r : model.relationships) {
    if (named(r.id)) continue;
    Endpoint source = moved_end(r.source);
    Endpoint target = moved_end(r.target);
    int index = 1;
    std::string derived = dsl::relationship_id(source, target, index);
    while (taken.contains(derived)) derived = dsl::relationship_id(source, target, ++index);
    if (derived != r.id) change.relationships[r.id] = derived;
    taken.insert(std::move(derived));
  }

  auto decl = parsed.source_map.elements.find(id);
  // Without a label the element shows its id, so its name changes with it.
  if (decl != parsed.source_map.elements.end() && !decl->second.label) change.name = std::string(to);

  // Group ids, again in declaration order: a group outside any other is named from the path of
  // the element it is declared in (`shop/web/Edge`), a nested one from its parent's id, and a
  // repeated id gets `#2`, `#3`, … So groups inside the element follow its new path, and any
  // group may renumber.
  std::optional<dsl::Span> body;
  if (decl != parsed.source_map.elements.end()) body = decl->second.body;
  auto slashes = [](std::string s) {
    std::replace(s.begin(), s.end(), '.', '/');
    return s + "/";
  };
  const std::string from = slashes(id);
  const std::string now = slashes(change.to);
  std::map<GroupId, GroupId, std::less<>> groups;  // every group's id after the rename
  std::set<GroupId, std::less<>> group_ids;
  for (const auto& g : model.groups) {
    std::string base;
    if (g.parent) {
      auto parent = groups.find(*g.parent);
      base = (parent == groups.end() ? *g.parent : parent->second) + "/" + g.name;
    } else {
      // The id less its `#n`: it ends with the group's label.
      base = g.id;
      if (!(base.ends_with(g.name) && (base.size() == g.name.size() || base[base.size() - g.name.size() - 1] == '/'))) {
        auto hash = base.rfind('#');
        if (hash != std::string::npos) base.resize(hash);
      }
      auto source = parsed.source_map.groups.find(g.id);
      bool inside = body && source != parsed.source_map.groups.end() && source->second.decl.from >= body->from &&
                    source->second.decl.to <= body->to;
      if (inside && base.starts_with(from)) base = now + base.substr(from.size());
    }
    std::string gid = base;
    for (int n = 2; group_ids.contains(gid); ++n) gid = base + "#" + std::to_string(n);
    group_ids.insert(gid);
    if (gid != g.id) change.groups[g.id] = gid;
    groups.emplace(g.id, std::move(gid));
  }
  if (renamed) *renamed = change.to;
  return std::optional<Operation>{std::move(change)};
}

std::optional<RenameTarget> rename_target(const dsl::ParseResult& parsed, std::size_t offset) {
  auto at = [&](const dsl::Span& span) { return span.from <= offset && offset <= span.to; };
  const auto& map = parsed.source_map;
  for (const auto& [id, source] : map.elements)
    if (at(source.name)) return RenameTarget{id, source.name};
  for (const auto& ref : map.references) {
    auto parts = map.parts(ref);
    if (parts.empty() || !at({parts.front().from, parts.back().to}) || ref.count > ref.path_length()) continue;
    for (std::size_t j = 0; j < parts.size(); ++j) {
      if (!at(parts[j])) continue;
      // The element whose name is path part `index`: the target's id up to that part.
      std::size_t index = ref.path_index(j);
      const ElementId& element = ref.target.element;
      std::size_t end = 0;
      for (std::size_t k = 0; k <= index; ++k) {
        if (k > 0) ++end;  // past the dot
        if (end > element.size()) return std::nullopt;  // past the element: a port
        end = std::min(element.find('.', end), element.size());
      }
      return RenameTarget{element.substr(0, end), parts[j]};
    }
  }
  return std::nullopt;
}

Result<std::optional<ElementRename>> rename_element(std::string_view source, const dsl::ParseResult& parsed,
                                                    const ElementId& id, std::string_view to) {
  ElementId after;
  auto op = change_element_id_operation(parsed, id, to, &after);
  if (!op) return op.error();
  if (!*op) return std::optional<ElementRename>{};
  auto update = dsl::update_source(source, **op, &parsed);
  if (!update) return update.error();
  // update_source checks a patch reads back as the model (errors and print) and otherwise falls
  // back to a reprint, which would lose the text's comments and layout: that only happens when the
  // new name changes how the text reads (a keyword there, or a reference elsewhere that would now
  // resolve to the renamed element). Printing writes no group ids, so those are compared here.
  if (!update->minimal || update->result.model.groups.keys() != update->model.groups.keys())
    return fail("\"", to, "\" cannot be used as a name here: the text would read differently");
  ElementRename out{std::move(after), std::get<op::ChangeElementId>(std::move(**op)), std::move(*update)};
  return std::optional<ElementRename>{std::move(out)};
}

}  // namespace dd::editor
