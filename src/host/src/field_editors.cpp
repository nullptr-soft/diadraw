#include "host/field_editors.hpp"

#include <algorithm>
#include <set>

#include "diadraw/render/svg.hpp"

#include "ascii.hpp"

namespace dd::host {

namespace {

using detail::trim_ascii;

std::vector<Choice> plain(const std::vector<std::string>& values) {
  std::vector<Choice> out;
  for (const auto& v : values) out.push_back({v, v, ""});
  return out;
}

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const std::vector<Choice>& arrowheads() {
  // draw.io's names, so a file it wrote shows its ends here; the group sections the pop-up.
  static const std::vector<Choice> all{
      {"", "Default", ""},
      {"none", "None", ""},
      {"classic", "Arrow", "Arrows"},
      {"block", "Triangle", "Arrows"},
      {"open", "Open arrow", "Arrows"},
      {"async", "Half arrow", "Arrows"},
      {"openAsync", "Open half arrow", "Arrows"},
      {"doubleBlock", "Double triangle", "Arrows"},
      {"diamond", "Diamond", "Shapes"},
      {"oval", "Circle", "Shapes"},
      {"box", "Square", "Shapes"},
      {"circlePlus", "Circle with plus", "Shapes"},
      {"halfCircle", "Socket", "Shapes"},
      {"baseDash", "Bar", "Lines"},
      {"dash", "Slash", "Lines"},
      {"cross", "Cross", "Lines"},
      {"ERone", "One", "Crow's foot"},
      {"ERmandOne", "Exactly one", "Crow's foot"},
      {"ERzeroToOne", "Zero or one", "Crow's foot"},
      {"ERmany", "Many", "Crow's foot"},
      {"ERoneToMany", "One or many", "Crow's foot"},
      {"ERzeroToMany", "Zero or many", "Crow's foot"},
  };
  return all;
}

// The Line style pop-up's value for the plain dashes: `dashed=1` without a pattern.
constexpr std::string_view kPlainDashes = "dashed";

// The values of the Line style pop-up: solid, the plain dashes, then the dash patterns.
const std::vector<Choice>& line_styles() {
  static const std::vector<Choice> all{
      {"", "Solid", ""},
      {std::string(kPlainDashes), "Dashed", ""},
      {"1 3", "Dotted", ""},
      {"3 3", "Short dashes", ""},
      {"12 6", "Long dashes", ""},
      {"8 3 1 3", "Dash and dot", ""},
      {"8 3 1 3 1 3", "Dash and two dots", ""},
  };
  return all;
}

bool is_line_style(const StyleKey& key) { return key.key == kLineStyleKey; }

}  // namespace

// ── Choices ──────────────────────────────────────────────────────────────────────────────────

const std::vector<std::string>& builtin_kinds() {
  // The order a picker lists them: the common C4 and flowchart kinds first.
  static const std::vector<std::string> kinds{
      "person",  "system",     "container", "component",  "database", "process", "decision", "start",
      "end",     "note",       "class",     "object",     "event",    "task",    "subprocess", "gateway",
      "data",    "annotation", "group",     "pool",       "lane",     "conversation"};
  return kinds;
}

const std::vector<std::string>& base_shapes() {
  static const std::vector<std::string> shapes{
      "rect",     "rounded",   "ellipse", "circle",  "rhombus",  "triangle", "hexagon",   "octagon",
      "cylinder", "parallelogram", "trapezoid", "document", "cloud", "actor", "person", "note",
      "card",     "step",      "arrow",   "process", "stadium",  "cube",     "swimlane",  "folder",
      "queue",    "interface", "text",    "image",   "invisible", "frame",  "module", "lifeline"};
  return shapes;
}

FieldChoices library_choices(const library::LibrarySet& libraries) {
  FieldChoices out;
  out.kinds = plain(builtin_kinds());
  out.shapes = plain(base_shapes());
  for (const auto& item : libraries.palette()) {
    Choice choice{item.ref, item.label.empty() ? item.id : item.label, item.alias};
    if (item.type == "kind")
      out.kinds.push_back(std::move(choice));
    else if (item.type == "shape" || item.type == "composite")
      out.shapes.push_back(std::move(choice));
  }
  for (const auto& name : dd::theme_names()) out.themes.push_back({name, name, ""});
  for (const auto& ref : libraries.theme_refs()) {
    auto dot = ref.find('.');
    out.themes.push_back({ref, ref, dot == std::string::npos ? "" : ref.substr(0, dot)});
  }
  return out;
}

void add_model_choices(FieldChoices& out, const Model& model) {
  std::set<std::string, std::less<>> tags(out.tags.begin(), out.tags.end());
  std::set<std::string, std::less<>> technologies(out.technologies.begin(), out.technologies.end());
  // Kinds used in the model that no picker entry has (a typo, or a library not resolved), so
  // the picker can still show what the element is.
  std::set<std::string, std::less<>> kinds;
  for (const auto& c : out.kinds) kinds.insert(c.value);
  for (const auto& e : model.elements) {
    tags.insert(e.tags.begin(), e.tags.end());
    if (e.technology && !e.technology->empty()) technologies.insert(*e.technology);
    if (!e.kind.empty() && kinds.insert(e.kind).second) out.kinds.push_back({e.kind, e.kind, "Used"});
  }
  for (const auto& r : model.relationships) {
    tags.insert(r.tags.begin(), r.tags.end());
    if (r.technology && !r.technology->empty()) technologies.insert(*r.technology);
  }
  out.tags.assign(tags.begin(), tags.end());
  out.technologies.assign(technologies.begin(), technologies.end());
  for (const auto& t : model.themes)
    if (std::none_of(out.themes.begin(), out.themes.end(), [&](const Choice& c) { return c.value == t.name; }))
      out.themes.push_back({t.name, t.name, "Declared"});
}

FieldChoices field_choices(const Model& model, const library::LibrarySet& libraries) {
  FieldChoices out = library_choices(libraries);
  add_model_choices(out, model);
  return out;
}

std::vector<std::string> split_tags(std::string_view text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find(',', start);
    if (end == std::string_view::npos) end = text.size();
    auto tag = trim_ascii(text.substr(start, end - start));
    if (!tag.empty() && std::find(out.begin(), out.end(), tag) == out.end()) out.emplace_back(tag);
    start = end + 1;
  }
  return out;
}

