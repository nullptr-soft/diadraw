// Port of packages/dsl/src/print.ts.

#include "diadraw/dsl/print.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "diadraw/base/json.hpp"
#include "diadraw/dsl/parse.hpp"

namespace dd::dsl {

namespace {

constexpr std::string_view kIndent = "  ";

std::string indent(int depth) {
  std::string out;
  for (int i = 0; i < depth; ++i) out += kIndent;
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

/// ` style "…"` and one ` prop "key" "value"` per property.
template <class T>
std::string print_extras(const T& item) {
  std::string out;
  // An empty style still marks the item as styled (drawn as its own shape), so it is printed.
  if (item.style) out += " style " + quote(print_style_text(*item.style));
  for (const auto& [k, v] : item.properties) out += " prop " + quote(k) + " " + quote(v);
  return out;
}

std::string num(double d) { return format_number(d); }

std::string_view direction_name(Direction d) {
  switch (d) {
    case Direction::Right: return "right";
    case Direction::Left: return "left";
    case Direction::Up: return "up";
    default: return "down";
  }
}

std::string_view frame_name(FrameKind k) {
  switch (k) {
    case FrameKind::Alt: return "alt";
    case FrameKind::Opt: return "opt";
    case FrameKind::Loop: return "loop";
    case FrameKind::Par: return "par";
    case FrameKind::Critical: return "critical";
    case FrameKind::Break: return "break";
    default: return "group";
  }
}

std::string_view side_name(PortSide s) {
  switch (s) {
    case PortSide::North: return "north";
    case PortSide::South: return "south";
    case PortSide::East: return "east";
    default: return "west";
  }
}

}  // namespace

bool is_identifier(std::string_view s) {
  auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
  if (s.empty() || !start(s.front())) return false;
  return std::all_of(s.begin() + 1, s.end(), [&](char c) { return start(c) || (c >= '0' && c <= '9'); });
}

std::string local_name(const Element& element) {
  if (element.parent && element.id.size() > element.parent->size() + 1 &&
      element.id.compare(0, element.parent->size(), *element.parent) == 0 &&
      element.id[element.parent->size()] == '.') {
    return element.id.substr(element.parent->size() + 1);
  }
  auto dot = element.id.rfind('.');
  return dot == std::string::npos ? element.id : element.id.substr(dot + 1);
}

std::string print_use(const LibraryUse& use) {
  std::string alias = use.alias == default_library_alias(use.name) ? "" : " as " + use.alias;
  return "use " + quote(library_spec(use)) + alias;
}

std::string print_endpoint(const Endpoint& e) {
  return e.port ? e.element + "." + *e.port : e.element;
}

std::string print_element_head(const Element& element, std::string_view indent) {
  std::string local = local_name(element);
  std::string line = local + " = " + element.kind;
  if (element.name != local) line += " " + quote(element.name);
  if (element.technology) line += " tech " + quote(*element.technology);
  if (element.shape) line += " shape " + *element.shape;
  for (const auto& tag : element.tags) line += " tag " + quote(tag);
  if (element.description) line += " description " + quote_lines(*element.description, indent);
  return line + print_extras(element);
}

std::string print_relationship(const Relationship& r) {
  std::string arrow = r.bidirectional ? "<->" : r.line == LineStyle::Dashed ? "-->" : "->";
  std::string line = print_endpoint(r.source) + " " + arrow + " " + print_endpoint(r.target);
  // A derived id (`a->b`, `a->b#2`) is never a name, so a name is always an explicit id. Other
  // ids that are not names cannot be written and come back derived.
  if (is_identifier(r.id)) line = r.id + " = " + line;
  if (r.label) line += " " + quote(*r.label);
  if (r.technology) line += " tech " + quote(*r.technology);
  for (const auto& tag : r.tags) line += " tag " + quote(tag);
  return line + print_extras(r);
}

std::string print_hint(const LayoutHint& hint) {
  if (const auto* h = std::get_if<PinHint>(&hint)) {
    std::string_view placement = h->placement == Placement::Below   ? "below"
                                 : h->placement == Placement::Above ? "above"
                                 : h->placement == Placement::LeftOf ? "left of"
                                                                     : "right of";
    return "pin " + h->element + " " + std::string(placement) + " " + h->reference;
  }
  if (const auto* h = std::get_if<PinAtHint>(&hint)) {
    std::string out = "pin " + h->element + " at " + num(h->x) + ", " + num(h->y);
    if (h->width && h->height) out += " size " + num(*h->width) + ", " + num(*h->height);
    return out;
  }
  if (const auto* h = std::get_if<RouteHint>(&hint)) {
    std::string line = "route " + quote(h->relationship);
    if (!h->points.empty()) {
      std::vector<std::string> pts;
      for (const auto& p : h->points) pts.push_back(num(p.x) + ", " + num(p.y));
      line += " via " + join(pts, " ");
    }
    if (h->exit) line += " from " + num(h->exit->x) + ", " + num(h->exit->y);
    if (h->entry) line += " to " + num(h->entry->x) + ", " + num(h->entry->y);
    return line;
  }
  if (const auto* h = std::get_if<AlignHint>(&hint)) {
    return "align " + join(h->elements, " ") + " " +
           (h->axis == Axis::Vertical ? "vertically" : "horizontally");
  }
  if (const auto* h = std::get_if<SizeHint>(&hint))
    return "size " + h->element + " " + num(h->width) + ", " + num(h->height);
  if (const auto* h = std::get_if<RotateHint>(&hint)) return "rotate " + h->element + " " + num(h->degrees);
  return "order " + join(std::get<OrderHint>(hint).elements, " ");
}

std::string print_decoration(const Decoration& d) {
  std::string out = d.kind == DecorationKind::Note ? "note" : d.kind == DecorationKind::Text ? "text" : "box";
  if (d.name) out += " " + *d.name;
  if (d.text) out += " " + quote(*d.text);
  if (d.placement && d.reference) {
    std::string_view placement = *d.placement == Placement::Below   ? "below"
                                 : *d.placement == Placement::Above ? "above"
                                 : *d.placement == Placement::LeftOf ? "left of"
                                                                     : "right of";
    out += " " + std::string(placement) + " " + *d.reference;
  } else if (!d.around.empty()) {
    out += " around " + join(d.around, " ");
  } else if (d.at) {
    out += " at " + num(d.at->x) + ", " + num(d.at->y);
    if (d.width && d.height) out += " size " + num(*d.width) + ", " + num(*d.height);
  }
  if (d.style) out += " style " + quote(print_style_text(*d.style));
  return out;
}

void print_sequence(const std::vector<SequenceItem>& items, int depth, std::vector<std::string>& out) {
  std::string pad = indent(depth);
  auto label = [](const std::optional<std::string>& l) { return l ? " " + quote(*l) : std::string(); };
  for (const auto& item : items) {
    if (const auto* m = std::get_if<SequenceMessage>(&item)) {
      std::string_view arrow = m->style == MessageStyle::Async   ? "->>"
                               : m->style == MessageStyle::Reply ? "-->"
                                                                 : "->";
      out.push_back(pad + m->from + " " + std::string(arrow) + " " + m->to + label(m->label));
    } else if (const auto* a = std::get_if<SequenceActivation>(&item)) {
      out.push_back(pad + (a->activate ? "activate " : "deactivate ") + a->element);
    } else if (const auto* n = std::get_if<SequenceNote>(&item)) {
      if (n->position == NotePosition::Over) {
        out.push_back(pad + "note over " + join(n->over, " ") + " " + quote(n->text));
      } else {
        out.push_back(pad + "note " + (n->position == NotePosition::Left ? "left" : "right") +
                      " of " + (n->over.empty() ? std::string() : n->over[0]) + " " + quote(n->text));
      }
    } else if (const auto* d = std::get_if<SequenceDivider>(&item)) {
      out.push_back(pad + "divider" + label(d->label));
    } else if (const auto* dl = std::get_if<SequenceDelay>(&item)) {
      out.push_back(pad + "delay" + label(dl->label));
    } else if (const auto* r = std::get_if<SequenceRef>(&item)) {
      out.push_back(pad + "ref over " + join(r->over, " ") + " " + quote(r->label));
    } else if (const auto* f = std::get_if<SequenceFrame>(&item)) {
      for (std::size_t i = 0; i < f->sections.size(); ++i) {
        std::string head = i == 0 ? pad + std::string(frame_name(f->frame)) : pad + "} else";
        out.push_back(head + label(f->sections[i].label) + " {");
        print_sequence(f->sections[i].items, depth + 1, out);
      }
      if (f->sections.empty()) out.push_back(pad + std::string(frame_name(f->frame)) + " {");
      out.push_back(pad + "}");
    }
  }
}

std::string print_layout(const View& view) {
  std::string line = "layout " + view.layout.algorithm + " direction " +
                     std::string(direction_name(view.layout.direction));
  if (view.layout.spacing) line += " spacing " + num(*view.layout.spacing);
  return line;
}

std::string print_view(const View& view) {
  std::vector<std::string> lines;
  lines.push_back("view " + view.id + (view.scope ? " of " + *view.scope : "") + " {");
  std::string include = join(view.include, " ");
  lines.push_back(std::string(kIndent) + "include " + (include.empty() ? "*" : include));
  if (!view.exclude.empty()) lines.push_back(std::string(kIndent) + "exclude " + join(view.exclude, " "));
  bool is_default = view.layout.algorithm == "layered" && view.layout.direction == Direction::Down &&
                    !view.layout.spacing;
  if (!is_default) lines.push_back(std::string(kIndent) + print_layout(view));
  if (view.theme) lines.push_back(std::string(kIndent) + "theme " + *view.theme);
  for (const auto& hint : view.hints) lines.push_back(std::string(kIndent) + print_hint(hint));
  for (const auto& d : view.decorations) lines.push_back(std::string(kIndent) + print_decoration(d));
  if (view.sequence) {
    lines.push_back(std::string(kIndent) + "sequence {");
    if (view.autonumber.value_or(false)) lines.push_back(indent(2) + "autonumber");
    print_sequence(*view.sequence, 2, lines);
    lines.push_back(std::string(kIndent) + "}");
  }
  lines.push_back("}");
  return join(lines, "\n");
}

std::string print_style_selector(const StyleRule& rule) {
  std::string out;
  if (rule.target == StyleTarget::Relationships)
    out = "relationship";
  else if (rule.kind)
    out = *rule.kind;
  else if (!rule.tag)
    out = "*";
  if (rule.tag) out += (out.empty() ? "" : " ") + std::string("tag ") + quote(*rule.tag);
  return out;
}

std::string print_style_rule(const StyleRule& rule) {
  std::string head = "style " + print_style_selector(rule);
  if (rule.style.empty()) return head + " {}";
  std::string out = head + " {";
  for (const auto& [k, v] : rule.style) out += "\n" + std::string(kIndent) + k + " " + print_style_value(v);
  return out + "\n}";
}

std::string print_theme_def(const ThemeDef& theme) {
  std::string out = "theme " + theme.name + (theme.base ? " extends " + *theme.base : "") + " {";
  if (theme.values.empty() && theme.nodes.empty()) return out + "}";
  for (const auto& [k, v] : theme.values) out += "\n" + std::string(kIndent) + k + " " + print_style_value(v);
  for (const auto& [kind, look] : theme.nodes) {
    out += "\n" + std::string(kIndent) + "node " + kind + " {";
    for (const auto& [k, v] : look) out += "\n" + indent(2) + k + " " + print_style_value(v);
    out += "\n" + std::string(kIndent) + "}";
  }
  return out + "\n}";
}

std::string print(const Model& model) {
  // Children by parent id ("" for top level), in model order.
  std::map<std::string, std::vector<const Element*>, std::less<>> children;
  for (const auto& e : model.elements) {
    std::string parent = e.parent && model.elements.contains(*e.parent) ? *e.parent : "";
    children[parent].push_back(&e);
  }
  auto kids_of = [&](const std::string& id) -> const std::vector<const Element*>& {
    static const std::vector<const Element*> kNone;
    auto it = children.find(id);
    return it == children.end() ? kNone : it->second;
  };

  std::vector<std::string> out;
  if (model.libraries) {
    for (const auto& use : *model.libraries) out.push_back(print_use(use));
    if (!model.libraries->empty()) out.emplace_back();
  }
  for (const auto& theme : model.themes) {
    out.push_back(print_theme_def(theme));
    out.emplace_back();
  }
  if (model.theme) {
    out.push_back("theme " + *model.theme);
    out.emplace_back();
  }
  for (const auto& rule : model.styles) {
    out.push_back(print_style_rule(rule));
    out.emplace_back();
  }

  std::function<void(const Element&, int)> emit_element;
  std::function<void(const std::vector<const Element*>&, int)> emit_context;

  emit_element = [&](const Element& e, int depth) {
    std::string pad = indent(depth);
    const auto& kids = kids_of(e.id);
    if (kids.empty() && e.ports.empty()) {
      out.push_back(pad + print_element_head(e, pad));
      return;
    }
    out.push_back(pad + print_element_head(e, pad) + " {");
    for (const auto& port : e.ports) {
      out.push_back(pad + std::string(kIndent) + "port " + port.id +
                    (port.side ? " " + std::string(side_name(*port.side)) : ""));
    }
    emit_context(kids, depth + 1);
    out.push_back(pad + "}");
  };

  // Within one parent, ungrouped elements come first, then each group block in turn.
  emit_context = [&](const std::vector<const Element*>& items, int depth) {
    std::map<std::string, std::vector<const Element*>, std::less<>> by_group;
    std::vector<const Element*> ungrouped;
    std::set<std::string, std::less<>> used;
    for (const Element* e : items) {
      if (e->group) {
        by_group[*e->group].push_back(e);
        used.insert(*e->group);
      } else {
        ungrouped.push_back(e);
      }
    }
    for (const Element* e : ungrouped) emit_element(*e, depth);
    // Include ancestors of used groups so nesting prints correctly.
    for (const auto& g : std::set<std::string, std::less<>>(used)) {
      const Group* group = model.groups.find(g);
      for (auto p = group ? group->parent : std::nullopt; p;) {
        used.insert(*p);
        const Group* parent = model.groups.find(*p);
        p = parent ? parent->parent : std::nullopt;
      }
    }
    std::function<void(const Group&, int)> emit_group = [&](const Group& group, int d) {
      std::string pad = indent(d);
      out.push_back(pad + "group " + quote(group.name) + " {");
      auto it = by_group.find(group.id);
      if (it != by_group.end())
        for (const Element* e : it->second) emit_element(*e, d + 1);
      for (const auto& sub : model.groups)
        if (sub.parent && *sub.parent == group.id && used.count(sub.id)) emit_group(sub, d + 1);
      out.push_back(pad + "}");
    };
    for (const auto& group : model.groups)
      if (!group.parent && used.count(group.id)) emit_group(group, depth);
  };

  out.emplace_back("model {");
  emit_context(kids_of(""), 1);
  if (!model.relationships.empty() && !model.elements.empty()) out.emplace_back();
  for (const auto& r : model.relationships) out.push_back(std::string(kIndent) + print_relationship(r));
  out.emplace_back("}");

  for (const auto& view : model.views) {
    out.emplace_back();
    out.push_back(print_view(view));
  }
  return join(out, "\n") + "\n";
}

}  // namespace dd::dsl
