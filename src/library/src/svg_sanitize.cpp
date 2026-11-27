// Library drawings: SVG markup made safe to inline into the canvas, exported SVG and the web
// palette. A small tokenizer reads the markup into a tree of allowed elements and attributes, and
// the tree is written back out in one canonical form: every attribute double-quoted and escaped,
// every text run escaped, no comments, processing instructions, doctypes or CDATA. What the
// browser parses is that output, so quirks of the input (unquoted or valueless attributes,
// stray `<`, odd entities) cannot reach it.
//
// Allowlists, not blocklists: an element or attribute that is not listed below is dropped. That
// keeps out scripts, event handlers, foreignObject, animation (`<set attributeName="href">`),
// styles and anything a later SVG version adds. URLs survive only as same-document fragments
// (`#id`, `url(#id)`) and as embedded raster images on `<image>`.

#include <algorithm>
#include <iterator>
#include <list>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "diadraw/base/text.hpp"
#include "diadraw/library/library.hpp"
#include "svg_sanitize.hpp"

namespace dd::library {

namespace {

/// Deeper elements are dropped with their content; library art is never this deep, and the
/// writer recurses once per level.
constexpr std::size_t kMaxDepth = 64;
/// Attributes past this many on one tag are dropped.
constexpr std::size_t kMaxAttributes = 256;

// Shapes, structure, gradients, patterns, clipping, masking, markers, text and filters.
// Missing on purpose: script, style, foreignObject, a (unwrapped instead), animate*, set,
// feImage (fetches), cursor, view, and every HTML or namespaced name.
constexpr std::string_view kElements[] = {
    "svg",            "g",                 "defs",              "symbol",         "use",
    "switch",         "title",             "desc",              "path",           "rect",
    "circle",         "ellipse",           "line",              "polyline",       "polygon",
    "text",           "tspan",             "textPath",          "image",          "linearGradient",
    "radialGradient", "stop",              "pattern",           "clipPath",       "mask",
    "marker",         "filter",            "feBlend",           "feColorMatrix",  "feComponentTransfer",
    "feComposite",    "feConvolveMatrix",  "feDiffuseLighting", "feDisplacementMap", "feDistantLight",
    "feDropShadow",   "feFlood",           "feFuncA",           "feFuncB",        "feFuncG",
    "feFuncR",        "feGaussianBlur",    "feMerge",           "feMergeNode",    "feMorphology",
    "feOffset",       "fePointLight",      "feSpecularLighting", "feSpotLight",   "feTile",
    "feTurbulence",
};
// Unknown to the allowlist but harmless as a wrapper: the element goes, its content stays.
constexpr std::string_view kUnwrapped[] = {"a"};
// Their content is text only: in an HTML page `title` and `desc` are integration points where
// child markup is read as HTML.
constexpr std::string_view kTextOnly[] = {"title", "desc"};

// Presentation properties: allowed as attributes and as declarations in `style`.
constexpr std::string_view kPresentation[] = {
    "alignment-baseline", "baseline-shift", "clip-path", "clip-rule", "color", "color-interpolation",
    "color-interpolation-filters", "direction", "display", "dominant-baseline", "fill", "fill-opacity",
    "fill-rule", "filter", "flood-color", "flood-opacity", "font", "font-family", "font-size",
    "font-size-adjust", "font-stretch", "font-style", "font-variant", "font-weight", "image-rendering",
    "isolation", "letter-spacing", "lighting-color", "marker", "marker-end", "marker-mid", "marker-start",
    "mask", "mix-blend-mode", "opacity", "overflow", "paint-order", "shape-rendering", "stop-color",
    "stop-opacity", "stroke", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap", "stroke-linejoin",
    "stroke-miterlimit", "stroke-opacity", "stroke-width", "text-anchor", "text-decoration",
    "text-rendering", "transform", "transform-origin", "unicode-bidi", "vector-effect", "visibility",
    "word-spacing", "writing-mode",
};
// Geometry and the attributes of gradients, patterns, markers, text and filter primitives.
constexpr std::string_view kAttributes[] = {
    "id", "x", "y", "x1", "y1", "x2", "y2", "cx", "cy", "r", "rx", "ry", "fx", "fy", "fr", "width",
    "height", "d", "points", "pathLength", "viewBox", "preserveAspectRatio", "dx", "dy", "rotate",
    "textLength", "lengthAdjust", "startOffset", "method", "spacing", "side", "xml:space",
    "gradientUnits", "gradientTransform", "spreadMethod", "offset", "patternUnits", "patternContentUnits",
    "patternTransform", "clipPathUnits", "maskUnits", "maskContentUnits", "markerWidth", "markerHeight",
    "markerUnits", "refX", "refY", "orient", "filterUnits", "primitiveUnits", "in", "in2", "result",
    "stdDeviation", "mode", "operator", "k1", "k2", "k3", "k4", "values", "type", "tableValues", "slope",
    "intercept", "amplitude", "exponent", "radius", "scale", "xChannelSelector", "yChannelSelector",
    "baseFrequency", "numOctaves", "seed", "stitchTiles", "surfaceScale", "specularConstant",
    "specularExponent", "diffuseConstant", "kernelMatrix", "order", "divisor", "bias", "targetX",
    "targetY", "edgeMode", "preserveAlpha", "kernelUnitLength",
};
// Light sources, kept apart so the table above stays readable.
constexpr std::string_view kLightAttributes[] = {
    "azimuth", "elevation", "z", "pointsAtX", "pointsAtY", "pointsAtZ", "limitingConeAngle",
};

template <std::size_t N>
bool listed(const std::string_view (&list)[N], std::string_view name) {
  return std::find(std::begin(list), std::end(list), name) != std::end(list);
}

bool allowed_attribute(std::string_view name) {
  return listed(kPresentation, name) || listed(kAttributes, name) || listed(kLightAttributes, name) ||
         name == "style" || name == "href" || name == "xlink:href";
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool starts_with_nocase(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i)
    if (lower(s[i]) != prefix[i]) return false;
  return true;
}

std::string_view trim_space(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

/// Characters XML forbids, and that no drawing needs, are left out: C0 controls but tab, line
/// feed and carriage return.
void append_clean(std::string& out, std::string_view s) {
  for (char c : s)
    if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' || c == '\n' || c == '\r') out += c;
}

/// The five predefined entities and character references; anything else stays as written (and is
/// escaped on output, so `&foo;` reads as text, never as a reference into a DTD).
std::string decode_entities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '&') {
      auto next = s.find('&', i);
      if (next == std::string_view::npos) next = s.size();
      append_clean(out, s.substr(i, next - i));
      i = next;
      continue;
    }
    // A reference is at most 12 characters: look no further for its `;`.
    auto semi = s.substr(i + 1, 12).find(';');
    if (semi == std::string_view::npos) {
      out += '&';
      ++i;
      continue;
    }
    semi += i + 1;
    std::string_view name = s.substr(i + 1, semi - i - 1);
    std::uint32_t cp = 0;
    bool known = true;
    if (name == "lt") cp = '<';
    else if (name == "gt") cp = '>';
    else if (name == "amp") cp = '&';
    else if (name == "quot") cp = '"';
    else if (name == "apos") cp = '\'';
    else if (name.size() > 1 && name[0] == '#') {
      bool hex = name[1] == 'x' || name[1] == 'X';
      std::string_view digits = name.substr(hex ? 2 : 1);
      known = !digits.empty();
      for (char c : digits) {
        int d = c >= '0' && c <= '9'   ? c - '0'
                : hex && lower(c) >= 'a' && lower(c) <= 'f' ? lower(c) - 'a' + 10
                                                             : -1;
        if (d < 0) {
          known = false;
          break;
        }
        cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(d);
        if (cp > 0x10FFFF) cp = 0x110000;  // saturate; rejected below
      }
      if (known && (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || cp == 0xFFFE || cp == 0xFFFF)) cp = 0xFFFD;
      if (known && cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') cp = 0xFFFD;
    } else {
      known = false;
    }
    if (!known) {
      out += '&';
      ++i;
      continue;
    }
    append_utf8(out, cp);
    i = semi + 1;
  }
  return out;
}

void escape_into(std::string& out, std::string_view s, bool attribute) {
  for (char c : s) switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"':
        if (attribute) out += "&quot;";
        else out += c;
        break;
      default: out += c;
    }
}