std::string join_tags(const std::vector<std::string>& tags) {
  std::string out;
  for (const auto& t : tags) {
    auto tag = trim_ascii(t);
    if (tag.empty()) continue;
    if (!out.empty()) out += ", ";
    out += tag;
  }
  return out;
}

// ── Properties ───────────────────────────────────────────────────────────────────────────────

std::vector<std::pair<std::string, std::string>> property_rows(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    auto line = trim_ascii(text.substr(start, end - start));
    start = end + 1;
    auto eq = line.find('=');
    std::string key(trim_ascii(line.substr(0, eq)));
    if (key.empty()) continue;
    std::string value(eq == std::string_view::npos ? std::string_view() : trim_ascii(line.substr(eq + 1)));
    auto at = std::find_if(out.begin(), out.end(), [&](const auto& r) { return r.first == key; });
    if (at != out.end())
      at->second = std::move(value);
    else
      out.emplace_back(std::move(key), std::move(value));
  }
  return out;
}

std::string property_text(const std::vector<std::pair<std::string, std::string>>& rows) {
  auto clean = [](std::string_view s, bool key) {
    std::string out(trim_ascii(s));
    for (char& c : out) {
      if (c == '\n' || c == '\r') c = ' ';
      if (key && c == '=') c = '_';
    }
    return out;
  };
  std::string out;
  for (const auto& [k, v] : rows) {
    std::string key = clean(k, true);
    if (key.empty()) continue;
    if (!out.empty()) out.push_back('\n');
    out += key + "=" + clean(v, false);
  }
  return out;
}

// ── Style blocks and themes ──────────────────────────────────────────────────────────────────

std::vector<BlockRow> block_rows(std::string_view text) {
  std::vector<BlockRow> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    auto line = trim_ascii(text.substr(start, end - start));
    start = end + 1;
    if (line.empty()) continue;
    // The first colon outside quotes: `tag "a:b": fill=#fff`.
    std::size_t colon = std::string_view::npos;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size() && colon == std::string_view::npos; ++i) {
      if (line[i] == '\\' && quoted)
        ++i;
      else if (line[i] == '"')
        quoted = !quoted;
      else if (line[i] == ':' && !quoted)
        colon = i;
    }
    if (colon == std::string_view::npos)
      out.push_back({std::string(line), ""});
    else
      out.push_back({std::string(trim_ascii(line.substr(0, colon))), std::string(trim_ascii(line.substr(colon + 1)))});
  }
  return out;
}

