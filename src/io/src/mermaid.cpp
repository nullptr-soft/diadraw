// Port of packages/import/src/mermaid.ts. The regular expressions are the TypeScript ones, run by
// std::regex in its ECMAScript grammar.

#include <map>
#include <regex>
#include <set>

#include "diadraw/base/text.hpp"
#include "diadraw/io/import.hpp"

namespace dd::io {

namespace {

struct ShapeSpec {
  std::string_view open, close, kind, shape;
};

/// Node shapes, longest delimiters first so `([` wins over `(`.
constexpr ShapeSpec kShapes[] = {
    {"([", "])", "start", "stadium"},       {"[(", ")]", "database", ""},      {"[[", "]]", "process", ""},
    {"((", "))", "process", "circle"},      {"{{", "}}", "process", "hexagon"}, {"[/", "/]", "process", "parallelogram"},
    {"[\\", "\\]", "process", "parallelogram"}, {">", "]", "process", "flag"}, {"[", "]", "process", ""},
    {"(", ")", "process", ""},              {"{", "}", "decision", ""},
};

struct NodeRef {
  std::string id;
  std::optional<std::string> label, kind, shape;
};

struct Link {
  LineStyle line = LineStyle::Solid;
  bool arrow = false;
  bool both = false;
  std::optional<std::string> label;
};

std::string trim_start(std::string_view s) {
  std::string_view t = js_trim(s);
  // js_trim also trims the end; keep the end as it was.
  return std::string(s.substr(static_cast<std::size_t>(t.data() - s.data())));
}

std::string strip_class(const std::string& rest) {
  static const std::regex re("^:::[\\w-]+");
  return std::regex_replace(rest, re, "", std::regex_constants::format_first_only);
}

/// Reads `A`, `A[Label]`, `A{Label}` … at the start of `text`.
std::optional<std::pair<NodeRef, std::string>> read_node(const std::string& text) {
  static const std::regex id_re("^[A-Za-z0-9_]\\w*");
  std::smatch m;
  if (!std::regex_search(text, m, id_re)) return std::nullopt;
  std::string id = m[0];
  std::string rest = text.substr(id.size());
  for (const auto& s : kShapes) {
    if (rest.compare(0, s.open.size(), s.open) != 0) continue;
    auto end = rest.find(s.close, s.open.size());
    if (end == std::string::npos) continue;
    std::string label(js_trim(std::string_view(rest).substr(s.open.size(), end - s.open.size())));
    if (label.size() >= 1 && label.front() == '"' && label.back() == '"')
      label = label.size() >= 2 ? label.substr(1, label.size() - 2) : "";
    rest = rest.substr(end + s.close.size());
    NodeRef node{id, plain_text(label), std::string(s.kind), std::nullopt};
    if (!s.shape.empty()) node.shape = std::string(s.shape);
    return std::pair{node, strip_class(rest)};
  }
  return std::pair{NodeRef{id, std::nullopt, std::nullopt, std::nullopt}, strip_class(rest)};
}

bool arrowish(const std::string& op) { return op.find_first_of(">ox") != std::string::npos; }

/// Reads a link operator such as `-->`, `-.->`, `==>`, `-- text -->` or `-->|text|`.
std::optional<std::pair<Link, std::string>> read_link(const std::string& text) {
  static const std::regex inline_re(
      "^(<?)(--|-\\.|==)\\s+([^-.=>][^>]*?)\\s+(-->|--o|--x|---|\\.->|\\.-|==>|===)");
  static const std::regex plain_re("^(<?)(-{2,}>|-{3,}|-\\.+->|-\\.+-|={2,}>|={3,}|--o|--x)");
  static const std::regex pipe_re("^\\s*\\|([^|]*)\\|");
  std::string t = trim_start(text);
  std::smatch m;
  Link link;
  std::string rest;
  if (std::regex_search(t, m, inline_re)) {
    std::string op = m[2].str() + m[4].str();
    link.line = op.find('.') != std::string::npos ? LineStyle::Dashed : LineStyle::Solid;
    link.arrow = arrowish(m[4].str());
    link.both = m[1].str() == "<";
    link.label = plain_text(m[3].str());
    rest = t.substr(static_cast<std::size_t>(m[0].length()));
  } else if (std::regex_search(t, m, plain_re)) {
    std::string op = m[2].str();
    link.line = op.find('.') != std::string::npos ? LineStyle::Dashed : LineStyle::Solid;
    link.arrow = arrowish(op);
    link.both = m[1].str() == "<";
    rest = t.substr(static_cast<std::size_t>(m[0].length()));
  } else {
    return std::nullopt;
  }
  if (std::regex_search(rest, m, pipe_re)) {
    link.label = plain_text(m[1].str());
    rest = rest.substr(static_cast<std::size_t>(m[0].length()));
  }
  return std::pair{link, rest};
}

/// Reads `A & B & C`.
std::optional<std::pair<std::vector<NodeRef>, std::string>> read_group(const std::string& text) {
  std::vector<NodeRef> nodes;
  std::string rest = trim_start(text);
  while (true) {
    auto r = read_node(rest);
    if (!r) {
      if (nodes.empty()) return std::nullopt;
      return std::pair{nodes, rest};
    }
    nodes.push_back(r->first);
    rest = trim_start(r->second);
    if (rest.empty() || rest.front() != '&') return std::pair{nodes, rest};
    rest = trim_start(rest.substr(1));
  }
}

std::vector<std::string> split_lines(std::string_view s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    auto nl = s.find('\n', start);
    std::string_view line = s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    out.emplace_back(line);
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  return out;
}

}  // namespace

ImportResult import_mermaid(std::string_view source) {
  static const std::map<std::string, Direction> kDirections{
      {"TD", Direction::Down}, {"TB", Direction::Down}, {"BT", Direction::Up}, {"LR", Direction::Right}, {"RL", Direction::Left}};
  static const std::regex head_re("^(flowchart|graph)\\b\\s*(\\w+)?");
  static const std::regex sub_re("^subgraph\\s+(?:([\\w-]+)\\s*\\[\\s*\"?([^\\]\"]*)\"?\\s*\\]|\"([^\"]*)\"|(.+))$");
  static const std::regex dir_re("^direction\\s+(\\w+)$");
  static const std::regex style_re("^(classDef|class|style|linkStyle|click|accTitle|accDescr)\\b");

  ModelBuilder b;
  Direction direction = Direction::Down;
  std::vector<GroupId> groups;
  std::map<std::string, NodeRef> nodes;
  std::vector<std::string> order;
  struct LinkAt {
    std::string from, to;
    Link link;
    int line;
  };
  std::vector<LinkAt> links;
  std::map<std::string, GroupId> group_of;
  bool header = false;

  auto lines = split_lines(source);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const int line_no = static_cast<int>(i) + 1;
    std::string line = lines[i];
    if (auto pct = line.find("%%"); pct != std::string::npos) line.erase(pct);
    line = std::string(js_trim(line));
    if (!line.empty() && line.back() == ';') line.pop_back();
    if (line.empty()) continue;
    std::smatch m;
    if (std::regex_search(line, m, head_re)) {
      header = true;
      if (m[2].matched) {
        std::string d = m[2].str();
        for (auto& c : d) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (auto it = kDirections.find(d); it != kDirections.end()) direction = it->second;
      }
      continue;
    }
    if (!header) {
      b.drop("Not a flowchart line: " + line, line_no);
      continue;
    }
    if (std::regex_search(line, m, sub_re)) {
      std::string name = m[2].matched ? m[2].str() : m[3].matched ? m[3].str() : m[4].matched ? m[4].str() : m[1].matched ? m[1].str() : "Group";
      name = std::string(js_trim(name));
      groups.push_back(b.add_group(name, groups.empty() ? std::nullopt : std::optional<GroupId>(groups.back())));
      continue;
    }
    if (line == "end") {
      if (groups.empty())
        b.drop("Unmatched \"end\"", line_no);
      else
        groups.pop_back();
      continue;
    }
    if (std::regex_search(line, m, dir_re)) {
      b.drop("Per-subgraph direction is not supported; using the chart direction", line_no);
      continue;
    }
    if (std::regex_search(line, m, style_re)) {
      auto space = line.find_first_of(" \t\n\r\f\v");
      b.drop("Styling and interaction are not imported: " + line.substr(0, space), line_no);
      continue;
    }

    // A statement: group (link group)*
    auto first = read_group(line);
    if (!first) {
      b.drop("Could not read: " + line, line_no);
      continue;
    }
    auto remember = [&](const NodeRef& n) {
      auto known = nodes.find(n.id);
      if (known == nodes.end()) order.push_back(n.id);
      if (known == nodes.end()) {
        nodes[n.id] = n;
      } else if (n.label) {
        // { ...known, ...n }: fields n has replace those of known.
        NodeRef& k = known->second;
        k.label = n.label;
        if (n.kind) k.kind = n.kind;
        if (n.shape) k.shape = n.shape;
      }
      if (!groups.empty() && !group_of.count(n.id)) group_of[n.id] = groups.back();
    };
    for (const auto& n : first->first) remember(n);
    std::string rest = first->second;
    while (!js_trim(rest).empty()) {
      auto l = read_link(rest);
      auto next = l ? read_group(l->second) : std::nullopt;
      if (!l || !next) {
        b.drop("Could not read the rest of the line: " + std::string(js_trim(rest)), line_no);
        break;
      }
      for (const auto& n : next->first) remember(n);
      for (const auto& from : first->first)
        for (const auto& to : next->first) links.push_back({from.id, to.id, l->first, line_no});
      first = next;
      rest = next->second;
    }
  }

