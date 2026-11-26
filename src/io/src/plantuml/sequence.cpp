// Port of packages/import/src/plantuml/sequence.ts.

#include <map>

#include "diadraw/base/text.hpp"
#include "plantuml.hpp"

namespace dd::io::plantuml {

using js::Regex;

namespace {

const KeywordSpec& participant_spec(std::string_view keyword) {
  static const std::map<std::string, KeywordSpec, std::less<>> kParticipants{
      {"participant", {"component", "rect"}}, {"actor", {"person", "actor"}},
      {"boundary", {"component", "circle"}},  {"control", {"component", "circle"}},
      {"entity", {"component", "circle"}},    {"database", {"database", std::nullopt}},
      {"collections", {"component", "rect"}}, {"queue", {"container", "queue"}}};
  auto it = kParticipants.find(keyword);
  return it == kParticipants.end() ? kParticipants.at("participant") : it->second;
}

const std::string kName = R"re("[^"]*"|[\p{L}\p{N}_.@$-]+)re";

/// A level of the stack: the list items go to, and the frame it belongs to.
struct Level {
  std::vector<SequenceItem>* items;
  SequenceFrame* frame = nullptr;
};

}  // namespace

void parse_sequence(Ctx& ctx) {
  ModelBuilder& b = ctx.b;
  std::map<std::string, std::string, std::less<>> names;
  std::vector<std::string> name_order;
  // The stack points at item lists: the root, or a section of a frame still being built. Open
  // frames live on the heap until they end, then go into their parent list, so no list ever holds
  // a pointer into another.
  auto root = std::make_unique<std::vector<SequenceItem>>();
  std::vector<Level> stack{{root.get(), nullptr}};
  std::vector<std::string> callers;
  auto items = [&]() -> std::vector<SequenceItem>& { return *stack.back().items; };

  auto participant = [&](std::string_view raw, const std::string& keyword = "participant",
                         const std::optional<std::string>& label = std::nullopt) -> ElementId {
    std::string name = unquote(raw);
    if (auto it = names.find(name); it != names.end() && !it->second.empty()) return *b.id(it->second);
    const KeywordSpec& spec = participant_spec(keyword);
    std::string key = ctx.key(name);
    ElementInit init;
    init.kind = spec.kind;
    init.name = plain_label(label ? *label : name);
    init.local = name;
    if (spec.shape) init.shape = spec.shape;
    ElementId id = b.add_element(key, std::move(init));
    if (!names.count(name)) name_order.push_back(name);
    names[name] = key;
    ctx.include.push_back(key);
    return id;
  };
  auto resolve_name = [&](std::string_view raw) { return participant(raw); };

  static const Regex kAmp("^&\\s*");
  static const Regex kHideFootbox("^hide\\s+footbox$", "i");
  static const Regex kParticipant(
      "^(participant|actor|boundary|control|entity|database|collections|queue)\\s+(" + kName + ")(?:\\s+as\\s+(" +
          kName + R"re())?(?:\s*<<[^>]*>>)*(?:\s+order\s+-?\d+)?(?:\s*#[\w#]+)?\s*$)re",
      "iu");
  static const Regex kIconParticipant(R"re(^\$?\w+\s*\(([^)]*)\)\s+as\s+([\w.]+))re");
  static const Regex kCreate(
      "^create\\s+(?:(participant|actor|boundary|control|entity|database|collections|queue)\\s+)?(.+)$", "i");
  static const Regex kActivate(R"re(^(activate|deactivate|destroy)\s+([\w.@$"-]+)(?:\s+.*)?$)re", "i");
  static const Regex kAutonumber("^autonumber\\b", "i");
  static const Regex kStop("stop", "i");
  static const Regex kReturn("^return\\b\\s*(.*)$", "i");
  static const Regex kFrame("^(alt|opt|loop|par|par2|break|critical|group)\\b\\s*(.*)$", "i");
  static const Regex kBrackets("^\\[|\\]$", "g");
  static const Regex kElse("^(else|also)\\b\\s*(.*)$", "i");
  static const Regex kEnd("^end$", "i");
  static const Regex kNote(
      R"re(^(?:[hr]?note)\s+(left|right|over|across)\s*(?:of\s+)?([^:]*?)\s*(?:#[\w#]+)?\s*(?::\s*(.*))?$)re", "i");
  static const Regex kEndNote("^end\\s*[hr]?note$", "i");
  static const Regex kRef(R"re(^ref\s+over\s+([^:]+?)\s*(?::\s*(.*))?$)re", "i");
  static const Regex kEndRef("^end\\s*ref$", "i");
  static const Regex kDivider("^==\\s*(.*?)\\s*==$");
  static const Regex kDelay("^\\.\\.\\.\\s*(.*?)\\s*(?:\\.\\.\\.)?$");
  static const Regex kSpace("^\\|\\|\\d*\\|*$");
  static const Regex kBox("^box\\b", "i");
  static const Regex kEndBox("^end\\s*box$", "i");
  static const Regex kMessage(
      "^(\\[|" + kName +
          R"re()?\s*((?:\\\\|\/\/)?<?<?[ox]?(?:\\\\|\/\/)?(?:-+|\.+)(?:\[[^\]]*\])?(?:-+)?\\?\\?/?/?(?:>>?|x|o|\]|\\\\|//)?[ox]?)\s*(\]|)re" +
          kName + R"re()?\s*(\+\+|--|\*\*|!!)?\s*(?:[^:]*?)?\s*(?::\s*(.*))?$)re",
      "u");
  static const Regex kDashOrDot("-|\\.");
  static const Regex kTrailingOx("[ox]$");
  static const Regex kEndsGt(">$");
  static const Regex kOptions("\\[[^\\]]*\\]");
  static const Regex kDotted("-{2,}|\\.");
  static const Regex kAsync(R"re(>>|\\\\|\/\/)re");
  static const Regex kAsyncLeft("^<<");
  static const Regex kStyling("^(newpage|skinparam|title)\\b", "i");

  struct OpenFrame {
    std::vector<SequenceItem>* parent;
    std::size_t index;  // where the frame goes in the parent list
    std::unique_ptr<SequenceFrame> frame;
  };
  std::vector<OpenFrame> frames;

  auto close_frame = [&] {
    OpenFrame f = std::move(frames.back());
    frames.pop_back();
    stack.pop_back();
    (*f.parent)[f.index] = SequenceItem(std::move(*f.frame));
  };

  const auto& lines = ctx.lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const int line = lines[i].line;
    // Teoz: `& A -> B` runs in parallel with the previous message.
    std::string t = kAmp.replace(trim(lines[i].text), "", false);
    if (kHideFootbox.test(t)) {
      ctx.style(line, js::to_lower(first_word(t)));
      continue;
    }
    std::size_t used = common(ctx, lines, i);
    if (used > 0) {
      i += used - 1;
      continue;
    }

    if (auto m = kParticipant.exec(t)) {
      std::string a = m->str(2);
      std::optional<std::string> alias = m->get(3);
      // `Long as L`, `"Long" as L` or `L as "Long"`: the quoted or first part is the label.
      std::string name, label;
      if (alias) {
        if (!alias->empty() && alias->front() == '"' && (a.empty() || a.front() != '"')) {
          name = a;
          label = unquote(*alias);
        } else {
          name = *alias;
          label = unquote(a);
        }
      } else {
        name = a;
        label = unquote(a);
      }
      participant(name, js::to_lower(m->view(1)), label);
      continue;
    }
    // Icon-library macros used as participants: `$AWSIcon(Lambda, "Handler", "AWS Lambda") as fn`.
    if (auto m = kIconParticipant.exec(t)) {
      std::vector<std::string> parts;
      for (const auto& x : split(m->view(1), ',')) parts.push_back(unquote(trim(x)));
      std::string label = parts.size() > 1 && !parts[1].empty() ? parts[1] : !parts[0].empty() ? parts[0] : m->str(2);
      participant(m->str(2), "participant", plain_label(label));
      continue;
    }
    if (auto m = kCreate.exec(t)) {
      participant(trim(m->view(2)), m->has(1) ? js::to_lower(m->view(1)) : "participant");
      continue;
    }
    if (auto m = kActivate.exec(t)) {
      ElementId id = resolve_name(m->view(2));
      items().push_back(SequenceActivation{js::to_lower(m->view(1)) == "activate", id});
      continue;
    }
    if (kAutonumber.test(t)) {
      if (!kStop.test(t)) ctx.autonumber = true;
      continue;
    }
    if (auto m = kReturn.exec(t)) {
      std::optional<std::string> from, to;
      if (!callers.empty()) {
        from = callers.back();
        callers.pop_back();
      }
      if (!callers.empty()) {
        to = callers.back();
        callers.pop_back();
      }
      if (from && to) {
        SequenceMessage msg{*from, *to, std::nullopt, MessageStyle::Reply};
        if (!m->view(1).empty()) msg.label = plain_label(m->view(1));
        items().push_back(msg);
        items().push_back(SequenceActivation{false, *from});
      } else {
        ctx.drop(line, "return");
      }
      continue;
    }
    if (auto m = kFrame.exec(t)) {
      std::string kind = js::to_lower(m->view(1));
      FrameKind frame = kind == "alt"        ? FrameKind::Alt
                        : kind == "opt"      ? FrameKind::Opt
                        : kind == "loop"     ? FrameKind::Loop
                        : kind == "par" || kind == "par2" ? FrameKind::Par
                        : kind == "break"    ? FrameKind::Break
                        : kind == "critical" ? FrameKind::Critical
                                             : FrameKind::Group;
      FrameSection section;
      if (!m->view(2).empty()) {
        std::string label = plain_label(kBrackets.replace(m->view(2), "", true));
        if (!label.empty()) section.label = label;
      }
      auto f = std::make_unique<SequenceFrame>(SequenceFrame{frame, {}});
      f->sections.push_back(std::move(section));
      std::vector<SequenceItem>* parent = &items();
      parent->push_back(SequenceFrame{frame, {}});  // a placeholder, filled when the frame ends
      std::size_t index = parent->size() - 1;
      SequenceFrame* raw = f.get();
      frames.push_back({parent, index, std::move(f)});
      stack.push_back({&raw->sections.back().items, raw});
      continue;
    }
    if (auto m = kElse.exec(t)) {
      Level& top = stack.back();
      if (!top.frame) {
        ctx.drop(line, "else");
        continue;
      }
      FrameSection section;
      if (!m->view(2).empty()) section.label = plain_label(m->view(2));
      top.frame->sections.push_back(std::move(section));
      top.items = &top.frame->sections.back().items;
      continue;
    }
    if (kEnd.test(t)) {
      if (stack.size() > 1) close_frame();
      else ctx.drop(line, "end");
      continue;
    }
    if (auto m = kNote.exec(t)) {
      std::optional<std::string> body = m->get(3);
      if (!body) {
        std::vector<std::string> rest;
        while (++i < lines.size() && !kEndNote.test(trim(lines[i].text))) rest.push_back(trim(lines[i].text));
        body = join(rest, "\n");
      }
      std::vector<std::string> targets;
      for (const auto& x : split(m->view(2), ','))
        if (!js_trim(x).empty()) targets.push_back(trim(x));
      if (targets.empty() && !name_order.empty()) targets.push_back(name_order.front());
      std::vector<ElementId> over;
      for (const auto& x : targets) over.push_back(resolve_name(x));
      if (over.empty()) continue;
      std::string side = js::to_lower(m->view(1));
      NotePosition position = side == "left" ? NotePosition::Left : side == "right" ? NotePosition::Right : NotePosition::Over;
      items().push_back(SequenceNote{position, over, plain_label(*body)});
      continue;
    }
    if (auto m = kRef.exec(t)) {
      std::optional<std::string> body = m->get(2);
      if (!body) {
        std::vector<std::string> rest;
        while (++i < lines.size() && !kEndRef.test(trim(lines[i].text))) rest.push_back(trim(lines[i].text));
        body = join(rest, "\n");
      }
      std::vector<ElementId> over;
      for (const auto& x : split(m->view(1), ',')) over.push_back(resolve_name(trim(x)));
      items().push_back(SequenceRef{over, plain_label(*body)});
      continue;
    }
    if (auto m = kDivider.exec(t)) {
      SequenceDivider d;
      if (!m->view(1).empty()) d.label = plain_label(m->view(1));
      items().push_back(d);
      continue;
    }
    if (auto m = kDelay.exec(t)) {
      SequenceDelay d;
      if (!m->view(1).empty()) d.label = plain_label(m->view(1));
      items().push_back(d);
      continue;
    }
    if (kSpace.test(t)) continue;
    if (kBox.test(t) || kEndBox.test(t)) {
      ctx.style(line, "box");
      continue;
    }

    if (auto m = kMessage.exec(t); m && kDashOrDot.test(m->view(2))) {
      std::string arrow = m->str(2);
      std::optional<std::string> left_raw = m->get(1), right_raw = m->get(3);
      auto label = [&]() -> std::optional<std::string> {
        if (m->has(5) && !m->view(5).empty()) return plain_label(m->view(5));
        return std::nullopt;
      };
      if (!left_raw || left_raw->empty() || !right_raw || right_raw->empty() || *left_raw == "[" || *right_raw == "]") {
        // Found or lost messages: only one participant.
        std::optional<std::string> p;
        if (left_raw && !left_raw->empty() && *left_raw != "[") p = left_raw;
        else if (right_raw && !right_raw->empty() && *right_raw != "]") p = right_raw;
        if (!p) {
          ctx.drop(line, "message", t);
          continue;
        }
        ElementId id = resolve_name(*p);
        items().push_back(SequenceMessage{id, id, label(), MessageStyle::Async});
        continue;
      }
      bool reversed = arrow.front() == '<' && !kEndsGt.test(kTrailingOx.replace(arrow, "", false));
      ElementId a = resolve_name(*left_raw);
      ElementId c = resolve_name(*right_raw);
      ElementId from = reversed ? c : a;
      ElementId to = reversed ? a : c;
      bool dotted = kDotted.test(kOptions.replace(arrow, "", false));
      bool async_head = kAsync.test(arrow) || kAsyncLeft.test(arrow);
      MessageStyle style = dotted ? MessageStyle::Reply : async_head ? MessageStyle::Async : MessageStyle::Sync;
      items().push_back(SequenceMessage{from, to, label(), style});
      std::string activation = m->str(4);
      if (activation == "++") {
        items().push_back(SequenceActivation{true, to});
        callers.push_back(from);
        callers.push_back(to);
      } else if (activation == "--") {
        items().push_back(SequenceActivation{false, from});
      } else if (style != MessageStyle::Reply) {
        callers.push_back(from);
        callers.push_back(to);
      }
      continue;
    }
    if (kStyling.test(t)) {
      ctx.style(line, js::to_lower(first_word(t)));
      continue;
    }
    ctx.drop(line, unsupported_construct(t), t);
  }
  // Frames still open at the end keep what they hold.
  while (!frames.empty()) close_frame();
  ctx.sequence = std::move(*root);
}

}  // namespace dd::io::plantuml