std::string block_text(const std::vector<BlockRow>& rows) {
  std::string out;
  for (const auto& r : rows) {
    auto selector = trim_ascii(r.selector);
    if (selector.empty()) continue;
    if (!out.empty()) out.push_back('\n');
    out += std::string(selector) + ": " + std::string(trim_ascii(r.style));
  }
  return out;
}

StyleTarget selector_target(std::string_view selector) {
  selector = trim_ascii(selector);
  bool rel = selector.substr(0, 12) == "relationship" &&
             (selector.size() == 12 || selector[12] == ' ' || selector[12] == '\t');
  return rel ? StyleTarget::Relationships : StyleTarget::Elements;
}

std::vector<std::string> selector_choices(const FieldChoices& choices) {
  std::vector<std::string> out{"*", "relationship"};
  for (const auto& k : choices.kinds) out.push_back(k.value);
  auto quoted = [](const std::string& tag) {
    std::string q = "\"";
    for (char c : tag) {
      if (c == '"' || c == '\\') q.push_back('\\');
      q.push_back(c);
    }
    return q + "\"";
  };
  for (const auto& t : choices.tags) out.push_back("tag " + quoted(t));
  for (const auto& t : choices.tags) out.push_back("relationship tag " + quoted(t));
  return out;
}

bool is_color_key(std::string_view key) {
  return key != "fontFamily" && key != "fontSize" && key != "smallFontSize";
}

// ── Style editor ─────────────────────────────────────────────────────────────────────────────

const std::vector<StyleKey>& style_keys(StyleTarget target) {
  using C = StyleControl;
  static const std::vector<StyleKey> elements{
      {"fill", "Fill", C::Color, {}, 0, 0, 1, "", "Fill and line"},
      {"stroke", "Line", C::Color, {}, 0, 0, 1, "", "Fill and line"},
      {"strokeWidth", "Line width", C::Number, {}, 0, 20, 0.5, "1", "Fill and line"},
      {std::string(kLineStyleKey), "Line style", C::Choice, line_styles(), 0, 0, 1, "", "Fill and line"},
      {"rounded", "Rounded", C::Flag, {}, 0, 0, 1, "0", "Fill and line"},
      {"opacity", "Opacity", C::Number, {}, 0, 100, 5, "100", "Fill and line"},
      {"fontColor", "Text colour", C::Color, {}, 0, 0, 1, "", "Text"},
      {"fontSize", "Size", C::Number, {}, 6, 96, 1, "12", "Text"},
      {"fontStyle", "Style", C::FontStyle, {}, 0, 0, 1, "0", "Text"},
      {"align", "Align", C::Choice, {{"", "Default", ""}, {"left", "Left", ""}, {"center", "Centre", ""}, {"right", "Right", ""}},
       0, 0, 1, "", "Text"},
      {"verticalAlign", "Vertical", C::Choice,
       {{"", "Default", ""}, {"top", "Top", ""}, {"middle", "Middle", ""}, {"bottom", "Bottom", ""}}, 0, 0, 1, "", "Text"},
      {"labelPosition", "Label beside", C::Choice,
       {{"", "Inside", ""}, {"left", "Left", ""}, {"right", "Right", ""}}, 0, 0, 1, "", "Text"},
      {"verticalLabelPosition", "Label above/below", C::Choice,
       {{"", "Inside", ""}, {"top", "Above", ""}, {"bottom", "Below", ""}}, 0, 0, 1, "", "Text"},
  };
  static const std::vector<StyleKey> relationships{
      {"stroke", "Line", C::Color, {}, 0, 0, 1, "", "Line"},
      {"strokeWidth", "Line width", C::Number, {}, 0, 20, 0.5, "1.25", "Line"},
      {std::string(kLineStyleKey), "Line style", C::Choice, line_styles(), 0, 0, 1, "", "Line"},
      {"double", "Double line", C::Flag, {}, 0, 0, 1, "0", "Line"},
      {"edgeStyle", "Routing", C::Choice,
       {{"", "Default", ""}, {"orthogonalEdgeStyle", "Right angles", ""}}, 0, 0, 1, "", "Line"},
      {"curved", "Curved", C::Flag, {}, 0, 0, 1, "0", "Line"},
      {"rounded", "Rounded corners", C::Flag, {}, 0, 0, 1, "0", "Line"},
      {"startArrow", "Start", C::Choice, arrowheads(), 0, 0, 1, "", "Arrows"},
      {"startFill", "Filled start", C::Flag, {}, 0, 0, 1, "1", "Arrows"},
      {"endArrow", "End", C::Choice, arrowheads(), 0, 0, 1, "", "Arrows"},
      {"endFill", "Filled end", C::Flag, {}, 0, 0, 1, "1", "Arrows"},
      {"fontColor", "Text colour", C::Color, {}, 0, 0, 1, "", "Label"},
      {"fontSize", "Size", C::Number, {}, 6, 96, 1, "11", "Label"},
  };
  return target == StyleTarget::Relationships ? relationships : elements;
}

