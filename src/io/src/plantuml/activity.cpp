// Port of packages/import/src/plantuml/activity.ts.

#include <map>
#include <memory>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

namespace {

struct End {
  std::string key;
  std::optional<std::string> label;
};

void add_link(ModelBuilder& b, const std::string& from, const std::string& to, const std::optional<std::string>& label) {
  RelationshipInit init;
  if (label && !label->empty()) init.label = label;
  b.add_relationship(from, to, std::move(init));
}

}  // namespace

void parse_activity(Ctx& ctx) {
  ModelBuilder& b = ctx.b;
  int count = 0;
  std::vector<End> frontier;
  std::optional<std::string> pending;
  struct Frame {
    std::string kind;
    std::optional<std::string> node;
    std::vector<End> ends;
    bool saw_else = false;
  };
  std::vector<Frame> frames;
  std::vector<std::optional<GroupId>> groups;
  std::map<std::string, GroupId, std::less<>> lanes;
  std::optional<GroupId> lane;
  std::optional<std::string> last;
  std::vector<std::shared_ptr<std::optional<std::string>>> repeat_starts;

  auto node = [&](const std::string& kind, const std::string& name, const std::string& shape) -> std::string {
    std::string key = ctx.key("n" + std::to_string(++count));
    std::optional<GroupId> g = !groups.empty() && groups.back() ? groups.back() : lane;
    ElementInit init;
    init.kind = kind;
    init.name = name;
    init.local = kind == "decision" ? "decision" : kind == "start" ? "start" : kind == "end" ? "stop" : !name.empty() ? name : shape;
    init.shape = shape;
    init.group = g;
    b.add_element(key, std::move(init));
    ctx.include.push_back(key);
    for (auto& r : repeat_starts)
      if (!*r) *r = key;
    return key;
  };
  auto link = [&](const std::string& to, const std::vector<End>& from) {
    for (const auto& f : from) add_link(b, f.key, to, f.label ? f.label : pending);
    pending.reset();
  };
  auto step = [&](const std::string& kind, const std::string& name, const std::string& shape) -> std::string {
    std::string key = node(kind, name, shape);
    link(key, frontier);
    frontier = {{key, std::nullopt}};
    last = key;
    return key;
  };
  // `f.node!` of a frame without a node is undefined in the TypeScript, and links from it report.
  auto node_of = [](const Frame& f) { return f.node.value_or("undefined"); };
  auto with_label = [](const std::string& key, const std::optional<std::string>& label) {
    End e{key, std::nullopt};
    if (label && !label->empty()) e.label = label;
    return e;
  };

  static const Regex kStart("^start$", "i");
  static const Regex kStop("^(stop|end)$", "i");
  static const Regex kKill("^(kill|detach)$", "i");
  static const Regex kActionEnd(R"re([;|<>/\]}]\s*$)re");
  static const Regex kArrow(R"re(^-+>\s*(.*?);?$)re");
  static const Regex kArrowStyled(R"re(^-\[[^\]]*\]->\s*(.*?);?$)re");
  static const Regex kIf(R"re(^if\s*\((.*?)\)\s*(?:then\s*(?:\((.*)\))?|is\s*\((.*?)\)\s*then)?\s*$)re", "i");
  static const Regex kElseIf(R"re(^else\s*if\s*\((.*?)\)\s*(?:then\s*(?:\((.*)\))?|is\s*\((.*?)\)\s*then)?\s*$)re", "i");
  static const Regex kElse(R"re(^else\s*(?:\((.*)\))?\s*$)re", "i");
  static const Regex kEndIf("^end\\s*if$", "i");
  static const Regex kWhile(R"re(^while\s*\((.*?)\)\s*(?:is\s*\((.*)\))?\s*$)re", "i");
  static const Regex kEndWhile(R"re(^end\s*while\s*(?:\((.*)\))?\s*$)re", "i");
  static const Regex kRepeat("^repeat\\s*:?", "i");
  static const Regex kRepeatWhileStart("^repeat\\s+while", "i");
  static const Regex kRepeatAction(R"re(^repeat\s*:(.*?);?\s*$)re", "i");
  static const Regex kRepeatWhile(R"re(^repeat\s+while\s*\((.*?)\)\s*(?:is\s*\((.*?)\))?\s*(?:not\s*\((.*?)\))?\s*$)re", "i");
  static const Regex kFork("^(fork|split)$", "i");
  static const Regex kForkAgain("^(fork|split)\\s+again$", "i");
  static const Regex kEndFork(R"re(^end\s*(fork|split|merge)(\s*\{.*\})?$)re", "i");
  static const Regex kSwitch(R"re(^switch\s*\((.*)\)\s*$)re", "i");
  static const Regex kCase(R"re(^case\s*\((.*)\)\s*$)re", "i");
  static const Regex kEndSwitch("^end\\s*switch$", "i");
  static const Regex kGroup(R"re(^(partition|group|package|rectangle|card)\s+(.*?)\s*(#\w+)?\s*\{?\s*$)re", "i");
  static const Regex kLink(R"re(\s*\[\[.*\]\])re");
  static const Regex kEndGroup("^end\\s*(group|partition)$", "i");
  static const Regex kLane(R"re(^\|(?:#[\w#]+\|)?([^|]+)\|\s*$)re");
  static const Regex kNote(R"re(^(?:floating\s+)?note\s+(left|right|top|bottom)?\s*(?::\s*(.*))?$)re", "i");
  static const Regex kEndNote("^end\\s*note$", "i");
  static const Regex kConnector(R"re(^(detach|break|backward\s*:.*|\(\w\)|label\s+\w+|goto\s+\w+)\s*;?$)re", "i");
  static const Regex kConnectorWord(R"re([\s:(])re");

  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }
    const int line = lines[i].line;
    std::string t = trim(lines[i].text);

    if (kStart.test(t)) {
      std::string key = node("start", "", "circle");
      frontier = {{key, std::nullopt}};
      last = key;
      continue;
    }
    if (kStop.test(t)) {
      step("end", "", "circle");
      frontier.clear();
      continue;
    }
    if (kKill.test(t)) {
      frontier.clear();
      continue;
    }
    // An action, possibly over several lines, ending in ; | < > / ] }
    if (!t.empty() && t[0] == ':') {
      std::string body = t.substr(1);
      while (!kActionEnd.test(body) && i + 1 < lines.size()) body += "\n" + trim(lines[++i].text);
      body = kActionEnd.replace(body, "", false);
      step("process", plain_label(body), "rounded");
      continue;
    }
    {
      auto m = kArrow.exec(t);
      if (!m) m = kArrowStyled.exec(t);
      if (m) {
        if (!m->view(1).empty()) pending = plain_label(m->view(1));
        else pending.reset();
        continue;
      }
    }
    auto branch_label = [](const js::Match& m) -> std::optional<std::string> {
      if ((m.has(2) && !m.view(2).empty()) || (m.has(3) && !m.view(3).empty()))
        return m.has(2) ? m.str(2) : m.str(3);
      return std::nullopt;
    };
    if (auto m = kIf.exec(t)) {
      std::string d = step("decision", plain_label(m->view(1)), "rhombus");
      frames.push_back({"if", d, {}, false});
      End e{d, std::nullopt};
      if (auto label = branch_label(*m)) e.label = label;
      frontier = {e};
      continue;
    }
    if (auto m = kElseIf.exec(t)) {
      if (frames.empty() || frames.back().kind != "if") {
        ctx.drop(line, "elseif");
        continue;
      }
      Frame& f = frames.back();
      f.ends.insert(f.ends.end(), frontier.begin(), frontier.end());
      std::string d = node("decision", plain_label(m->view(1)), "rhombus");
      add_link(b, *f.node, d, std::string("no"));
      f.node = d;
      End e{d, std::nullopt};
      if (auto label = branch_label(*m)) e.label = label;
      frontier = {e};
      continue;
    }
    if (auto m = kElse.exec(t)) {
      if (frames.empty()) continue;
      Frame& f = frames.back();
      f.ends.insert(f.ends.end(), frontier.begin(), frontier.end());
      f.saw_else = true;
      frontier = {with_label(node_of(f), m->get(1))};
      continue;
    }
    if (kEndIf.test(t)) {
      if (frames.empty()) continue;
      Frame f = std::move(frames.back());
      frames.pop_back();
      std::vector<End> next = f.ends;
      next.insert(next.end(), frontier.begin(), frontier.end());
      if (!f.saw_else) next.push_back({node_of(f), std::nullopt});
      frontier = std::move(next);
      continue;
    }
    if (auto m = kWhile.exec(t)) {
      std::string d = step("decision", plain_label(m->view(1)), "rhombus");
      frames.push_back({"while", d, {}, false});
      frontier = {with_label(d, m->get(2))};
      continue;
    }
    if (auto m = kEndWhile.exec(t)) {
      if (frames.empty()) continue;
      Frame f = std::move(frames.back());
      frames.pop_back();
      link(node_of(f), frontier);
      frontier = {with_label(node_of(f), m->get(1))};
      continue;
    }
    if (kRepeat.test(t) && !kRepeatWhileStart.test(t)) {
      repeat_starts.push_back(std::make_shared<std::optional<std::string>>());
      frames.push_back({"repeat", std::nullopt, {}, false});
      if (auto action = kRepeatAction.exec(t)) step("process", plain_label(action->view(1)), "rounded");
      continue;
    }
    if (auto m = kRepeatWhile.exec(t)) {
      if (!frames.empty()) frames.pop_back();
      std::shared_ptr<std::optional<std::string>> r;
      if (!repeat_starts.empty()) {
        r = repeat_starts.back();
        repeat_starts.pop_back();
      }
      std::string d = step("decision", plain_label(m->view(1)), "rhombus");
      if (r && *r && !(*r)->empty()) add_link(b, d, **r, m->get(2));
      frontier = {with_label(d, m->get(3))};
      continue;
    }
    if (kFork.test(t)) {
      std::string bar = step("process", "", "bar");
      frames.push_back({"fork", bar, {}, false});
      continue;
    }
    if (kForkAgain.test(t)) {
      if (frames.empty()) continue;
      Frame& f = frames.back();
      f.ends.insert(f.ends.end(), frontier.begin(), frontier.end());
      frontier = {{node_of(f), std::nullopt}};
      continue;
    }
    if (kEndFork.test(t)) {
      if (frames.empty()) continue;
      Frame f = std::move(frames.back());
      frames.pop_back();
      std::vector<End> ends = f.ends;
      ends.insert(ends.end(), frontier.begin(), frontier.end());
      std::string join_node = node("process", "", "bar");
      link(join_node, ends);
      frontier = {{join_node, std::nullopt}};
      continue;
    }
    if (auto m = kSwitch.exec(t)) {
      std::string d = step("decision", plain_label(m->view(1)), "rhombus");
      frames.push_back({"switch", d, {}, false});
      frontier.clear();
      continue;
    }
    if (auto m = kCase.exec(t)) {
      if (frames.empty()) continue;
      Frame& f = frames.back();
      f.ends.insert(f.ends.end(), frontier.begin(), frontier.end());
      frontier = {{node_of(f), m->str(1)}};
      continue;
    }
    if (kEndSwitch.test(t)) {
      if (!frames.empty()) {
        Frame f = std::move(frames.back());
        frames.pop_back();
        std::vector<End> next = f.ends;
        next.insert(next.end(), frontier.begin(), frontier.end());
        frontier = std::move(next);
      }
      continue;
    }
    if (auto m = kGroup.exec(t)) {
      std::string name = plain_label(unquote(kLink.replace(m->view(2), "", false)));
      if (name.empty()) name = "group";
      std::optional<GroupId> parent = groups.empty() ? std::nullopt : groups.back();
      groups.emplace_back(b.add_group(name, parent));
      continue;
    }
    if (t == "}" || kEndGroup.test(t)) {
      if (!groups.empty()) groups.pop_back();
      continue;
    }
    if (auto m = kLane.exec(t)) {
      std::string name = plain_label(m->view(1));
      if (!lanes.count(name)) lanes[name] = b.add_group(name);
      lane = lanes[name];
      continue;
    }
    if (auto m = kNote.exec(t)) {
      std::optional<std::string> body = m->get(2);
      if (!body) {
        std::vector<std::string> rest;
        while (++i < lines.size() && !kEndNote.test(trim(lines[i].text))) rest.push_back(trim(lines[i].text));
        body = join(rest, "\n");
      }
      std::string key = node("note", plain_label(*body), "note");
      if (last) {
        RelationshipInit init;
        init.line = LineStyle::Dashed;
        init.style = Style{{"endArrow", "none"}, {"dashed", "1"}};
        b.add_relationship(key, *last, std::move(init));
      }
      continue;
    }
    if (kConnector.test(t)) {
      std::string word = js::to_lower(kConnectorWord.split(t)[0]);
      ctx.drop(line, word.empty() ? "connector" : word);
      continue;
    }
    ctx.drop(line, unsupported_construct(t), t);
  }
}

