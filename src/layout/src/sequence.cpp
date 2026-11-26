// Port of packages/layout/src/sequence.ts.

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "diadraw/base/text.hpp"
#include "diadraw/layout/layout.hpp"

namespace dd {

namespace {

constexpr double kGap = 60;
constexpr double kChar = 7;  // approximate width of a label character at 12 px
constexpr double kActivation = 10;
constexpr double kInf = std::numeric_limits<double>::infinity();

std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    auto nl = s.find('\n', start);
    out.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

Size text_size(const std::string& text, double min_width = 40) {
  auto lines = split_lines(text);
  double longest = -kInf;
  for (const auto& l : lines) longest = std::max(longest, static_cast<double>(utf16_length(l)));
  return {std::max(min_width, longest * kChar + 16), static_cast<double>(lines.size()) * 16 + 12};
}

/// Participants of a sequence view: listed `include` order first, then any others it uses.
std::vector<ElementId> participant_order(const ResolvedView& view) {
  std::vector<ElementId> order;
  auto visible = [&](const std::string& id) {
    return std::any_of(view.elements.begin(), view.elements.end(), [&](const Element& e) { return e.id == id; });
  };
  auto add = [&](const std::string& id) {
    if (visible(id) && std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
  };
  for (const auto& s : view.view.include)
    if (s != "*" && visible(s)) order.push_back(s);
  std::function<void(const std::vector<SequenceItem>&)> walk = [&](const std::vector<SequenceItem>& items) {
    for (const auto& i : items) {
      if (const auto* m = std::get_if<SequenceMessage>(&i)) {
        add(m->from);
        add(m->to);
      } else if (const auto* f = std::get_if<SequenceFrame>(&i)) {
        for (const auto& s : f->sections) walk(s.items);
      }
    }
  };
  if (view.view.sequence) walk(*view.view.sequence);
  for (const auto& e : view.elements) add(e.id);
  return order;
}

/// A JavaScript Set of ids: insertion order, no duplicates.
struct IdSet {
  std::vector<ElementId> items;
  void add(const ElementId& id) {
    if (std::find(items.begin(), items.end(), id) == items.end()) items.push_back(id);
  }
};

}  // namespace

Geometry sequence_layout(const ResolvedView& view, const Measure& measure) {
  auto order = participant_order(view);
  std::map<std::string, std::size_t, std::less<>> index;
  for (std::size_t i = 0; i < order.size(); ++i) index.emplace(order[i], i);
  auto element_of = [&](const std::string& id) -> const Element& {
    return *std::find_if(view.elements.begin(), view.elements.end(), [&](const Element& e) { return e.id == id; });
  };
  std::vector<Size> heads;
  for (const auto& id : order) heads.push_back(measure.node(element_of(id)));
  double head_height = 40;
  for (const auto& h : heads) head_height = std::max(head_height, h.height);

  // Column centres: wide enough for the heads, then for labels of messages between columns.
  std::vector<double> xs;
  for (std::size_t i = 0; i < heads.size(); ++i)
    xs.push_back(i == 0 ? heads[i].width / 2 : xs[i - 1] + heads[i - 1].width / 2 + kGap + heads[i].width / 2);
  std::function<void(const std::vector<SequenceItem>&)> widen = [&](const std::vector<SequenceItem>& items) {
    for (const auto& item : items) {
      if (const auto* m = std::get_if<SequenceMessage>(&item); m && m->label && !m->label->empty()) {
        auto ai = index.find(m->from);
        auto bi = index.find(m->to);
        if (ai == index.end() || bi == index.end()) continue;
        std::size_t a = ai->second, b = bi->second;
        double need = text_size(*m->label).width + 24;
        if (a == b) {
          if (a + 1 < xs.size() && xs[a + 1] - xs[a] < need + 40) {
            double d = need + 40 - (xs[a + 1] - xs[a]);
            for (std::size_t k = a + 1; k < xs.size(); ++k) xs[k] += d;
          }
          continue;
        }
        std::size_t lo = std::min(a, b), hi = std::max(a, b);
        double have = xs[hi] - xs[lo];
        if (have < need)
          for (std::size_t k = hi; k < xs.size(); ++k) xs[k] += need - have;
      } else if (const auto* f = std::get_if<SequenceFrame>(&item)) {
        for (const auto& s : f->sections) widen(s.items);
      }
    }
  };
  if (view.view.sequence) widen(*view.view.sequence);

  Geometry g;
  for (std::size_t i = 0; i < order.size(); ++i) {
    const Size& h = heads[i];
    g.nodes.set(order[i], Box{xs[i] - h.width / 2, head_height - h.height, h.width, h.height});
  }

  SequenceGeometry seq;
  std::vector<std::pair<ElementId, std::vector<double>>> open;
  auto stack_of = [&](const std::string& id) -> std::vector<double>* {
    for (auto& [k, v] : open)
      if (k == id) return &v;
    return nullptr;
  };
  auto depth = [&](const std::string& id) {
    auto* s = stack_of(id);
    return s ? static_cast<double>(s->size()) : 0.0;
  };
  auto x_of = [&](const std::string& id) { return xs[index.at(id)]; };
  double left = 0;
  for (const auto& b : g.nodes) left = std::min(left, b.x);
  left -= 20;
  double right = 0;
  for (const auto& b : g.nodes) right = std::max(right, b.x + b.width);
  right += 20;
  int number = 0;
  double y = head_height + 30;
  const bool autonumber = view.view.autonumber.value_or(false);

  std::function<IdSet(const std::vector<SequenceItem>&, int)> run = [&](const std::vector<SequenceItem>& items,
                                                                        int nesting) {
    IdSet involved;
    for (const auto& item : items) {
      if (const auto* m = std::get_if<SequenceMessage>(&item)) {
        if (!index.count(m->from) || !index.count(m->to)) continue;
        involved.add(m->from);
        involved.add(m->to);
        std::optional<std::string> text = m->label;
        if (autonumber) text = std::string(trim(std::to_string(++number) + " " + m->label.value_or("")));
        bool has_text = text && !text->empty();
        std::optional<Size> size;
        if (has_text) size = text_size(*text);
        y += size ? size->height - 6 : 12;
        double x1 = x_of(m->from);
        double x2 = x_of(m->to);
        std::vector<Point> points;
        if (m->from == m->to) {
          double x = x1 + (depth(m->from) * kActivation) / 2;
          points = {{x, y}, {x + 40, y}, {x + 40, y + 18}, {x, y + 18}};
        } else {
          double dir = x2 > x1 ? 1 : x2 < x1 ? -1 : 0;
          points = {{x1 + (dir * depth(m->from) * kActivation) / 2, y},
                    {x2 - (dir * depth(m->to) * kActivation) / 2, y}};
        }
        double label_x = m->from == m->to ? x1 + 46 : std::min(x1, x2) + 8;
        SequenceMessageGeometry mg{m->from, m->to, m->style, std::move(points), std::nullopt, std::nullopt};
        if (has_text) mg.text = text;
        if (size) mg.label = Box{label_x, y - size->height + 2, size->width, size->height - 6};
        seq.messages.push_back(std::move(mg));
        y += m->from == m->to ? 30 : 14;
      } else if (const auto* a = std::get_if<SequenceActivation>(&item)) {
        if (a->activate) {
          if (!index.count(a->element)) continue;
          involved.add(a->element);
          if (auto* s = stack_of(a->element))
            s->push_back(y - 6);
          else
            open.push_back({a->element, {y - 6}});
        } else {
          auto* stack = stack_of(a->element);
          if (!stack || stack->empty()) continue;
          double start = stack->back();
          stack->pop_back();
          double d = static_cast<double>(stack->size());
          seq.activations.push_back({a->element, Box{x_of(a->element) - kActivation / 2 + (d * kActivation) / 2, start,
                                                     kActivation, std::max(10.0, y - start)}});
          y += 4;
        }
      } else if (const auto* n = std::get_if<SequenceNote>(&item)) {
        std::vector<ElementId> ids;
        for (const auto& id : n->over)
          if (index.count(id)) ids.push_back(id);
        if (ids.empty()) continue;
        for (const auto& id : ids) involved.add(id);
        Size size = text_size(n->text, 60);
        double x;
        double width = size.width;
        if (n->position == NotePosition::Left) {
          x = x_of(ids[0]) - 12 - width;
        } else if (n->position == NotePosition::Right) {
          x = x_of(ids[0]) + 12;
        } else {
          double lo = kInf, hi = -kInf;
          for (const auto& id : ids) {
            lo = std::min(lo, x_of(id));
            hi = std::max(hi, x_of(id));
          }
          width = std::max(width, hi - lo + 40);
          x = (lo + hi) / 2 - width / 2;
        }
        y += 6;
        seq.notes.push_back({Box{x, y, width, size.height}, n->text});
        y += size.height + 10;
      } else if (const auto* dv = std::get_if<SequenceDivider>(&item)) {
        y += 16;
        seq.dividers.push_back({y, dv->label});
        y += 20;
      } else if (const auto* dl = std::get_if<SequenceDelay>(&item)) {
        seq.delays.push_back({y, 30, dl->label});
        y += 34;
      } else if (const auto* r = std::get_if<SequenceRef>(&item)) {
        std::vector<ElementId> ids;
        for (const auto& id : r->over)
          if (index.count(id)) ids.push_back(id);
        if (ids.empty()) continue;
        for (const auto& id : ids) involved.add(id);
        double lo = kInf, hi = -kInf;
        for (const auto& id : ids) {
          lo = std::min(lo, x_of(id));
          hi = std::max(hi, x_of(id));
        }
        lo -= 30;
        hi += 30;
        y += 6;
        seq.refs.push_back({Box{lo, y, std::max(hi - lo, 100.0), 40}, r->label});
        y += 50;
      } else if (const auto* f = std::get_if<SequenceFrame>(&item)) {
        double top = y;
        y += 28;
        std::vector<LabelledY> separators;
        IdSet inner;
        for (std::size_t i = 0; i < f->sections.size(); ++i) {
          if (i > 0) {
            y += 6;
            separators.push_back({y, f->sections[i].label});
            y += 20;
          }
          for (const auto& id : run(f->sections[i].items, nesting + 1).items) inner.add(id);
        }
        y += 10;
        for (const auto& id : inner.items) involved.add(id);
        double pad = 24 + (3 - std::min(nesting, 3)) * 6;
        double lo, hi;
        if (inner.items.empty()) {
          lo = left + 10;
          hi = right - 10;
        } else {
          lo = kInf;
          hi = -kInf;
          for (const auto& id : inner.items) {
            lo = std::min(lo, x_of(id));
            hi = std::max(hi, x_of(id));
          }
          lo = lo - pad - 20;
          hi = hi + pad + 20;
        }
        FrameGeometry fg{f->frame, Box{lo, top, std::max(hi - lo, 120.0), y - top}, std::nullopt, std::move(separators)};
        if (!f->sections.empty()) fg.label = f->sections[0].label;
        seq.frames.push_back(std::move(fg));
        y += 8;
      }
    }
    return involved;
  };
  if (view.view.sequence) run(*view.view.sequence, 0);

  // Close activations that were never deactivated.
  for (const auto& [element, stack] : open) {
    for (std::size_t d = 0; d < stack.size(); ++d) {
      double start = stack[d];
      seq.activations.push_back({element, Box{x_of(element) - kActivation / 2 + (static_cast<double>(d) * kActivation) / 2,
                                              start, kActivation, std::max(10.0, y - start)}});
    }
  }
  double bottom = y + 20;
  for (const auto& id : order) seq.lifelines.set(id, Lifeline{x_of(id), head_height, bottom});

  std::vector<Box> all;
  for (const auto& b : g.nodes) all.push_back(b);
  for (const auto& f : seq.frames) all.push_back(f.box);
  for (const auto& n : seq.notes) all.push_back(n.first);
  for (const auto& r : seq.refs) all.push_back(r.first);
  for (const auto& m : seq.messages)
    if (m.label) all.push_back(*m.label);
  double width = right;
  for (const auto& b : all) width = std::max(width, b.x + b.width);
  double min_left = left;
  for (const auto& b : all) min_left = std::min(min_left, b.x);
  g.width = width;
  g.height = bottom;
  seq.left = min_left;
  seq.right = width;
  g.sequence = std::move(seq);
  return g;
}

}  // namespace dd
