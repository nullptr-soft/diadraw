#include "diadraw/model/model.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace dd {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

}  // namespace

LibrarySpec parse_library_spec(std::string_view spec) {
  auto at = spec.rfind('@');
  if (at == std::string_view::npos || at == 0) return {std::string(trim(spec)), "*"};
  auto range = trim(spec.substr(at + 1));
  return {std::string(trim(spec.substr(0, at))), range.empty() ? "*" : std::string(range)};
}

std::string default_library_alias(std::string_view name) {
  auto slash = name.rfind('/');
  std::string_view base = slash == std::string_view::npos ? name : name.substr(slash + 1);
  // Runs of other characters become one underscore, as /[^A-Za-z0-9_]+/g does.
  std::string id;
  bool in_run = false;
  for (char c : base) {
    if (is_ident_char(c)) {
      id.push_back(c);
      in_run = false;
    } else if (!in_run) {
      id.push_back('_');
      in_run = true;
    }
  }
  if (!id.empty() && std::isdigit(static_cast<unsigned char>(id.front()))) id.insert(0, "_");
  return id.empty() ? "lib" : id;
}

std::string library_spec(const LibraryUse& use) {
  return use.range == "*" ? use.name : use.name + "@" + use.range;
}

std::vector<ElementId> descendants_of(const Model& model, std::string_view id) {
  std::vector<ElementId> result{std::string(id)};
  // A model read from JSON may have parents in a cycle: each id once, so the walk ends.
  std::set<std::string, std::less<>> seen{std::string(id)};
  for (std::size_t i = 0; i < result.size(); ++i) {
    // Copy: push_back below may reallocate `result`.
    const std::string current = result[i];
    for (const auto& el : model.elements) {
      if (el.parent && *el.parent == current && seen.insert(el.id).second) result.push_back(el.id);
    }
  }
  return result;
}

bool is_within(std::string_view id, std::string_view ancestor) {
  return id.starts_with(ancestor) && (id.size() == ancestor.size() || id[ancestor.size()] == '.');
}

std::map<ElementId, ElementId, std::less<>> renamed_ids(const Model& model, std::string_view id,
                                                        std::string_view to) {
  std::map<ElementId, ElementId, std::less<>> moved;
  for (auto& e : descendants_of(model, id)) {
    std::string now = is_within(e, id) ? std::string(to).append(e, id.size()) : e;
    moved.emplace(std::move(e), std::move(now));
  }
  return moved;
}

Element make_element(ElementId id, std::string kind, std::optional<std::string> name,
                     std::optional<ElementId> parent) {
  Element e;
  e.name = name ? *name : id;
  e.id = std::move(id);
  e.kind = std::move(kind);
  e.parent = std::move(parent);
  return e;
}

Relationship make_relationship(RelationshipId id, Endpoint source, Endpoint target,
                               std::optional<std::string> label) {
  Relationship r;
  r.id = std::move(id);
  r.source = std::move(source);
  r.target = std::move(target);
  r.label = std::move(label);
  return r;
}

View make_view(ViewId id, std::optional<ElementId> scope) {
  View v;
  v.id = std::move(id);
  v.scope = std::move(scope);
  return v;
}

namespace {

bool tagged(const std::vector<std::string>& tags, const std::optional<std::string>& tag) {
  return !tag || std::find(tags.begin(), tags.end(), *tag) != tags.end();
}

/// `*` 0, kind 1, tag 2, kind and tag 3.
int specificity(const StyleRule& rule) { return (rule.kind ? 1 : 0) + (rule.tag ? 2 : 0); }

template <class Item>
Style merged(const std::vector<StyleRule>& rules, const Item& item) {
  Style out;
  for (int level = 0; level <= 3; ++level)
    for (const auto& rule : rules)
      if (specificity(rule) == level && style_applies(rule, item))
        for (const auto& [k, v] : rule.style) out.set(k, v);
  return out;
}

}  // namespace

bool style_applies(const StyleRule& rule, const Element& element) {
  return rule.target == StyleTarget::Elements && (!rule.kind || *rule.kind == element.kind) &&
         tagged(element.tags, rule.tag);
}

bool style_applies(const StyleRule& rule, const Relationship& relationship) {
  return rule.target == StyleTarget::Relationships && tagged(relationship.tags, rule.tag);
}

Style rule_style(const std::vector<StyleRule>& rules, const Element& element) { return merged(rules, element); }
Style rule_style(const std::vector<StyleRule>& rules, const Relationship& relationship) {
  return merged(rules, relationship);
}

}  // namespace dd