struct Node {
  bool text = false;
  std::string name;  ///< the element name, or the text of a text node
  SvgAttributes attrs;
  std::list<Node> children;  ///< a list: open elements are held by pointer while siblings are added
};

struct Parsed {
  SvgAttributes root_attrs;
  Node root;
  bool found = false;
};

bool is_tag_end(char c) { return is_space(c) || c == '/' || c == '>'; }

struct Tag {
  std::string name;
  SvgAttributes attrs;
  bool closing = false;
  bool self_closing = false;
};

/// A start or end tag at `s[i] == '<'`, read the way an HTML tokenizer would (quoted, unquoted
/// and valueless attributes; the first of a repeated name wins). Returns the position after `>`,
/// or npos when the tag never ends.
std::size_t read_tag(std::string_view s, std::size_t i, Tag& tag) {
  ++i;
  if (i < s.size() && s[i] == '/') {
    tag.closing = true;
    ++i;
  }
  std::size_t start = i;
  while (i < s.size() && !is_tag_end(s[i])) ++i;
  tag.name = std::string(s.substr(start, i - start));
  while (i < s.size()) {
    while (i < s.size() && (is_space(s[i]) || s[i] == '/')) {
      tag.self_closing = s[i] == '/';
      ++i;
    }
    if (i >= s.size()) break;
    if (s[i] == '>') return i + 1;
    tag.self_closing = false;
    std::size_t name_start = i;
    ++i;  // a name may start with `=`, as in HTML
    while (i < s.size() && !is_tag_end(s[i]) && s[i] != '=') ++i;
    std::string name(s.substr(name_start, i - name_start));
    std::size_t j = i;
    while (j < s.size() && is_space(s[j])) ++j;
    std::string value;
    if (j < s.size() && s[j] == '=') {
      ++j;
      while (j < s.size() && is_space(s[j])) ++j;
      if (j < s.size() && (s[j] == '"' || s[j] == '\'')) {
        auto close = s.find(s[j], j + 1);
        if (close == std::string_view::npos) return std::string_view::npos;
        value = decode_entities(s.substr(j + 1, close - j - 1));
        i = close + 1;
      } else {
        std::size_t v = j;
        while (j < s.size() && !is_space(s[j]) && s[j] != '>') ++j;
        value = decode_entities(s.substr(v, j - v));
        i = j;
      }
    }
    // Past the cap, attributes are dropped: no drawing needs that many, and the first-wins check
    // stays bounded.
    bool seen = tag.attrs.size() >= kMaxAttributes ||
                std::any_of(tag.attrs.begin(), tag.attrs.end(), [&](const auto& a) { return a.first == name; });
    if (!seen) tag.attrs.emplace_back(std::move(name), std::move(value));
  }
  return std::string_view::npos;
}