void parse_legacy_activity(Ctx& ctx) {
  ModelBuilder& b = ctx.b;
  std::map<std::string, std::string, std::less<>> names;
  std::optional<std::string> last;
  struct If {
    std::string decision;
    std::vector<std::string> ends;
    std::optional<std::string> label;
  };
  std::vector<If> ifs;
  std::vector<GroupId> groups;
  int count = 0;

  static const Regex kStartEnd(R"re(^\(\*(top)?\)$)re");
  static const Regex kBar(R"re(^===+\s*(\w+)\s*===+$)re");
  static const Regex kAlias(R"re(^(".*?"|[\w.]+)\s+as\s+(\w+)$)re");

  auto activity = [&](std::string_view raw, bool source) -> std::string {
    std::string t = trim(raw);
    if (kStartEnd.test(t)) {
      std::string id = source ? "(*)start" : "(*)end";
      if (!names.count(id)) {
        std::string key = ctx.key(id);
        ElementInit init;
        init.kind = source ? "start" : "end";
        init.name = "";
        init.local = source ? "start" : "stop";
        init.shape = "circle";
        b.add_element(key, std::move(init));
        names[id] = key;
        ctx.include.push_back(key);
      }
      return names[id];
    }
    auto bar = kBar.exec(t);
    auto alias = kAlias.exec(t);
    std::string name = bar ? "===" + bar->str(1) + "===" : alias ? alias->str(2) : unquote(t);
    std::string label = bar ? "" : alias ? plain_label(unquote(alias->view(1))) : plain_label(unquote(t));
    if (!names.count(name)) {
      std::string key = ctx.key("a" + std::to_string(++count));
      ElementInit init;
      init.kind = "process";
      init.name = label;
      init.local = label.empty() ? "bar" : label;
      init.shape = bar ? "bar" : "rounded";
      if (!groups.empty()) init.group = groups.back();
      b.add_element(key, std::move(init));
      names[name] = key;
      ctx.include.push_back(key);
    }
    return names[name];
  };

  static const Regex kArrow(
      R"re(^(?:([^-\s].*?)\s*)?-+(?:\[([^\]]*)\])?(?:up|down|left|right|u|d|l|r)?-*>\s*(?:\[([^\]]*)\]\s*)?(.+)$)re");
  static const Regex kIf(R"re(^if\s+"?(.*?)"?\s+then$)re", "i");
  static const Regex kElse("^else$", "i");
  static const Regex kEndIf("^end\\s*if$", "i");
  static const Regex kPartition(R"re(^partition\s+(.+?)\s*\{?$)re", "i");
  static const Regex kEndPartition("^end\\s*partition$", "i");
  static const Regex kNote("^note\\b", "i");
  static const Regex kEndNote("^end\\s*note$", "i");

  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }
    const int line = lines[i].line;
    std::string t = trim(lines[i].text);
    if (auto m = kArrow.exec(t)) {
      std::optional<std::string> from = m->has(1) && !m->view(1).empty() ? std::optional(activity(m->view(1), true)) : last;
      std::string to = activity(m->view(4), false);
      std::optional<std::string> label = m->has(2) ? m->get(2) : m->has(3) ? m->get(3) : ifs.empty() ? std::nullopt : ifs.back().label;
      if (!ifs.empty()) ifs.back().label.reset();
      if (from) {
        RelationshipInit init;
        if (label && !label->empty()) init.label = plain_label(*label);
        b.add_relationship(*from, to, std::move(init));
      }
      last = to;
      continue;
    }
    if (auto m = kIf.exec(t)) {
      std::string key = ctx.key("d" + std::to_string(++count));
      ElementInit init;
      init.kind = "decision";
      init.name = plain_label(m->view(1));
      init.local = "decision";
      init.shape = "rhombus";
      b.add_element(key, std::move(init));
      ctx.include.push_back(key);
      if (last) b.add_relationship(*last, key);
      ifs.push_back({key, {}, std::nullopt});
      last = key;
      continue;
    }
    if (kElse.test(t)) {
      if (!ifs.empty()) {
        If& f = ifs.back();
        if (last) f.ends.push_back(*last);
        last = f.decision;
      }
      continue;
    }
    if (kEndIf.test(t)) {
      if (!ifs.empty()) {
        If f = std::move(ifs.back());
        ifs.pop_back();
        if (last) f.ends.push_back(*last);
        if (!f.ends.empty()) last = f.ends.back();
      }
      continue;
    }
    if (auto m = kPartition.exec(t)) {
      std::optional<GroupId> parent = groups.empty() ? std::nullopt : std::optional(groups.back());
      groups.push_back(b.add_group(plain_label(unquote(m->view(1))), parent));
      continue;
    }
    if (t == "}" || kEndPartition.test(t)) {
      if (!groups.empty()) groups.pop_back();
      continue;
    }
    if (kNote.test(t)) {
      if (t.find(':') == std::string::npos)
        while (++i < lines.size() && !kEndNote.test(trim(lines[i].text))) {
        }
      ctx.drop(line, "note");
      continue;
    }
    ctx.drop(line, unsupported_construct(t), t);
  }
}

}  // namespace dd::io::plantuml