std::vector<std::pair<std::string, std::string>> style_entries(std::string_view style) {
  // As dsl::parse_style_text reads it: `;` separates, the first `=` splits, a later repeat wins.
  std::vector<std::pair<std::string, std::string>> out;
  std::size_t start = 0;
  while (start <= style.size()) {
    auto end = style.find(';', start);
    if (end == std::string_view::npos) end = style.size();
    auto part = style.substr(start, end - start);
    if (!part.empty()) {
      auto eq = part.find('=');
      std::string key(eq == std::string_view::npos ? part : part.substr(0, eq));
      std::string value(eq == std::string_view::npos ? std::string_view() : part.substr(eq + 1));
      auto at = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.first == key; });
      if (at != out.end())
        at->second = std::move(value);
      else
        out.emplace_back(std::move(key), std::move(value));
    }
    start = end + 1;
  }
  return out;
}

std::string style_text(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::string out;
  for (const auto& [k, v] : entries) {
    if (!out.empty()) out.push_back(';');
    out += v.empty() ? k : k + "=" + v;
  }
  return out;
}

std::optional<std::string> style_value(std::string_view style, std::string_view key) {
  for (auto& [k, v] : style_entries(style))
    if (k == key) return v;
  return std::nullopt;
}

std::string with_style_value(std::string_view style, std::string_view key, const std::optional<std::string>& value) {
  auto entries = style_entries(style);
  auto at = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == key; });
  if (!value) {
    if (at == entries.end()) return std::string(style);
    entries.erase(at);
  } else if (at != entries.end()) {
    if (at->second == *value) return std::string(style);
    at->second = *value;
  } else {
    entries.emplace_back(std::string(key), *value);
  }
  return style_text(entries);
}

std::optional<std::string> style_control_value(std::string_view style, const StyleKey& key) {
  if (!is_line_style(key)) return style_value(style, key.key);
  // One pass over the text: the pop-up reads two keys.
  std::optional<std::string> dashed, pattern;
  for (auto& [k, v] : style_entries(style)) {
    if (k == "dashed")
      dashed = std::move(v);
    else if (k == "dashPattern")
      pattern = std::move(v);
  }
  if (dashed != "1") return std::nullopt;
  return pattern && !pattern->empty() ? std::move(*pattern) : std::string(kPlainDashes);
}

bool style_key_covers(const StyleKey& key, std::string_view style_key) {
  if (is_line_style(key)) return style_key == "dashed" || style_key == "dashPattern";
  return key.key == style_key;
}

std::optional<std::string> style_setting(const StyleKey& key, std::string_view value) {
  switch (key.control) {
    case StyleControl::Choice:
      if (value.empty() || (!key.choices.empty() && value == key.choices.front().value)) return std::nullopt;
      break;
    case StyleControl::Flag:
    case StyleControl::FontStyle:
      if (value == key.fallback) return std::nullopt;
      break;
    case StyleControl::Color:
    case StyleControl::Number:
      if (value.empty()) return std::nullopt;
      break;
  }
  return std::string(value);
}