  if (!header) b.drop("No \"flowchart\" or \"graph\" header; nothing imported");

  // Stadium nodes read as start or end depending on where links go.
  std::set<std::string> incoming, outgoing;
  for (const auto& l : links) {
    incoming.insert(l.to);
    outgoing.insert(l.from);
  }
  for (const auto& id : order) {
    const NodeRef& n = nodes.at(id);
    std::string kind = n.kind.value_or("process");
    if (n.shape == std::optional<std::string>("stadium"))
      kind = !incoming.count(id) ? "start" : !outgoing.count(id) ? "end" : "process";
    ElementInit init;
    init.kind = kind;
    init.name = n.label && !n.label->empty() ? *n.label : id;
    if (n.shape && !n.shape->empty() && *n.shape != "stadium") init.shape = n.shape;
    if (auto g = group_of.find(id); g != group_of.end()) init.group = g->second;
    b.add_element(id, std::move(init));
  }
  bool undirected = false;
  for (const auto& l : links) {
    if (!l.link.arrow) undirected = true;
    RelationshipInit init;
    init.line = l.link.line;
    init.bidirectional = l.link.both;
    if (l.link.label && !l.link.label->empty()) init.label = l.link.label;
    b.add_relationship(l.from, l.to, std::move(init), l.line);
  }
  if (undirected) b.note("Links without arrowheads were imported as directed relationships");
  ViewInit view;
  view.direction = direction;
  b.add_view("main", std::move(view));
  return b.result();
}

}  // namespace dd::io