bool starts_at(std::string_view s, std::size_t at, std::string_view prefix) {
  return s.size() - at >= prefix.size() && s.compare(at, prefix.size(), prefix) == 0;
}

/// Adds text to `node`, joined to the text before it so a run of stray `<` stays one node.
void add_text(Node* node, std::string_view text) {
  if (!node || text.empty()) return;
  if (node->children.empty() || !node->children.back().text) node->children.push_back({true, {}, {}, {}});
  node->children.back().name += text;
}

/// Reads the first `<svg>` element into a tree of allowed elements. Attributes are kept as
/// written here and checked when written out. With `inside_root`, `s` is the content of an
/// `<svg>` already open.
Parsed parse(std::string_view s, bool inside_root = false) {
  Parsed out;
  out.root.name = "svg";
  // One frame per open element. `node` is where children go: null while dropping a subtree, the
  // parent's node for an unwrapped element.
  struct Frame {
    std::string name;
    Node* node;
  };
  std::vector<Frame> stack;
  // Open frames by name, so an end tag that nothing opened is skipped without a scan.
  std::unordered_map<std::string, std::size_t> open;
  auto push = [&](std::string name, Node* node) {
    ++open[name];
    stack.push_back({std::move(name), node});
  };
  if (inside_root) {
    out.found = true;
    push("svg", &out.root);
  }
  std::size_t i = 0;
  while (i < s.size()) {
    Node* top = stack.empty() ? nullptr : stack.back().node;
    if (s[i] != '<') {
      auto next = s.find('<', i);
      if (next == std::string_view::npos) next = s.size();
      if (top) add_text(top, decode_entities(s.substr(i, next - i)));
      i = next;
      continue;
    }
    auto skip_to = [&](std::string_view end, std::size_t from) {
      auto at = s.find(end, from);
      return at == std::string_view::npos ? s.size() : at + end.size();
    };
    if (starts_at(s, i, "<!--")) {
      i = skip_to("-->", i + 4);
      continue;
    }
    if (starts_at(s, i, "<![CDATA[")) {
      auto end = s.find("]]>", i + 9);
      std::string_view body = s.substr(i + 9, end == std::string_view::npos ? std::string_view::npos : end - i - 9);
      if (top) {
        std::string text;
        append_clean(text, body);
        add_text(top, text);
      }
      i = end == std::string_view::npos ? s.size() : end + 3;
      continue;
    }
    if (starts_at(s, i, "<!")) {  // a doctype, with any internal subset: skipped, never expanded
      int depth = 0;
      std::size_t j = i + 2;
      char quote = 0;
      for (; j < s.size(); ++j) {
        char c = s[j];
        if (quote) {
          if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') quote = c;
        else if (c == '[') ++depth;
        else if (c == ']') depth = std::max(0, depth - 1);
        else if (c == '>' && depth == 0) break;
      }
      i = j < s.size() ? j + 1 : s.size();
      continue;
    }
    if (starts_at(s, i, "<?")) {
      i = skip_to("?>", i + 2);
      continue;
    }
    bool tag_start = i + 1 < s.size() && (s[i + 1] == '/' || (lower(s[i + 1]) >= 'a' && lower(s[i + 1]) <= 'z'));
    if (!tag_start) {  // a stray `<` is text
      add_text(top, "<");
      ++i;
      continue;
    }
    Tag tag;
    std::size_t end = read_tag(s, i, tag);
    if (end == std::string_view::npos) break;  // an unfinished tag ends the drawing
    i = end;
    if (tag.closing) {
      // Close the nearest open element of that name; an end tag nothing opened is ignored. The
      // scan only passes frames it then closes, so it is linear over the whole drawing.
      if (auto c = open.find(tag.name); c == open.end() || c->second == 0) continue;
      auto at = std::find_if(stack.rbegin(), stack.rend(), [&](const Frame& f) { return f.name == tag.name; });
      auto first = std::prev(at.base());
      for (auto f = first; f != stack.end(); ++f) --open[f->name];
      stack.erase(first, stack.end());
      if (stack.empty()) break;  // the drawing's `</svg>`
      continue;
    }
    if (stack.empty()) {
      if (tag.name.size() != 3 || !starts_with_nocase(tag.name, "svg")) continue;  // anything before the drawing
      out.found = true;
      out.root_attrs = std::move(tag.attrs);
      if (tag.self_closing) break;
      push("svg", &out.root);
      continue;
    }
    Node* child = nullptr;
    bool text_only = top && listed(kTextOnly, top->name);
    if (top && !text_only && stack.size() < kMaxDepth) {
      if (listed(kElements, tag.name)) {
        top->children.push_back({false, tag.name, std::move(tag.attrs), {}});
        child = &top->children.back();
      } else if (listed(kUnwrapped, tag.name)) {
        child = top;
      }
    }
    if (!tag.self_closing) push(std::move(tag.name), child);
  }
  return out;
}

/// `url(…)` references in a value: same-document ones are renamed with the ids, the rest become
/// `none`. Nullopt when the value cannot be read safely (CSS escapes could spell a URL).
std::optional<std::string> rewrite_urls(std::string_view value, const std::unordered_set<std::string>& ids,
                                        std::string_view prefix) {
  if (value.find('\\') != std::string_view::npos) return std::nullopt;
  std::string out;
  std::size_t i = 0;
  while (i < value.size()) {
    std::size_t at = std::string_view::npos;
    for (std::size_t j = i; j + 4 <= value.size(); ++j)
      if (starts_with_nocase(value.substr(j), "url(")) {
        at = j;
        break;
      }
    if (at == std::string_view::npos) {
      out += value.substr(i);
      break;
    }
    out += value.substr(i, at - i);
    auto close = value.find(')', at + 4);
    if (close == std::string_view::npos) return std::nullopt;
    std::string_view target = trim_space(value.substr(at + 4, close - at - 4));
    if (target.size() >= 2 && (target.front() == '"' || target.front() == '\'') && target.back() == target.front())
      target = trim_space(target.substr(1, target.size() - 2));
    if (!target.empty() && target.front() == '#' &&
        target.find_first_of("\"'() \t\n\r\f") == std::string_view::npos) {
      std::string id(target.substr(1));
      bool own = ids.count(id) > 0;
      if (!prefix.empty() && !own) {
        out += "none";  // with prefixed ids, anything else would reach the page's own elements
      } else {
        out += "url(#";
        if (own) out += prefix;
        out += id;
        out += ')';
      }
    } else {
      out += "none";
    }
    i = close + 1;
  }
  return out;
}

/// Outside `url()`, a quoted CSS string is a URL in image functions (`image-set`, `image`,
/// `cross-fade`, `src`), so only font families may hold one.
bool may_quote(std::string_view property, std::string_view value) {
  return property == "font-family" || property == "font" || value.find_first_of("\"'") == std::string_view::npos;
}

/// A declaration the browser reads as we do: no comment and no open bracket that would carry it
/// past our `;` into the next declaration (and that one's quoted string).
bool self_contained(std::string_view v) {
  if (v.find("/*") != std::string_view::npos || v.find("*/") != std::string_view::npos) return false;
  int depth = 0;
  for (char c : v) {
    if (c == '(' || c == '[' || c == '{') ++depth;
    else if ((c == ')' || c == ']' || c == '}') && --depth < 0) return false;
  }
  return depth == 0;
}

/// The `style` attribute, declaration by declaration: presentation properties only, values
/// through the same URL rules. Empty when nothing survives.
std::string clean_style(std::string_view style, const std::unordered_set<std::string>& ids, std::string_view prefix) {
  std::string out;
  std::size_t i = 0;
  while (i <= style.size()) {
    auto semi = style.find(';', i);
    if (semi == std::string_view::npos) semi = style.size();
    std::string_view decl = style.substr(i, semi - i);
    i = semi + 1;
    auto colon = decl.find(':');
    if (colon == std::string_view::npos) continue;
    std::string property;
    for (char c : trim_space(decl.substr(0, colon))) property += lower(c);
    std::string_view value = trim_space(decl.substr(colon + 1));
    if (!listed(kPresentation, property) || value.empty()) continue;
    if (!self_contained(value)) continue;
    // Quotes must pair up, or the declaration could run on into the next.
    if (std::count(value.begin(), value.end(), '"') % 2 || std::count(value.begin(), value.end(), '\'') % 2) continue;
    auto clean = rewrite_urls(value, ids, prefix);
    if (!clean || !may_quote(property, *clean)) continue;
    if (!out.empty()) out += ';';
    out += property;
    out += ':';
    out += *clean;
  }
  return out;
}

bool is_embedded_image(std::string_view url) {
  if (!starts_with_nocase(url, "data:image/")) return false;
  url.remove_prefix(11);
  for (std::string_view type : {"png", "jpeg", "gif", "webp"})
    if (starts_with_nocase(url, type) && url.size() > type.size() && (url[type.size()] == ';' || url[type.size()] == ','))
      return true;
  return false;
}

/// An attribute's value as written out, or nullopt to drop it.
std::optional<std::string> clean_attribute(const Node& element, const std::string& name, const std::string& value,
                                           const std::unordered_set<std::string>& ids, std::string_view prefix) {
  if (!allowed_attribute(name)) return std::nullopt;
  if (name == "href" || name == "xlink:href") {
    std::string_view url = trim_space(value);
    if (!url.empty() && url.front() == '#') {
      std::string id(url.substr(1));
      bool own = ids.count(id) > 0;
      if (!prefix.empty() && !own) return std::nullopt;
      return "#" + (own ? std::string(prefix) : std::string()) + id;
    }
    if (element.name == "image" && is_embedded_image(url)) return std::string(url);
    return std::nullopt;
  }
  if (name == "id") return value.empty() ? std::nullopt : std::optional<std::string>(std::string(prefix) + value);
  if (name == "style") {
    std::string style = clean_style(value, ids, prefix);
    return style.empty() ? std::nullopt : std::optional<std::string>(std::move(style));
  }
  if (!self_contained(value)) return std::nullopt;
  auto clean = rewrite_urls(value, ids, prefix);
  if (!clean || !may_quote(name, *clean)) return std::nullopt;
  return clean;
}

void collect_ids(const Node& node, std::unordered_set<std::string>& ids) {
  for (const auto& child : node.children) {
    if (child.text) continue;
    for (const auto& [k, v] : child.attrs)
      if (k == "id" && !v.empty()) ids.insert(v);
    collect_ids(child, ids);
  }
}

void write(const Node& node, std::string& out, const std::unordered_set<std::string>& ids, std::string_view prefix) {
  for (const auto& child : node.children) {
    if (child.text) {
      escape_into(out, child.name, false);
      continue;
    }
    out += '<';
    out += child.name;
    bool href = false;
    for (const auto& [k, v] : child.attrs) {
      bool is_href = k == "href" || k == "xlink:href";
      if (is_href && href) continue;  // `href` and `xlink:href` both: the first one written wins
      auto clean = clean_attribute(child, k, v, ids, prefix);
      if (!clean) continue;
      href = href || is_href;
      out += ' ';
      // SVG 2's plain `href`: an exported file declares no xlink namespace.
      out += is_href ? std::string_view("href") : std::string_view(k);
      out += "=\"";
      escape_into(out, *clean, true);
      out += '"';
    }
    if (child.children.empty()) {
      out += "/>";
      continue;
    }
    out += '>';
    write(child, out, ids, prefix);
    out += "</";
    out += child.name;
    out += '>';
  }
}

std::string write_body(const Node& root, std::string_view prefix) {
  std::unordered_set<std::string> ids;
  if (!prefix.empty()) collect_ids(root, ids);
  std::string out;
  write(root, out, ids, prefix);
  // Bytes that are not UTF-8 would make an exported file unreadable.
  if (std::any_of(out.begin(), out.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; })) {
    out = to_valid_utf8(out);
    // U+FFFE and U+FFFF are valid UTF-8 but not XML.
    for (auto at = out.find("\xEF\xBF"); at != std::string::npos; at = out.find("\xEF\xBF", at + 3))
      if (at + 2 < out.size() && (out[at + 2] == '\xBE' || out[at + 2] == '\xBF')) out[at + 2] = '\xBD';
  }
  std::string_view trimmed = js_trim(out);
  if (trimmed.size() != out.size()) {
    auto from = static_cast<std::size_t>(trimmed.data() - out.data());
    out.erase(from + trimmed.size());
    out.erase(0, from);
  }
  return out;
}

}  // namespace

std::optional<SanitizedSvg> sanitize_svg_markup(std::string_view svg, std::string_view id_prefix) {
  Parsed parsed = parse(svg);
  if (!parsed.found) return std::nullopt;
  return SanitizedSvg{std::move(parsed.root_attrs), write_body(parsed.root, id_prefix)};
}

std::string sanitize_svg_body(std::string_view body, std::string_view id_prefix) {
  return write_body(parse(body, true).root, id_prefix);
}

}  // namespace dd::library