std::string with_style_setting(std::string_view style, const StyleKey& key, std::string_view value) {
  if (!is_line_style(key)) return with_style_value(style, key.key, style_setting(key, value));
  // Solid removes both keys; the plain dashes keep no pattern; a pattern turns dashing on.
  bool dashed = !value.empty();
  bool patterned = dashed && value != kPlainDashes;
  std::string out = with_style_value(style, "dashed", dashed ? std::optional<std::string>("1") : std::nullopt);
  return with_style_value(out, "dashPattern", patterned ? std::optional<std::string>(value) : std::nullopt);
}

std::optional<Style> style_sample(const StyleKey& key, std::string_view value) {
  std::string v(value);
  if (key.key == "endArrow") return Style{{"endArrow", v.empty() ? "classic" : v}};
  if (key.key == "startArrow") return Style{{"startArrow", v.empty() ? "none" : v}, {"endArrow", "none"}};
  if (is_line_style(key)) {
    Style style{{"endArrow", "none"}};
    if (!v.empty()) style.set("dashed", "1");
    if (!v.empty() && v != kPlainDashes) style.set("dashPattern", v);
    return style;
  }
  return std::nullopt;
}

const std::optional<draw::DisplayList>& LinePreview::list(std::string_view ink, std::string_view surface,
                                                         double width, double height) {
  if (drawn_ && ink == ink_ && surface == surface_ && width == width_ && height == height_) return list_;
  Theme theme = default_theme();
  theme.surface = surface;
  theme.background = surface;
  Style inked = sample_;
  inked.set("stroke", std::string(ink));
  inked.set("strokeWidth", "1.25");
  list_ = draw::display_list_from_svg(line_sample_svg(inked, width, height, theme));
  drawn_ = true;
  ink_ = ink;
  surface_ = surface;
  width_ = width;
  height_ = height;
  return list_;
}

std::string other_style(std::string_view style, StyleTarget target) {
  const auto& keys = style_keys(target);
  std::vector<std::pair<std::string, std::string>> other;
  for (auto& entry : style_entries(style))
    if (std::none_of(keys.begin(), keys.end(), [&](const StyleKey& k) { return style_key_covers(k, entry.first); }))
      other.push_back(std::move(entry));
  return style_text(other);
}

std::string with_other_style(std::string_view style, StyleTarget target, std::string_view other) {
  const auto& keys = style_keys(target);
  auto known = [&](const std::string& key) {
    return std::any_of(keys.begin(), keys.end(), [&](const StyleKey& k) { return style_key_covers(k, key); });
  };
  // Typed text: spaces around keys and values are not meant.
  std::vector<std::pair<std::string, std::string>> typed;
  for (auto& [k, v] : style_entries(other)) {
    std::string key(trim_ascii(k));
    if (!key.empty()) typed.emplace_back(std::move(key), std::string(trim_ascii(v)));
  }
  std::vector<std::pair<std::string, std::string>> out;
  // The entries with a control keep their place unless Other sets them too.
  for (auto& entry : style_entries(style)) {
    if (!known(entry.first)) continue;
    auto t = std::find_if(typed.begin(), typed.end(), [&](const auto& e) { return e.first == entry.first; });
    if (t != typed.end()) {
      entry.second = t->second;
      typed.erase(t);
    }
    out.push_back(std::move(entry));
  }
  for (auto& entry : typed) {
    auto t = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.first == entry.first; });
    if (t != out.end())
      t->second = std::move(entry.second);
    else
      out.push_back(std::move(entry));
  }
  // Nothing changed: keep the text as written, entry order included.
  auto before = style_entries(style);
  auto after = out;
  std::sort(before.begin(), before.end());
  std::sort(after.begin(), after.end());
  return before == after ? std::string(style) : style_text(out);
}

std::optional<std::string> normalized_color(std::string_view value) {
  value = trim_ascii(value);
  if (value.size() != 4 && value.size() != 7) return std::nullopt;
  if (value.front() != '#') return std::nullopt;
  std::string out = "#";
  for (std::size_t i = 1; i < value.size(); ++i) {
    int h = hex(value[i]);
    if (h < 0) return std::nullopt;
    char c = "0123456789abcdef"[h];
    out.push_back(c);
    if (value.size() == 4) out.push_back(c);
  }
  return out;
}

int font_style_bits(std::string_view value) {
  value = trim_ascii(value);
  if (value.empty()) return 0;
  int bits = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return 0;
    bits = bits * 10 + (c - '0');
    if (bits > 1 << 20) return 0;
  }
  return bits;
}

}  // namespace dd::host
