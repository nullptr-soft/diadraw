// Port of packages/import/src/drawio.ts (and the shape tables of drawio-shapes.ts).

#include "diadraw/io/drawio.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <regex>
#include <unordered_map>
#include <unordered_set>

#include "diadraw/base/text.hpp"
#include "diadraw/io/codec.hpp"
#include "diadraw/io/xml.hpp"

namespace dd::io {

// ── Index ─────────────────────────────────────────────────────────────────────────────────────

Result<DrawioIndex> DrawioIndex::from_json(const Json& json) {
  DrawioIndex index;
  const Json* sets = json.get("sets");
  const Json* styles = json.get("styles");
  if (!sets || !sets->is_array() || !styles || !styles->is_object()) return fail("Not a draw.io stencil index");
  for (const auto& s : sets->as_array()) {
    const Json* name = s.get("name");
    const Json* version = s.get("version");
    if (!name || !name->is_string() || !version || !version->is_string()) return fail("Malformed stencil set entry");
    index.sets.push_back({name->as_string(), version->as_string()});
  }
  for (const auto& [style, hit] : styles->as_object()) {
    if (!hit.is_array() || hit.as_array().size() != 2 || !hit.as_array()[0].is_number() || !hit.as_array()[1].is_string())
      continue;
    index.styles.emplace(style, std::pair{static_cast<std::size_t>(hit.as_array()[0].as_number()), hit.as_array()[1].as_string()});
  }
  return index;
}

// ── File forms ────────────────────────────────────────────────────────────────────────────────

Result<std::string> inflate_page(std::string_view text, std::size_t limit, std::size_t* inflated) {
  auto bytes = base64_decode(text);
  if (!bytes) return fail("The page is not base64");
  auto xml = inflate_raw(*bytes, limit);
  if (!xml && !is_too_large(xml.error())) {
    auto zlib = inflate_zlib(*bytes, limit);
    if (zlib || is_too_large(zlib.error())) xml = std::move(zlib);
  }
  if (!xml) return xml;
  if (inflated) *inflated = xml->size();
  std::string_view head = js_trim(*xml);
  if (!head.empty() && head.front() == '<') return xml;
  return safe_decode_uri(*xml);
}

namespace {

/// The `mxfile` XML embedded in a draw.io PNG (a tEXt, zTXt or iTXt chunk named `mxfile`).
Result<std::string> xml_from_png(std::string_view bytes, std::size_t limit, std::size_t& inflated_size) {
  auto inflated = [&](std::string_view body) -> Result<std::string> {
    auto text = inflate_zlib(body, limit);
    if (!text) {
      if (is_too_large(text.error()))
        return fail("The PNG's draw.io diagram could not be decompressed: ", text.error().message);
      return fail("PNG has no embedded draw.io diagram");
    }
    inflated_size = text->size();
    return safe_decode_uri(*text);
  };
  auto u32 = [&](std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3]));
  };
  for (std::size_t at = 8; at + 8 <= bytes.size();) {
    std::size_t length = u32(at);
    std::string_view type = bytes.substr(at + 4, 4);
    std::string_view data = bytes.substr(std::min(at + 8, bytes.size()), length);
    at += 12 + length;
    if (type != "tEXt" && type != "zTXt" && type != "iTXt") continue;
    auto nul = data.find('\0');
    if (nul == std::string_view::npos || data.substr(0, nul) != "mxfile") continue;
    if (type == "tEXt") return safe_decode_uri(decode_windows1252(data.substr(nul + 1)));
    if (type == "zTXt") return inflated(data.substr(std::min(nul + 2, data.size())));
    // iTXt: compression flag, method, language tag \0, translated keyword \0, text
    bool compressed = nul + 1 < data.size() && data[nul + 1] != '\0';
    auto find0 = [&](std::size_t from) {
      auto p = from < data.size() ? data.find('\0', from) : std::string_view::npos;
      return p == std::string_view::npos ? std::size_t{0} : p + 1;  // indexOf(-1) + 1 == 0
    };
    std::size_t p = find0(nul + 3);
    p = find0(p);
    std::string_view body = data.substr(p);
    if (compressed) return inflated(body);
    return safe_decode_uri(body);
  }
  return fail("PNG has no embedded draw.io diagram");
}

/// The value of the first `content` attribute of the first `<svg>` tag that has one, as the
/// regex `<svg\b[^>]*?\scontent="([^"]*)"` finds it, in linear time and constant stack
/// (std::regex recurses once per character, which overflows on an attribute of tens of KB).
std::optional<std::string_view> svg_content_attribute(std::string_view text) {
  constexpr std::string_view kAttr = "content=\"";
  for (std::size_t tag = text.find("<svg"); tag != std::string_view::npos;) {
    std::size_t at = tag + 4;
    if (at < text.size() && is_ascii_word(text[at])) {
      tag = text.find("<svg", tag + 1);
      continue;
    }
    std::size_t end = std::min(text.find('>', at), text.size());
    // `content="` holds no '>', so a candidate lies in the tag exactly when it fits before `end`.
    std::string_view head = text.substr(0, end);
    for (std::size_t attr = head.find(kAttr, at); attr != std::string_view::npos; attr = head.find(kAttr, attr + 1)) {
      if (!is_ascii_space(text[attr - 1])) continue;
      std::size_t from = attr + kAttr.size();
      std::size_t to = text.find('"', from);
      if (to == std::string_view::npos) return std::nullopt;  // no later candidate can close either
      return text.substr(from, to - from);
    }
    // An <svg before `end` sees only candidates of this tag, all of them refused already.
    tag = text.find("<svg", std::max(tag + 1, end));
  }
  return std::nullopt;
}

/// Whether `text` holds `tag` not followed by a word character, in linear time: the regex
/// `tag\b` for a `tag` that ends in a word character, such as `<svg`.
bool contains_tag(std::string_view text, std::string_view tag) {
  for (std::size_t at = text.find(tag); at != std::string_view::npos; at = text.find(tag, at + 1))
    if (at + tag.size() == text.size() || !is_ascii_word(text[at + tag.size()])) return true;
  return false;
}

}  // namespace

DrawioFile read_drawio_file(std::string_view bytes, std::size_t limit) {
  if (bytes.size() >= 4 && static_cast<unsigned char>(bytes[0]) == 0x89 && bytes.substr(1, 3) == "PNG") {
    std::size_t inflated = 0;
    auto xml = xml_from_png(bytes, limit, inflated);
    if (xml) return {std::move(*xml), std::nullopt, inflated};
    return {std::nullopt, xml.error().message};
  }
  if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == 'K')
    return {std::nullopt, "Visio (.vsdx) files are not supported; open them in draw.io and save as .drawio"};
  std::string text(bytes);
  if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
  if (auto attr = svg_content_attribute(text)) {
    std::string content = decode_entities(*attr);
    if (content.rfind("%3C", 0) == 0) content = safe_decode_uri(content);
    std::string_view head = js_trim(content);
    if (!head.empty() && head.front() == '<') return {content, std::nullopt};
    std::size_t inflated = 0;
    auto xml = inflate_page(content, limit, &inflated);
    if (xml) return {std::move(*xml), std::nullopt, inflated};
    if (is_too_large(xml.error()))
      return {std::nullopt, "The SVG's draw.io diagram could not be decompressed: " + xml.error().message};
    return {std::nullopt, "SVG has a content attribute that is not a draw.io diagram"};
  }
  if (contains_tag(text, "<svg") && text.find("<mxfile") == std::string::npos &&
      text.find("<mxGraphModel") == std::string::npos)
    return {std::nullopt, "SVG has no embedded draw.io diagram"};
  if (contains_tag(text, "<mxlibrary")) return {std::nullopt, "This is a draw.io shape library, not a diagram"};
  return {text, std::nullopt};
}

// ── Shapes ────────────────────────────────────────────────────────────────────────────────────

namespace {

const std::unordered_map<std::string_view, std::string_view>& exact_shapes() {
  static const std::unordered_map<std::string_view, std::string_view> m{
      {"rect", "rect"}, {"rectangle", "rect"}, {"label", "rect"}, {"ellipse", "ellipse"}, {"doubleEllipse", "ellipse"},
      {"rhombus", "rhombus"}, {"triangle", "triangle"}, {"hexagon", "hexagon"}, {"cylinder", "cylinder"},
      {"cylinder2", "cylinder"}, {"cylinder3", "cylinder"}, {"datastore", "cylinder"}, {"parallelogram", "parallelogram"},
      {"trapezoid", "trapezoid"}, {"document", "document"}, {"cloud", "cloud"}, {"actor", "actor"}, {"umlActor", "actor"},
      {"note", "note"}, {"note2", "note"}, {"card", "card"}, {"step", "step"}, {"singleArrow", "arrow"},
      {"doubleArrow", "arrow"}, {"flexArrow", "arrow"}, {"process", "process"}, {"process2", "process"}, {"cube", "cube"},
      {"swimlane", "swimlane"}, {"folder", "folder"}, {"text", "text"}, {"image", "image"}, {"line", "rect"},
      {"link", "rect"}, {"callout", "note"}, {"tape", "document"}, {"dataStorage", "cylinder"},
      {"internalStorage", "rect"}, {"corner", "rect"}, {"tee", "rect"}, {"cross", "rect"}, {"plus", "rect"},
      {"orEllipse", "ellipse"}, {"sumEllipse", "ellipse"}, {"lineEllipse", "ellipse"}, {"manualInput", "trapezoid"},
      {"loopLimit", "hexagon"}, {"offPageConnector", "step"}, {"delay", "stadium"}, {"display", "document"},
      {"xor", "ellipse"}, {"or", "ellipse"}, {"and", "ellipse"}, {"partialRectangle", "rect"}, {"table", "rect"},
      {"tableRow", "rect"}, {"group", "invisible"}, {"waypoint", "invisible"}, {"message", "card"},
      {"dimension", "rect"}, {"smileyFace", "ellipse"}, {"curlyBracket", "text"}, {"switch", "rect"},
      {"umlState", "rounded"}, {"umlFrame2", "rect"}, {"mxRectangle", "rect"},
      {"mxgraph.flowchart.parallel_mode", "rect"}, {"module", "module"}, {"component", "rect"},
      {"umlLifeline", "lifeline"}, {"umlFrame", "frame"}, {"umlBoundary", "circle"}, {"umlEntity", "circle"},
      {"umlControl", "circle"}, {"umlDestroy", "rect"}, {"providedRequiredInterface", "interface"},
      {"requiredInterface", "interface"}, {"lollipop", "interface"}, {"startState", "circle"}, {"endState", "circle"},
      {"ext", "rect"}, {"isoCube", "cube"}, {"isoCube2", "cube"}, {"isoRectangle", "rhombus"},
      {"mxgraph.flowchart.terminator", "stadium"}, {"mxgraph.flowchart.process", "rect"},
      {"mxgraph.flowchart.decision", "rhombus"}, {"mxgraph.flowchart.data", "parallelogram"},
      {"mxgraph.flowchart.document", "document"}, {"mxgraph.flowchart.multi-document", "document"},
      {"mxgraph.flowchart.database", "cylinder"}, {"mxgraph.flowchart.stored_data", "cylinder"},
      {"mxgraph.flowchart.direct_data", "cylinder"}, {"mxgraph.flowchart.sequential_data", "circle"},
      {"mxgraph.flowchart.predefined_process", "process"}, {"mxgraph.flowchart.manual_input", "trapezoid"},
      {"mxgraph.flowchart.manual_operation", "trapezoid"}, {"mxgraph.flowchart.preparation", "hexagon"},
      {"mxgraph.flowchart.delay", "stadium"}, {"mxgraph.flowchart.display", "document"},
      {"mxgraph.flowchart.card", "card"}, {"mxgraph.flowchart.paper_tape", "document"},
      {"mxgraph.flowchart.annotation_1", "text"}, {"mxgraph.flowchart.annotation_2", "text"},
      {"mxgraph.flowchart.on-page_reference", "circle"}, {"mxgraph.flowchart.off-page_reference", "step"},
      {"mxgraph.flowchart.start_1", "ellipse"}, {"mxgraph.flowchart.start_2", "circle"},
      {"mxgraph.flowchart.extract_or_measurement", "triangle"}, {"mxgraph.flowchart.merge_or_storage", "triangle"},
      {"mxgraph.flowchart.or", "circle"}, {"mxgraph.flowchart.summing_function", "circle"},
      {"mxgraph.flowchart.collate", "triangle"}, {"mxgraph.flowchart.sort", "rhombus"},
      {"mxgraph.flowchart.loop_limit", "hexagon"}, {"mxgraph.flowchart.internal_storage", "rect"},
      {"mxgraph.flowchart.transfer", "arrow"}, {"mxgraph.bpmn.task", "rounded"}, {"mxgraph.bpmn.gateway2", "rhombus"},
      {"mxgraph.bpmn.event", "circle"}, {"mxgraph.bpmn.data", "note"}, {"mxgraph.bpmn.data2", "note"},
      {"mxgraph.bpmn.conversation", "hexagon"}, {"mxgraph.basic.rect", "rect"}, {"mxgraph.basic.ellipse", "ellipse"},
      {"mxgraph.basic.triangle", "triangle"}, {"mxgraph.basic.octagon2", "octagon"},
      {"mxgraph.basic.cloud_rect", "cloud"}, {"mxgraph.basic.document", "document"},
  };
  return m;
}

const std::vector<std::pair<std::regex, std::string>>& families() {
  static const std::vector<std::pair<std::regex, std::string>> f{
      {std::regex("^mxgraph\\.(aws\\d*|azure\\d*|gcp\\d*)\\.(group|groupCenter)"), "rect"},
      {std::regex("^mxgraph\\.(aws\\d*|azure\\d*|gcp\\d*|alibaba_cloud|ibm|kubernetes|veeam\\d*|citrix\\d*|cisco\\w*|"
                  "network\\d*|rack|office|vvd|sap\\w*|android|ios\\d*|eip|electrical|pid\\d*|signs|weblogos|lean_mapping|"
                  "cabinets|floorplan|mscae|openstack|salesforce|webicons|atlassian|gmdl|dfd|ios7\\w*|cisco_safe|"
                  "fluidpower|lean|kpi|infographic|networks\\d*|bootstrap|cloud_rack)\\."),
       "image"},
      {std::regex("^mxgraph\\.bpmn\\."), "rounded"},
      {std::regex("^mxgraph\\.flowchart\\."), "rect"},
      {std::regex("^mxgraph\\.(arrows\\d*)\\."), "arrow"},
      {std::regex("^mxgraph\\.(basic|general)\\."), "rect"},
      {std::regex("^mxgraph\\.(er|uml\\d*|sysml|archimate\\d*|c4)\\."), "rect"},
      {std::regex("^mxgraph\\.(mockup|mockups|ios|android|bootstrap)\\."), "rect"},
      {std::regex("^mxgraph\\.lean_mapping\\."), "image"},
  };
  return f;
}

std::string kind_of(std::string_view base) {
  if (base == "rhombus") return "decision";
  if (base == "cylinder") return "database";
  if (base == "actor") return "person";
  if (base == "note" || base == "text") return "note";
  if (base == "swimlane") return "system";
  return "process";
}

using Values = std::vector<std::pair<std::string, std::string>>;

const std::string* value_of(const Values& values, std::string_view key) {
  for (const auto& [k, v] : values)
    if (k == key) return &v;
  return nullptr;
}

}  // namespace

ShapeInfo shape_for(const std::vector<std::string>& names, const Values& values) {
  auto info = [](std::string base, std::optional<std::string> name, bool unknown = false) {
    ShapeInfo s;
    s.kind = kind_of(base);
    s.base = std::move(base);
    s.name = std::move(name);
    s.unknown = unknown;
    return s;
  };
  const std::string* shape = value_of(values, "shape");
  const std::string* perimeter = value_of(values, "perimeter");
  const std::string* rounded = value_of(values, "rounded");
  if (shape && (*shape == "mxgraph.bpmn.shape" || *shape == "mxgraph.bpmn.gateway2")) {
    // BPMN 1 shapes: the symbol says whether it is a gateway (diamond) or an event (circle).
    const std::string* symbol = value_of(values, "symbol");
    bool gateway = (shape->size() >= 8 && shape->compare(shape->size() - 8, 8, "gateway2") == 0) ||
                   (symbol && std::regex_search(*symbol, std::regex("gw|gateway", std::regex::icase))) ||
                   (perimeter && *perimeter == "rhombusPerimeter");
    return info(gateway ? "rhombus" : "circle", *shape);
  }
  // The perimeter tells round and diamond shapes apart when the name alone does not.
  auto by_perimeter = [&](const std::string& base) -> std::string {
    if (base != "rect" && base != "rounded") return base;
    if (perimeter && *perimeter == "ellipsePerimeter") return "ellipse";
    if (perimeter && *perimeter == "rhombusPerimeter") return "rhombus";
    return base;
  };
  const auto& exact = exact_shapes();
  if (shape) {
    if (auto it = exact.find(*shape); it != exact.end()) return info(by_perimeter(std::string(it->second)), *shape);
    if (shape->rfind("stencil(", 0) == 0) return info("image", "stencil");
    for (const auto& [re, base] : families()) {
      if (std::regex_search(*shape, re)) {
        ShapeInfo s = info(by_perimeter(base), *shape);
        s.stencil = base == "image";
        return s;
      }
    }
    // Any other library stencil: an icon box with its label, like the known icon families.
    if (shape->rfind("mxgraph.", 0) == 0) {
      ShapeInfo s = info("image", *shape);
      s.stencil = true;
      return s;
    }
    return info(rounded && *rounded == "1" ? "rounded" : "rect", *shape, true);
  }
  if (value_of(values, "image") || std::find(names.begin(), names.end(), "image") != names.end())
    return info("image", "image");
  for (const auto& n : names) {
    if (auto it = exact.find(n); it != exact.end()) {
      const std::string* layout = value_of(values, "childLayout");
      return info(n == "swimlane" && layout && *layout == "stackLayout" ? "swimlane" : std::string(it->second), n);
    }
  }
  if (value_of(values, "ellipse")) return info("ellipse", "ellipse");
  return info(by_perimeter(rounded && *rounded == "1" ? "rounded" : "rect"), std::nullopt);
}

bool is_known_style_key(std::string_view key) {
  static const std::unordered_set<std::string_view> known = [] {
    static const char* const kList =
        "shape html whiteSpace rounded arcSize fillColor strokeColor strokeWidth dashed dashPattern opacity fillOpacity "
        "strokeOpacity textOpacity fontColor fontSize fontFamily fontStyle align verticalAlign labelPosition "
        "verticalLabelPosition labelBackgroundColor labelBorderColor spacing spacingTop spacingLeft spacingRight "
        "spacingBottom spacingTop gradientColor gradientDirection shadow glass sketch curved edgeStyle elbow orthogonal "
        "orthogonalLoop jettySize jettyWidth jettyHeight endArrow startArrow endFill startFill endSize startSize exitX exitY exitDx exitDy "
        "exitPerimeter entryX entryY entryDx entryDy entryPerimeter jumpStyle jumpSize image imageAspect imageWidth "
        "imageHeight imageAlign imageVerticalAlign imageBackground imageBorder aspect perimeter perimeterSpacing "
        "portConstraint direction rotation flipH flipV container collapsible childLayout horizontal startSize "
        "swimlaneLine swimlaneFillColor resizeParent resizeParentMax resizeLast horizontalStack marginBottom marginTop "
        "marginLeft marginRight connectable recursiveResize expand editable movable resizable deletable rotatable "
        "points overflow autosize fixedSize part dropTarget treeFolding treeMoving noLabel portConstraintRotation "
        "size backgroundOutline outline symbol isLoopStandard isLoopMultiParallel isLoopSequential isLoopComp "
        "isAdHoc bpmnShapeType gwType taskMarker verticalTextPosition comic jiggle fillStyle hachureGap hachureAngle "
        "disableMultiStroke disableMultiStrokeFill simplification curveFitting fillWeight sourcePerimeterSpacing "
        "targetPerimeterSpacing labelPadding textDirection fontFamilies strokeColor2 fillColor2 resIcon prIcon "
        "grIcon pathIcon resourceIcon sketchStyle boundedLbl darkOpacity darkOpacity1 darkOpacity2 dx dy notch "
        "tabWidth tabHeight tabPosition position position2 base arrowWidth arrowSize width endWidth snapToPoint "
        "pointerEvents allowArrows enumerate enumerateValue cloneable metaEdit editableCssRules moveCells "
        "targetPortConstraint sourcePortConstraint anchorPointDirection fixDash entityRelation segment "
        "mainMenuColor mainMenuStroke subMenuColor subMenuStroke strokeColor3 buttonText textSize barPos "
        "barHeight textColor textColor2 mainText subText icon line top bottom left right indent participant "
        "lifelineDashed lifelineColor lifelineMirror outlineConnect newEdgeStyle targetShapes libavoidRouting "
        "defaultLabelBackgroundColor";
    std::unordered_set<std::string_view> set;
    std::string_view all(kList);
    std::size_t start = 0;
    while (start < all.size()) {
      auto end = all.find(' ', start);
      if (end == std::string_view::npos) end = all.size();
      set.insert(all.substr(start, end - start));
      start = end + 1;
    }
    return set;
  }();
  return known.count(key) > 0;
}

// ── Labels ────────────────────────────────────────────────────────────────────────────────────

namespace {

/// Whether the inside of a tag (between '<' and '>') is a line break as the regexes
/// `<br\s*\/?>` and `<\/(div|p|li|h[1-6]|tr)>` read it, ignoring case.
bool is_line_break_tag(std::string_view inside) {
  if (starts_with_ascii_icase(inside, "br")) {
    std::size_t i = 2;
    while (i < inside.size() && is_ascii_space(inside[i])) ++i;
    if (i < inside.size() && inside[i] == '/') ++i;
    return i == inside.size();
  }
  if (inside.empty() || inside.front() != '/') return false;
  std::string_view name = inside.substr(1);
  if (name.size() == 2 && ascii_lower(name[0]) == 'h' && name[1] >= '1' && name[1] <= '6') return true;
  static constexpr std::array<std::string_view, 4> kBlocks{"div", "p", "li", "tr"};
  return std::any_of(kBlocks.begin(), kBlocks.end(), [&](std::string_view block) { return ascii_iequals(name, block); });
}

/// `html` with its <br>s and closing block tags as line breaks and every other tag removed: the
/// regex replacements `<br\s*\/?>` and `<\/(div|p|li|h[1-6]|tr)>` with "\n", then `<[^>]*>` with "",
/// ignoring case. Scanned by hand in linear time and constant stack, because std::regex recurses
/// once per character of a match and overflows on a tag that holds an image of a few hundred KB.
std::string strip_tags(std::string_view html) {
  // First the line breaks: a tag of either kind holds no '<' or '>' but its own, so it is the
  // text from a '<' to the next '>' with no '<' between them.
  std::string breaks;
  breaks.reserve(html.size());
  for (std::size_t i = 0; i < html.size();) {
    std::size_t open = html.find('<', i);
    if (open == std::string_view::npos) open = html.size();
    breaks.append(html, i, open - i);
    if (open == html.size()) break;
    std::size_t end = html.find_first_of("<>", open + 1);
    if (end != std::string_view::npos && html[end] == '>' && is_line_break_tag(html.substr(open + 1, end - open - 1))) {
      breaks.push_back('\n');
      i = end + 1;
    } else {
      breaks.push_back('<');
      i = open + 1;
    }
  }
  // Then every other tag: from a '<' to the next '>'. With no '>' left, no later '<' closes.
  std::string out;
  out.reserve(breaks.size());
  for (std::size_t i = 0; i < breaks.size();) {
    std::size_t open = breaks.find('<', i);
    std::size_t close = open == std::string::npos ? std::string::npos : breaks.find('>', open + 1);
    if (close == std::string::npos) {
      out.append(breaks, i, std::string::npos);
      break;
    }
    out.append(breaks, i, open - i);
    i = close + 1;
  }
  return out;
}

/// One line of a label: runs of spaces, tabs and no-break spaces become one space; then trimmed.
std::string collapse_line(std::string_view line) {
  std::string collapsed;
  bool run = false;
  for (std::size_t i = 0; i < line.size();) {
    std::size_t at = i;
    std::uint32_t cp = next_code_point(line, i);
    if (cp == ' ' || cp == '\t' || cp == 0xA0) {
      if (!run) collapsed.push_back(' ');
      run = true;
    } else {
      collapsed.append(line, at, i - at);
      run = false;
    }
  }
  return std::string(js_trim(collapsed));
}

}  // namespace

std::string html_to_text(std::string_view html) {
  std::string s = decode_entities(strip_tags(html));
  std::string joined;
  std::size_t start = 0;
  while (true) {
    auto nl = s.find('\n', start);
    std::string_view line = std::string_view(s).substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    joined += collapse_line(line);
    if (nl == std::string::npos) break;
    joined.push_back('\n');
    start = nl + 1;
  }
  // .replace(/\n{3,}/g, '\n\n')
  std::string out;
  for (std::size_t i = 0; i < joined.size();) {
    if (joined[i] == '\n') {
      std::size_t j = i;
      while (j < joined.size() && joined[j] == '\n') ++j;
      out.append(j - i >= 3 ? 2 : j - i, '\n');
      i = j;
    } else {
      out.push_back(joined[i++]);
    }
  }
  return std::string(js_trim(out));
}

namespace {

/// A code point of a label in its fontStyle bits.
struct StyledChar {
  std::string text;  // UTF-8
  int bits;
};
using StyledLine = std::vector<StyledChar>;

/// The position of the fontStyle bit a tag's element sets: 0 (bold, 1) for <b>/<strong>, 1 (italic,
/// 2) for <i>/<em>, 2 (underline, 4) for <u>, 3 (strike-through, 8) for <s>/<strike>/<del>; -1 for
/// any other.
int font_bit(std::string_view name) {
  if (name == "b" || name == "strong") return 0;
  if (name == "i" || name == "em") return 1;
  if (name == "u") return 2;
  if (name == "s" || name == "strike" || name == "del") return 3;
  return -1;
}

/// The lines of `html` as html_to_text lays them out (collapsed and trimmed, at most one blank line
/// in a row, none at either end), each code point with `base` and the bits of the font elements
/// open around it. Empty when they cannot be matched up with `expected`, what html_to_text gives.
std::vector<StyledLine> styled_lines(std::string_view html, int base, std::string_view expected) {
  // Runs of text with the bits of the font elements open around them.
  struct Run {
    std::string text;
    int bits;
  };
  std::vector<std::vector<Run>> lines(1);
  std::array<int, 4> open{};  // bold, italic, underline, strike-through
  auto bits = [&] {
    int b = base;
    for (std::size_t k = 0; k < open.size(); ++k)
      if (open[k] > 0) b |= 1 << k;
    return b;
  };
  std::string pending;
  bool unclosed = false;
  auto flush = [&] {
    if (!pending.empty()) lines.back().push_back({decode_entities(pending), bits()});
    pending.clear();
  };
  for (std::size_t i = 0; i < html.size();) {
    const char c = html[i];
    if (c == '\n') {
      flush();
      lines.emplace_back();
      ++i;
      continue;
    }
    // Once a '<' has no '>' after it, none further on has one either: no rescanning.
    auto close = c == '<' && !unclosed ? html.find('>', i) : std::string_view::npos;
    if (c == '<' && close == std::string_view::npos) unclosed = true;
    if (close == std::string_view::npos) {
      pending.push_back(c);
      ++i;
      continue;
    }
    flush();
    std::string_view tag = html.substr(i + 1, close - i - 1);
    i = close + 1;
    const bool end = !tag.empty() && tag.front() == '/';
    std::size_t name_end = end ? 1 : 0;
    while (name_end < tag.size() && std::isalnum(static_cast<unsigned char>(tag[name_end]))) ++name_end;
    std::string name(tag.substr(end ? 1 : 0, name_end - (end ? 1 : 0)));
    for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
    if ((!end && name == "br") ||
        (end && (name == "div" || name == "p" || name == "li" || name == "tr" || heading))) {
      lines.emplace_back();
      continue;
    }
    const int bit = font_bit(name);
    if (bit < 0) continue;
    auto& count = open[static_cast<std::size_t>(bit)];
    if (end)
      count = std::max(0, count - 1);
    else if (tag.back() != '/')
      ++count;
  }
  flush();
  // Each line collapsed as collapse_line does it (kept in step with it by the check against
  // `expected` below): a run of blanks becomes one space in the bits of its first blank, then the
  // line is trimmed.
  std::vector<StyledLine> laid;
  for (const auto& runs : lines) {
    StyledLine line;
    bool blank_run = false;
    for (const auto& r : runs) {
      for (std::size_t i = 0; i < r.text.size();) {
        const std::size_t at = i;
        const std::uint32_t cp = next_code_point(r.text, i);
        if (cp == ' ' || cp == '\t' || cp == 0xA0) {
          if (!blank_run) line.push_back({" ", r.bits});
          blank_run = true;
        } else {
          line.push_back({r.text.substr(at, i - at), r.bits});
          blank_run = false;
        }
      }
    }
    std::string text;
    std::vector<std::size_t> starts;  // the byte each code point starts at
    for (const auto& ch : line) {
      starts.push_back(text.size());
      text += ch.text;
    }
    const std::string_view trimmed = js_trim(text);
    const auto first = static_cast<std::size_t>(trimmed.data() - text.data());
    const std::size_t last = first + trimmed.size();
    StyledLine kept;
    for (std::size_t k = 0; k < line.size(); ++k)
      if (starts[k] >= first && starts[k] < last) kept.push_back(std::move(line[k]));
    if (kept.empty() && (laid.empty() || laid.back().empty())) continue;
    laid.push_back(std::move(kept));
  }
  while (!laid.empty() && laid.back().empty()) laid.pop_back();
  std::string joined;
  for (std::size_t k = 0; k < laid.size(); ++k) {
    if (k > 0) joined.push_back('\n');
    for (const auto& ch : laid[k]) joined += ch.text;
  }
  // Anything this reading takes differently from html_to_text leaves the label to its fontStyle.
  if (joined != expected) return {};
  return laid;
}

/// The bits all of a line's visible text has, or `base` when it has none.
int line_bits(const StyledLine& line, int base) {
  int b = 15;
  bool visible = false;
  for (const auto& ch : line)
    if (ch.text != " ") {
      b &= ch.bits;
      visible = true;
    }
  return visible ? b : base;
}

/// Whether some line's visible text differs in style within the line.
bool mixed_within_lines(const std::vector<StyledLine>& lines) {
  for (const auto& line : lines) {
    std::optional<int> seen;
    for (const auto& ch : line) {
      if (ch.text == " ") continue;
      if (seen && *seen != ch.bits) return true;
      seen = ch.bits;
    }
  }
  return false;
}

/// fontRuns for `lines`: `count:bits` for each run of code points in one style, a line break
/// counting as one code point in the style of the run before it.
std::vector<FontRun> font_runs(const std::vector<StyledLine>& lines, int base) {
  std::vector<FontRun> out;
  auto add = [&](int bits) {
    if (!out.empty() && out.back().bits == bits) ++out.back().count;
    else out.push_back({1, bits});
  };
  for (std::size_t k = 0; k < lines.size(); ++k) {
    if (k > 0) add(out.empty() ? base : out.back().bits);
    for (const auto& ch : lines[k]) add(ch.bits);
  }
  return out;
}

}  // namespace

std::vector<int> html_line_font_styles(std::string_view html, int base) {
  std::vector<int> out;
  for (const auto& line : styled_lines(html, base, html_to_text(html))) out.push_back(line_bits(line, base));
  return out;
}

std::vector<FontRun> html_font_runs(std::string_view html, int base) {
  return font_runs(styled_lines(html, base, html_to_text(html)), base);
}

// ── Import ────────────────────────────────────────────────────────────────────────────────────

namespace {

struct Geo {
  double x = 0, y = 0, width = 0, height = 0;
  bool relative = false;
  /// The `offset` point: a relative vertex's offset from its spot on the parent, in pixels.
  Point2 offset{0, 0};
  std::vector<Point2> points;
  std::optional<Point2> source_point, target_point;
};

struct Cell {
  std::string id;
  std::string value;
  std::string style_text;
  std::vector<std::string> names;
  Values style;
  std::optional<std::string> parent;
  bool vertex = false, edge = false;
  std::optional<std::string> source, target;
  bool visible = true, collapsed = false;
  std::optional<Geo> geometry;
  /// Attributes of an `object`/`UserObject` wrapper: custom data, tooltip, placeholders.
  Values data;
};

void parse_style(const std::string& style, std::vector<std::string>& names, Values& values) {
  std::size_t start = 0;
  while (start <= style.size()) {
    auto end = style.find(';', start);
    if (end == std::string::npos) end = style.size();
    std::string part = style.substr(start, end - start);
    if (!part.empty()) {
      auto eq = part.find('=');
      if (eq == std::string::npos) {
        if (std::find(names.begin(), names.end(), part) == names.end()) names.push_back(part);
      } else {
        std::string key = part.substr(0, eq), value = part.substr(eq + 1);
        bool replaced = false;
        for (auto& [k, v] : values)
          if (k == key) {
            v = value;
            replaced = true;
          }
        if (!replaced) values.emplace_back(std::move(key), std::move(value));
      }
    }
    start = end + 1;
  }
}

double number_attr(const XmlNode& n, std::string_view key) {
  const std::string* v = n.attr(key);
  return v ? js_to_number(*v) : 0;
}

Point2 point_of(const XmlNode& n) { return {number_attr(n, "x"), number_attr(n, "y")}; }

// ── Sequence messages ──
// draw.io routes an edge with edgeStyle=sequenceEdgeStyle (a UML message) itself, from its ends and
// at most two waypoints (mxEdgeStyle.SequenceMessage in its Shapes.js). The import does the same
// once and keeps the result as a route hint, as a fixed layout has no such style.

/// One end of a message: its terminal's box in absolute coordinates, how the terminal's outline
/// takes a connection, and the point a fixed exit or entry (exitX, entryDy, …) pins it to.
struct MessageEnd {
  double x = 0, y = 0, width = 0, height = 0;
  /// A lifeline takes connections on its line, below a head `head` deep; any other terminal (an
  /// activation bar) on the side of its box that faces the message, as orthogonalPerimeter does.
  bool lifeline = false;
  double head = 0;
  std::optional<Point2> pinned;

  [[nodiscard]] double center_x() const { return x + width / 2; }
  [[nodiscard]] double center_y() const { return y + height / 2; }
  /// The highest and lowest y a message can leave or enter at.
  [[nodiscard]] double top() const { return lifeline ? y + std::min(head, height) : y; }
  [[nodiscard]] double bottom() const { return y + height; }
  /// Where the outline takes a line that heads for `next` (lifelinePerimeter, orthogonalPerimeter).
  [[nodiscard]] Point2 anchor(Point2 next) const {
    const double at_y = std::clamp(next.y, top(), bottom());
    if (lifeline) return {center_x(), at_y};
    return {std::clamp(next.x, x, x + width), at_y};
  }
};

struct MessageRoute {
  Point2 start;
  std::vector<Point2> via;
  Point2 end;
};

/// draw.io's route for a message from `s` to `t` with the waypoints `points` (absolute), or
/// nothing for one it draws through its waypoints as they are (more than one, not a self-call).
/// `self`: both ends are on one lifeline, so the message is a loop out to the right and back.
std::optional<MessageRoute> message_route(const MessageEnd& s, const MessageEnd& t, bool self,
                                          const std::vector<Point2>& points) {
  constexpr double kSelfCallSize = 30;  // draw.io's sequenceSelfCallSize: the loop's width and height
  if (self && points.size() <= 2) {
    const std::optional<Point2> first = points.empty() ? std::nullopt : std::optional(points.front());
    const std::optional<Point2> last = points.size() > 1 ? std::optional(points.back()) : std::nullopt;
    const double y1 = s.pinned ? s.pinned->y : first ? first->y : s.center_y();
    const double y2 = t.pinned ? t.pinned->y : last ? last->y : y1 + kSelfCallSize;
    const double x = first ? first->x : s.anchor({s.x + s.width + kSelfCallSize, y1}).x + kSelfCallSize;
    return MessageRoute{s.pinned.value_or(s.anchor({x, y1})), {{x, y1}, {x, y2}}, t.pinned.value_or(t.anchor({x, y2}))};
  }
  if (points.size() > 1) return std::nullopt;
  if (s.pinned && t.pinned) return MessageRoute{*s.pinned, {}, *t.pinned};
  // The x of draw.io's routed point: halfway between the centres, out of both terminals.
  double mx = (s.center_x() + t.center_x()) / 2;
  const double dir = s.center_x() <= t.center_x() ? 1 : -1;
  auto clear = [&](const MessageEnd& end, double side) {
    if (mx > end.x && mx < end.x + end.width) mx = side > 0 ? end.x + end.width : end.x;
  };
  clear(s, dir);
  clear(t, -dir);
  if (s.pinned || t.pinned) {
    // A pinned end gives the y; out of the other end's reach, the line runs straight to the pin.
    const Point2 pin = s.pinned ? *s.pinned : *t.pinned;
    const MessageEnd& floating = s.pinned ? t : s;
    const Point2 other = floating.anchor(pin.y < floating.top() || pin.y > floating.bottom() ? pin : Point2{mx, pin.y});
    return s.pinned ? MessageRoute{pin, {}, other} : MessageRoute{other, {}, pin};
  }
  // The waypoint holds the message's y (else it is halfway between the centres), kept where both
  // ends reach; ends that share no y get a straight line between them.
  double y = points.empty() ? (s.center_y() + t.center_y()) / 2 : points.front().y;
  const double top = std::max(s.top(), t.top());
  const double bottom = std::min(s.bottom(), t.bottom());
  if (!(top <= bottom)) return MessageRoute{s.anchor({t.center_x(), t.center_y()}), {}, t.anchor({s.center_x(), s.center_y()})};
  y = std::clamp(y, top, bottom);
  return MessageRoute{s.anchor({mx, y}), {}, t.anchor({mx, y})};
}

std::vector<Cell> cells_of(const XmlNode& model) {
  std::vector<Cell> cells;
  walk_xml(model, [&](const XmlNode& node) {
    const XmlNode* cell = nullptr;
    Values attrs;
    Values data;
    if (node.name == "mxCell") {
      const std::string* id = node.attr("id");
      if (!id || id->empty()) return;
      cell = &node;
      attrs = node.attrs;
    } else if (node.name == "object" || node.name == "UserObject") {
      // Wrappers carry the id, label and custom data; the inner mxCell carries the rest.
      cell = node.child("mxCell");
      if (!cell) return;
      attrs = cell->attrs;
      std::string id, label;
      for (const auto& [k, v] : node.attrs) {
        if (k == "id") id = v;
        else if (k == "label") label = v;
        else data.emplace_back(k, v);
      }
      auto set = [&](const std::string& key, const std::string& value) {
        for (auto& [k, v] : attrs)
          if (k == key) {
            v = value;
            return;
          }
        attrs.emplace_back(key, value);
      };
      set("id", id);
      set("value", label);
    } else {
      return;
    }
    auto get = [&](std::string_view key) -> const std::string* {
      for (const auto& [k, v] : attrs)
        if (k == key) return &v;
      return nullptr;
    };
    Cell c;
    c.id = get("id") ? *get("id") : "";
    c.value = get("value") ? *get("value") : "";
    c.style_text = get("style") ? *get("style") : "";
    parse_style(c.style_text, c.names, c.style);
    c.vertex = get("vertex") && *get("vertex") == "1";
    c.edge = get("edge") && *get("edge") == "1";
    c.visible = !(get("visible") && *get("visible") == "0");
    c.collapsed = get("collapsed") && *get("collapsed") == "1";
    c.data = std::move(data);
    if (get("parent")) c.parent = *get("parent");
    if (get("source")) c.source = *get("source");
    if (get("target")) c.target = *get("target");
    if (const XmlNode* g = cell->child("mxGeometry")) {
      Geo geo;
      geo.x = number_attr(*g, "x");
      geo.y = number_attr(*g, "y");
      geo.width = number_attr(*g, "width");
      geo.height = number_attr(*g, "height");
      const std::string* rel = g->attr("relative");
      geo.relative = rel && *rel == "1";
      // The first <Array as="points">, even when it is empty.
      for (const auto& child : g->children) {
        const std::string* as = child->attr("as");
        if (child->name == "Array" && as && *as == "points") {
          for (const auto& p : child->children)
            if (p->name == "mxPoint") geo.points.push_back(point_of(*p));
          break;
        }
      }
      // The first child with as="sourcePoint" / "targetPoint", whatever its element name.
      for (const auto& child : g->children) {
        const std::string* as = child->attr("as");
        if (!as) continue;
        if (*as == "sourcePoint" && !geo.source_point) geo.source_point = point_of(*child);
        if (*as == "targetPoint" && !geo.target_point) geo.target_point = point_of(*child);
        if (*as == "offset" && child->name == "mxPoint") geo.offset = point_of(*child);
      }
      c.geometry = std::move(geo);
    }
    cells.push_back(std::move(c));
  });
  return cells;
}

/// `%name%` placeholders in labels, filled from the cell's data (as draw.io does).
std::string fill_placeholders(const std::string& label, const Values& data) {
  auto word = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
  };
  std::string out;
  for (std::size_t i = 0; i < label.size();) {
    if (label[i] == '%') {
      std::size_t j = i + 1;
      while (j < label.size() && word(label[j])) ++j;
      if (j > i + 1 && j < label.size() && label[j] == '%') {
        std::string key = label.substr(i + 1, j - i - 1);
        const std::string* v = value_of(data, key);
        out += v ? *v : label.substr(i, j - i + 1);
        i = j + 1;
        continue;
      }
    }
    out.push_back(label[i++]);
  }
  return out;
}

bool is_html_label(const Cell& c, std::string_view raw) {
  const std::string* html = value_of(c.style, "html");
  if (html && *html == "1") return true;
  // The regex `<(br|div|span|b|i|font|p)\b`, ignoring case, scanned by hand as html_to_text is.
  static constexpr std::array<std::string_view, 7> kNames{"br", "div", "span", "b", "i", "font", "p"};
  for (std::size_t at = raw.find('<'); at != std::string_view::npos; at = raw.find('<', at + 1)) {
    std::string_view rest = raw.substr(at + 1);
    for (std::string_view name : kNames)
      if (starts_with_ascii_icase(rest, name) && (rest.size() == name.size() || !is_ascii_word(rest[name.size()])))
        return true;
  }
  return false;
}

/// Whether `value` holds a tag as the regex `<[a-z][^>]*>`, ignoring case, finds one: a '<' and a
/// letter with a '>' anywhere after them. The first such pair has a '>' after it if any has.
bool has_tag(std::string_view value) {
  for (std::size_t at = value.find('<'); at != std::string_view::npos; at = value.find('<', at + 1))
    if (at + 1 < value.size() && ascii_lower(value[at + 1]) >= 'a' && ascii_lower(value[at + 1]) <= 'z')
      return value.find('>', at + 2) != std::string_view::npos;
  return false;
}

std::string label_of(const Cell& c) {
  std::string raw = fill_placeholders(c.value, c.data);
  if (is_html_label(c, raw)) return html_to_text(raw);
  return std::string(js_trim(raw));
}

/// The shape keys of draw.io's cube, note, folder and module, its label spacing and its bounded
/// labels, with draw.io's defaults where DiaDraw's own shape would draw differently without them.
void add_shape_style(Style& out, const Cell& c, const std::string& base) {
  const std::string* shape = value_of(c.style, "shape");
  auto copy = [&](std::string_view key, std::optional<std::string_view> fallback = std::nullopt) {
    const std::string* value = value_of(c.style, key);
    if (value && !value->empty())
      out.set(std::string(key), *value);
    else if (fallback)
      out.set(std::string(key), std::string(*fallback));
  };
  const bool drawio_shape = shape && *shape == base;
  if (base == "cube") {
    copy("size", drawio_shape ? std::optional<std::string_view>("20") : std::nullopt);
    copy("direction", drawio_shape ? std::optional<std::string_view>("east") : std::nullopt);
  } else if (base == "note") {
    // Only notes fold their corner by `size`; a callout's is the length of its pointer.
    if (shape && (*shape == "note" || *shape == "note2")) copy("size", "30");
    else if (shape && shape->rfind("mxgraph.bpmn.data", 0) == 0) copy("size");
  } else if (base == "folder") {
    copy("tabWidth", drawio_shape ? std::optional<std::string_view>("60") : std::nullopt);
    copy("tabHeight", drawio_shape ? std::optional<std::string_view>("20") : std::nullopt);
    copy("tabPosition", drawio_shape ? std::optional<std::string_view>("right") : std::nullopt);
  } else if (base == "module") {
    copy("jettyWidth");
    copy("jettyHeight");
  } else if (base == "lifeline") {
    copy("size");
    copy("participant");
    copy("lifelineDashed");
  }
  for (const char* k : {"spacingTop", "spacingLeft", "spacingRight", "spacingBottom", "boundedLbl"}) copy(k);
}

/// A <b>, <strong>, <i>, <em>, <u>, <s>, <strike> or <del> tag in `html`; without one every line has
/// the cell's own fontStyle, so most labels need no closer reading.
bool has_font_tag(std::string_view html) {
  for (auto at = html.find('<'); at != std::string_view::npos; at = html.find('<', at + 1)) {
    std::size_t start = at + 1 + (at + 1 < html.size() && html[at + 1] == '/' ? 1 : 0);
    std::size_t end = start;
    while (end < html.size() && end - start <= 6 && std::isalpha(static_cast<unsigned char>(html[end]))) ++end;
    std::string name(html.substr(start, end - start));
    for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (font_bit(name) >= 0) return true;
  }
  return false;
}

/// Bold, italic, underlined and struck-through text of an HTML label: the whole label's fontStyle
/// when all of it has the same, one fontStyle per line in lineFontStyles when each line has one,
/// else runs of code points in fontRuns. `label` is what label_of gives for the cell.
void add_line_font_styles(Style& out, const Cell& c, std::string_view label) {
  std::string raw = fill_placeholders(c.value, c.data);
  if (!has_font_tag(raw) || !is_html_label(c, raw)) return;
  const std::string* font_style = value_of(c.style, "fontStyle");
  const int base = font_style ? js_to_int32(js_to_number(*font_style)) & 15 : 0;
  const std::vector<StyledLine> styled = styled_lines(raw, base, label);
  if (styled.empty()) return;
  if (mixed_within_lines(styled)) {
    std::string list;
    for (const auto& run : font_runs(styled, base))
      list += (list.empty() ? "" : ",") + std::to_string(run.count) + ":" + std::to_string(run.bits);
    out.set("fontRuns", list);
    return;
  }
  std::vector<int> lines;
  for (const auto& line : styled) lines.push_back(line_bits(line, base));
  if (std::all_of(lines.begin(), lines.end(), [&](int b) { return b == lines.front(); })) {
    if (lines.front() != base) out.set("fontStyle", std::to_string(lines.front()));
    return;
  }
  std::string list;
  for (int b : lines) list += (list.empty() ? "" : ",") + std::to_string(b);
  out.set("lineFontStyles", list);
}

/// The DiaDraw style for a draw.io style: the visual keys the renderer understands.
Style style_for(const Values& v, bool edge) {
  Style out;
  auto copy = [&](std::string_view from, std::string_view to) {
    const std::string* value = value_of(v, from);
    if (value && !value->empty()) out.set(std::string(to), *value);
  };
  copy("fillColor", "fill");
  copy("strokeColor", "stroke");
  for (const char* k : {"strokeWidth", "dashed", "opacity", "fontColor", "fontSize", "fontStyle"}) copy(k, k);
  if (edge) {
    for (const char* k : {"startArrow", "endArrow", "startFill", "endFill", "edgeStyle"}) copy(k, k);
  } else {
    // An edge's dash pattern stays out, as docs/decisions.md records; a shape's is its outline.
    for (const char* k : {"dashPattern", "rounded", "align", "verticalAlign", "labelPosition", "verticalLabelPosition",
                          "whiteSpace"})
      copy(k, k);
  }
  return out;
}

void count(std::vector<std::pair<std::string, int>>& into, const std::string& key) {
  for (auto& [k, n] : into)
    if (k == key) {
      ++n;
      return;
    }
  into.emplace_back(key, 1);
}

double round2(double v) { return js_round(v * 100) / 100; }
double round4(double v) { return js_round(v * 10000) / 10000; }

/// A UML lifeline (shape=umlLifeline), which takes connections on its line.
bool is_lifeline(const Cell& c) {
  const std::string* shape = value_of(c.style, "shape");
  const std::string* perimeter = value_of(c.style, "perimeter");
  return (shape && *shape == "umlLifeline") || (perimeter && *perimeter == "lifelinePerimeter") ||
         std::find(c.names.begin(), c.names.end(), "umlLifeline") != c.names.end();
}

Direction flow_direction(const std::vector<Cell>& cells, const std::map<std::string, const Cell*>& by_id) {
  double dx = 0, dy = 0;
  for (const auto& e : cells) {
    if (!e.edge) continue;
    auto geo = [&](const std::optional<std::string>& id) -> const Geo* {
      if (!id) return nullptr;
      auto it = by_id.find(*id);
      return it == by_id.end() || !it->second->geometry ? nullptr : &*it->second->geometry;
    };
    const Geo* a = geo(e.source);
    const Geo* c = geo(e.target);
    if (!a || !c) continue;
    dx += c->x + c->width / 2 - (a->x + a->width / 2);
    dy += c->y + c->height / 2 - (a->y + a->height / 2);
  }
  if (std::fabs(dx) > std::fabs(dy)) return dx >= 0 ? Direction::Right : Direction::Left;
  return dy >= 0 ? Direction::Down : Direction::Up;
}

}  // namespace

ImportResult import_drawio(std::string_view bytes, const DrawioOptions& options) {
  const bool keep = options.keep_geometry;
  ModelBuilder b;
  std::vector<std::pair<std::string, int>> unknown_shapes, unknown_styles;
  std::vector<std::pair<std::string, double>> stats{{"pages", 0},         {"vertices", 0}, {"edges", 0},
                                                    {"elements", 0},      {"relationships", 0}, {"anchors", 0},
                                                    {"hidden", 0},        {"stencils", 0}, {"artwork", 0}};
  auto stat = [&](std::string_view key) -> double& {
    for (auto& [k, v] : stats)
      if (k == key) return v;
    return stats.front().second;
  };
  const DrawioIndex* stencil_index = options.stencils;
  auto finish = [&] {
    ImportResult r = b.result();
    r.report.unknown_shapes = unknown_shapes;
    r.report.unknown_styles = unknown_styles;
    r.report.stats = stats;
    return r;
  };

  DrawioFile file = read_drawio_file(bytes, options.max_inflated);
  if (!file.xml) {
    b.drop(file.problem.value_or("Not a draw.io file"));
    return finish();
  }

  auto doc = parse_xml(*file.xml);
  std::vector<std::unique_ptr<XmlNode>> inflated;  // keeps decompressed pages alive
  // Everything one import decompresses (the file, then each compressed page) shares one budget,
  // counted before URI decoding, so many pages cannot add up to more than one could.
  std::size_t budget = options.max_inflated - std::min(options.max_inflated, file.inflated);
  struct Page {
    std::string name;
    const XmlNode* model;
  };
  std::vector<Page> pages;
  walk_xml(*doc, [&](const XmlNode& node) {
    if (node.name != "diagram") return;
    std::string name = node.attr("name") ? *node.attr("name") : "Page-" + std::to_string(pages.size() + 1);
    if (const XmlNode* inline_model = node.child("mxGraphModel")) {
      pages.push_back({name, inline_model});
    } else if (!js_trim(node.text).empty()) {
      std::size_t page_size = 0;
      auto xml = inflate_page(node.text, budget, &page_size);
      if (!xml) {
        if (is_too_large(xml.error()))
          b.drop("Page \"" + name + "\" could not be decompressed: the file would decompress to more than " +
                 describe_size(options.max_inflated) + " in all");
        else
          b.drop("Page \"" + name + "\" could not be decompressed");
        return;
      }
      budget -= std::min(budget, page_size);
      auto inner = parse_xml(*xml);
      const XmlNode* model = inner->child("mxGraphModel");
      if (model) {
        pages.push_back({name, model});
        inflated.push_back(std::move(inner));
      } else {
        b.drop("Page \"" + name + "\" has no diagram");
      }
    }
  });
  if (pages.empty()) {
    const XmlNode* bare = nullptr;
    walk_xml(*doc, [&](const XmlNode& n) {
      if (!bare && n.name == "mxGraphModel") bare = &n;
    });
    if (bare) pages.push_back({"Page-1", bare});
  }
  if (pages.empty()) b.drop("No draw.io diagram found; nothing imported");

  bool collapsed_seen = false;
  for (std::size_t page_index = 0; page_index < pages.size(); ++page_index) {
    const Page& page = pages[page_index];
    stat("pages")++;
    std::vector<Cell> cells = cells_of(*page.model);
    std::map<std::string, const Cell*> by_id;
    for (const auto& c : cells) by_id[c.id] = &c;  // a later cell with the same id wins, as in a Map
    auto find = [&](const std::optional<std::string>& id) -> const Cell* {
      if (!id) return nullptr;
      auto it = by_id.find(*id);
      return it == by_id.end() ? nullptr : it->second;
    };
    auto key = [&](const std::string& id) { return std::to_string(page_index) + ":" + id; };
    // Any parent that is neither a vertex nor an edge is a layer (or the root).
    auto is_layer = [&](const std::optional<std::string>& id) {
      const Cell* c = find(id);
      return !c || (!c->vertex && !c->edge);
    };
    std::vector<const Cell*> layers;
    for (const auto& c : cells)
      if (!c.vertex && !c.edge && c.parent && by_id.count(*c.parent)) layers.push_back(&c);
    auto layer_of = [&](const Cell& c) -> const Cell* {
      const Cell* cur = &c;
      int guard = 0;
      while (cur && !is_layer(cur->parent) && guard++ < 10000) cur = find(cur->parent);
      return cur && cur->parent ? find(cur->parent) : nullptr;
    };
    // A vertex's position in its parent. A relative one (an icon pinned to its container's
    // corner) sits at a fraction of the parent's size plus its offset.
    auto local = [&](const Cell& c) -> Point2 {
      const Geo& g = *c.geometry;
      if (!g.relative) return {g.x, g.y};
      const Cell* parent = find(c.parent);
      if (!parent || !parent->vertex || !parent->geometry) return {0, 0};
      return {g.x * parent->geometry->width + g.offset.x, g.y * parent->geometry->height + g.offset.y};
    };
    // Absolute position of a vertex's origin: the sum of its containers' positions.
    auto origin = [&](const std::optional<std::string>& id) {
      Point2 o{0, 0};
      int guard = 0;
      for (const Cell* cur = find(id); cur && cur->vertex && guard++ < 10000; cur = find(cur->parent)) {
        if (cur->geometry) {
          Point2 at = local(*cur);
          o.x += at.x;
          o.y += at.y;
        }
      }
      return o;
    };
    auto hidden = [&](const Cell& c) {
      if (!c.visible) return true;
      int guard = 0;
      for (const Cell* cur = find(c.parent.value_or("")); cur && guard++ < 10000; cur = find(cur->parent.value_or(""))) {
        if (!cur->visible) return true;
        if (cur->vertex && cur->collapsed) {
          collapsed_seen = true;
          return true;
        }
      }
      return false;
    };

    // Labels that sit on edges (child vertices of an edge) belong to that edge.
    std::map<std::string, std::string> edge_labels;
    std::vector<const Cell*> vertices;
    for (const auto& c : cells) {
      if (!c.vertex) continue;
      const Cell* parent = find(c.parent);
      if (parent && parent->edge) {
        std::string text = label_of(c);
        if (!text.empty()) {
          auto& joined = edge_labels[parent->id];
          joined = joined.empty() ? text : joined + " " + text;
        }
        continue;
      }
      vertices.push_back(&c);
    }
    stat("vertices") += static_cast<double>(vertices.size());
    std::set<std::string, std::less<>> containers;
    for (const Cell* v : vertices)
      if (v->parent && !is_layer(v->parent)) containers.insert(*v->parent);

    std::vector<std::string> include;
    std::vector<LayoutHint> hints;
    std::function<int(const Cell&, int)> depth = [&](const Cell& c, int guard) -> int {
      if (is_layer(c.parent) || guard > 10000) return 0;
      return 1 + depth(*find(c.parent), guard + 1);
    };
    std::vector<const Cell*> ordered = vertices;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const Cell* a, const Cell* c) { return depth(*a, 0) < depth(*c, 0); });
    for (const Cell* vp : ordered) {
      const Cell& v = *vp;
      ShapeInfo shape = shape_for(v.names, v.style);
      if (shape.unknown && shape.name && !shape.name->empty()) count(unknown_shapes, *shape.name);
      // Stencils draw with draw.io's own artwork from the drawio-* libraries.
      std::optional<std::pair<std::string, Style>> artwork;
      if (shape.stencil && stencil_index) {
        // Tiles: AWS resource/product icons and Cisco 2019 icons draw a glyph on a coloured tile.
        const std::string* icon = value_of(v.style, "resIcon");
        if (!icon) icon = value_of(v.style, "prIcon");
        const std::string* style_shape = value_of(v.style, "shape");
        static const std::regex tile_re("^mxgraph\\.(aws4\\.(resourceIcon|productIcon)|cisco19\\.rect)$", std::regex::icase);
        bool tile = icon && std::regex_search(style_shape ? *style_shape : std::string(), tile_re);
        std::optional<std::string> name;
        if (tile)
          name = icon->find('.') != std::string::npos ? *icon : style_shape->substr(0, style_shape->rfind('.')) + "." + *icon;
        else if (style_shape)
          name = *style_shape;
        if (name && !name->empty()) {
          std::string lower = *name;
          for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          auto hit = stencil_index->styles.find(lower);
          if (hit != stencil_index->styles.end() && hit->second.first < stencil_index->sets.size()) {
            const auto& set = stencil_index->sets[hit->second.first];
            std::string major = set.version.substr(0, set.version.find('.'));
            std::string alias = set.name.rfind("drawio-", 0) == 0 ? set.name.substr(7) : set.name;
            alias = std::regex_replace(alias, std::regex("[^A-Za-z0-9_]+"), "_");
            alias = b.use_library(set.name, "^" + major, alias);
            Style style = style_for(v.style, false);
            if (tile) {
              // A resource icon: the glyph in the stroke colour on a tile of the fill colour.
              Style tiled;
              const std::string* fill = style.find("fill");
              std::optional<std::string> icon_background;
              if (fill && !fill->empty()) icon_background = *fill;
              const std::string* stroke_color = value_of(v.style, "strokeColor");
              bool fill_set = false;
              for (const auto& [k, val] : style) {
                if (k == "stroke") continue;
                if (k == "fill") {
                  tiled.set("fill", stroke_color ? *stroke_color : "#ffffff");
                  fill_set = true;
                } else {
                  tiled.set(k, val);
                }
              }
              if (icon_background) tiled.set("iconBackground", *icon_background);
              if (!fill_set) tiled.set("fill", stroke_color ? *stroke_color : "#ffffff");
              style = std::move(tiled);
            }
            const std::string* aspect = value_of(v.style, "aspect");
            if (aspect && *aspect == "fixed") style.set("aspect", "fixed");
            artwork = std::pair{alias + "." + hit->second.second, std::move(style)};
            stat("artwork")++;
          }
        }
      }
      if (shape.stencil && !artwork) stat("stencils")++;
      for (const auto& [k, _] : v.style)
        if (!is_known_style_key(k)) count(unknown_styles, k);
      std::optional<std::string> container = is_layer(v.parent) ? std::nullopt : v.parent;
      std::string label = label_of(v);
      const Cell* layer = layer_of(v);
      Properties properties;
      properties.set("drawio.id", v.id);
      properties.set("drawio.style", v.style_text);
      const std::string* html = value_of(v.style, "html");
      if (html && *html == "1" && has_tag(v.value)) properties.set("drawio.label", v.value);
      if (layers.size() > 1 && layer) properties.set("drawio.layer", layer->value.empty() ? layer->id : layer->value);
      for (const auto& [k, val] : v.data) properties.set(k, val);
      ElementInit init;
      init.kind = shape.kind;
      init.name = label;
      std::string first_line = label.substr(0, label.find('\n'));
      std::string shape_tail;
      if (shape.name) {
        auto dot = shape.name->rfind('.');
        shape_tail = dot == std::string::npos ? *shape.name : shape.name->substr(dot + 1);
      }
      init.local = !first_line.empty() ? first_line : !shape_tail.empty() ? shape_tail : "shape";
      init.shape = artwork ? artwork->first : shape.base;
      init.style = artwork ? artwork->second : style_for(v.style, false);
      if (!artwork) {
        add_shape_style(*init.style, v, shape.base);
        add_line_font_styles(*init.style, v, label);
        // draw.io centres a container's label unless its style says otherwise; DiaDraw would put
        // it in a header at the top.
        // Tables, lifelines and anything with a startSize keep their label in a header, as draw.io
        // draws them.
        const std::string* start = value_of(v.style, "startSize");
        const bool header = (shape.name && (*shape.name == "table" || *shape.name == "umlLifeline")) ||
                            (start && js_to_number(*start) > 0);
        if (containers.count(v.id) && !header && !value_of(v.style, "verticalAlign") && shape.base != "swimlane" &&
            shape.base != "frame" && shape.base != "invisible")
          init.style->set("verticalAlign", "middle");
      }
      init.properties = std::move(properties);
      if (container && b.has(key(*container))) init.parent = key(*container);
      // A vertex placed relative to its container (an icon on a component's corner) stays on that
      // spot of it: an adornment, kept there when the container moves or is resized.
      const Cell* container_cell = container ? find(container) : nullptr;
      const bool adornment = init.parent && v.geometry && v.geometry->relative && container_cell &&
                             container_cell->vertex && container_cell->geometry;
      if (adornment) {
        if (!init.style) init.style = Style{};
        init.style->set("anchor", js_number_string(v.geometry->x) + "," + js_number_string(v.geometry->y));
        if (v.geometry->offset.x != 0 || v.geometry->offset.y != 0)
          init.style->set("anchorOffset",
                          js_number_string(v.geometry->offset.x) + "," + js_number_string(v.geometry->offset.y));
      }
      ElementId id = b.add_element(key(v.id), std::move(init));
      stat("elements")++;
      if (hidden(v))
        stat("hidden")++;
      else
        include.push_back(key(v.id));
      if (keep && v.geometry && adornment) {
        hints.emplace_back(SizeHint{id, round2(v.geometry->width), round2(v.geometry->height)});
      } else if (keep && v.geometry) {
        Point2 o = origin(container);
        Point2 at = local(v);
        hints.emplace_back(PinAtHint{id, round2(o.x + at.x), round2(o.y + at.y),
                                     round2(v.geometry->width), round2(v.geometry->height)});
      }
    }

    for (const auto& e : cells) {
      if (!e.edge) continue;
      stat("edges")++;
      for (const auto& [k, _] : e.style)
        if (!is_known_style_key(k)) count(unknown_styles, k);
      Point2 offset = origin(is_layer(e.parent) ? std::nullopt : e.parent);
      auto at = [&](const Point2& p) { return Point2{round2(offset.x + p.x), round2(offset.y + p.y)}; };
      // Whether each end is its cell's element (not a loose end's anchor).
      bool attached_from = false, attached_to = false;
      auto end = [&](const std::optional<std::string>& cell_id, const std::optional<Point2>& point, const char* which,
                     bool& attached) -> std::optional<std::string> {
        if (cell_id) {
          std::string k = key(*cell_id);
          if (b.has(k)) {
            attached = true;
            return k;
          }
        }
        if (!keep || !point) return std::nullopt;
        // A loose end: an invisible anchor keeps the connector where it was drawn.
        std::string anchor_key = key(e.id + "#" + which);
        Point2 p = at(*point);
        ElementInit init;
        init.kind = "note";
        init.name = "";
        init.local = "anchor";
        init.shape = "invisible";
        init.properties = Properties{{"drawio.anchor", "1"}};
        ElementId anchor = b.add_element(anchor_key, std::move(init));
        hints.emplace_back(PinAtHint{anchor, p.x, p.y, 0.0, 0.0});
        if (!hidden(e)) include.push_back(anchor_key);
        stat("anchors")++;
        return anchor_key;
      };
      auto from = end(e.source, e.geometry ? e.geometry->source_point : std::nullopt, "source", attached_from);
      auto to = end(e.target, e.geometry ? e.geometry->target_point : std::nullopt, "target", attached_to);
      if (!from || !to) {
        b.drop("Connector " + e.id + " on \"" + page.name + "\" is not attached at both ends");
        continue;
      }
      std::string label = label_of(e);
      if (auto el = edge_labels.find(e.id); el != edge_labels.end() && !el->second.empty())
        label = label.empty() ? el->second : label + " " + el->second;
      Properties properties;
      properties.set("drawio.id", e.id);
      properties.set("drawio.style", e.style_text);
      for (const auto& [k, val] : e.data) properties.set(k, val);
      const std::string* start = value_of(e.style, "startArrow");
      const std::string* end_style = value_of(e.style, "endArrow");
      std::string end_arrow = end_style ? *end_style : "classic";
      const std::string* dashed = value_of(e.style, "dashed");
      RelationshipInit rel;
      if (!label.empty()) rel.label = label;
      rel.line = dashed && *dashed == "1" ? LineStyle::Dashed : LineStyle::Solid;
      rel.bidirectional = start && !start->empty() && *start != "none" && end_arrow != "none";
      rel.style = style_for(e.style, true);
      // Which side of its spot the label sits on; an unlabelled edge has no label to place.
      if (!label.empty())
        for (const char* k : {"align", "verticalAlign"})
          if (const std::string* v = value_of(e.style, k); v && !v->empty()) rel.style->set(k, *v);
      // The label's spot: draw.io's relative x along the line (-1..1), its y off it, its offset.
      if (e.geometry && !label.empty()) {
        const Geo& g = *e.geometry;
        // Within the bounds the layout reads (label_anchor_of), so neither number is dropped.
        if (std::isfinite(g.x) && std::isfinite(g.y) && (g.x != 0 || g.y != 0))
          rel.style->set("labelAt", js_number_string(std::clamp(g.x, -1.0, 1.0)) +
                                        (g.y != 0 ? "," + js_number_string(std::clamp(g.y, -1e6, 1e6)) : std::string()));
        if (std::isfinite(g.offset.x) && std::isfinite(g.offset.y) && (g.offset.x != 0 || g.offset.y != 0))
          rel.style->set("labelOffset", js_number_string(std::clamp(g.offset.x, -1e6, 1e6)) + "," +
                                            js_number_string(std::clamp(g.offset.y, -1e6, 1e6)));
      }
      rel.properties = std::move(properties);
      auto rid = b.add_relationship(*from, *to, std::move(rel));
      if (!rid) continue;
      stat("relationships")++;
      if (keep && e.geometry) {
        auto num = [&](std::string_view k) -> std::optional<double> {
          const std::string* v = value_of(e.style, k);
          if (!v) return std::nullopt;
          double d = js_to_number(*v);
          return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
        };
        auto pair = [&](std::string_view x, std::string_view y) -> std::optional<Point2> {
          auto a = num(x), c = num(y);
          if (!a || !c) return std::nullopt;
          return Point2{*a, *c};
        };
        auto exit = pair("exitX", "exitY");
        auto entry = pair("entryX", "entryY");
        std::vector<Point2> points;
        for (const auto& p : e.geometry->points) points.push_back(at(p));
        const std::string* edge_style = value_of(e.style, "edgeStyle");
        const bool message = edge_style && *edge_style == "sequenceEdgeStyle" && attached_from && attached_to;
        // Only messages pay for looking their terminals up.
        const Cell* source = message ? find(e.source) : nullptr;
        const Cell* target = message ? find(e.target) : nullptr;
        if (source && target && source->vertex && target->vertex && source->geometry && target->geometry) {
          // A UML message: routed as draw.io routes it, its ends kept as fractions of their boxes.
          auto end_of = [&](const Cell& c, std::string_view prefix) {
            const Point2 o = origin(c.parent);  // a layer adds nothing: it is no vertex
            const Point2 l = local(c);
            MessageEnd m;
            m.x = o.x + l.x;
            m.y = o.y + l.y;
            m.width = c.geometry->width;
            m.height = c.geometry->height;
            m.lifeline = is_lifeline(c);
            if (m.lifeline) {
              const std::string* size = value_of(c.style, "size");
              // As the renderer reads it: a missing, negative or unreadable size is draw.io's 40.
              const double head = size && !size->empty() ? js_to_number(*size) : 40;
              m.head = std::isfinite(head) && head >= 0 ? head : 40;
            }
            const std::string p(prefix);
            if (auto fx = pair(p + "X", p + "Y"))
              m.pinned = Point2{m.x + fx->x * m.width + num(p + "Dx").value_or(0),
                                m.y + fx->y * m.height + num(p + "Dy").value_or(0)};
            return m;
          };
          const MessageEnd s = end_of(*source, "exit");
          const MessageEnd t = end_of(*target, "entry");
          // draw.io's lifeline of a terminal: the terminal or the lifeline it sits on.
          auto lifeline_of = [&](const Cell& c, bool lifeline) {
            if (lifeline) return &c;
            const Cell* parent = find(c.parent);
            return parent && parent->vertex && is_lifeline(*parent) ? parent : &c;
          };
          if (auto route = message_route(s, t, lifeline_of(*source, s.lifeline) == lifeline_of(*target, t.lifeline), points)) {
            auto fraction = [](const MessageEnd& m, Point2 p) {
              return Point2{m.width > 0 ? round4((p.x - m.x) / m.width) : 0.5,
                            m.height > 0 ? round4((p.y - m.y) / m.height) : 0.5};
            };
            for (auto& p : route->via) p = Point2{round2(p.x), round2(p.y)};
            hints.emplace_back(RouteHint{*rid, std::move(route->via), fraction(s, route->start), fraction(t, route->end)});
            continue;
          }
        }
        if (!points.empty() || exit || entry) hints.emplace_back(RouteHint{*rid, std::move(points), exit, entry});
      }
    }

    if (include.empty()) {
      b.note("Page \"" + page.name + "\" is empty and has no view");
      continue;
    }
    ViewInit view;
    view.algorithm = keep ? "fixed" : "layered";
    view.direction = keep ? Direction::Down : flow_direction(cells, by_id);
    view.include = std::move(include);
    view.hints = std::move(hints);
    b.add_view(page.name, std::move(view));
  }

  auto js_count = [](double n) { return js_number_string(n); };
  if (stat("hidden") > 0)
    b.note(js_count(stat("hidden")) + " shapes on hidden layers or in collapsed containers are imported but not shown");
  if (collapsed_seen) b.note("Collapsed containers are shown collapsed");
  if (stat("anchors") > 0) b.note(js_count(stat("anchors")) + " loose connector ends were kept with invisible anchors");
  if (!unknown_shapes.empty()) {
    std::string list;
    for (std::size_t i = 0; i < unknown_shapes.size() && i < 10; ++i) list += (i ? ", " : "") + unknown_shapes[i].first;
    b.note("Unknown shapes were drawn as rectangles: " + list);
  }
  if (stat("artwork") > 0)
    b.note(js_count(stat("artwork")) +
           " stencil shapes are drawn with draw.io's artwork from the drawio-* libraries (Apache-2.0; not for use in "
           "Atlassian products; vendor icons are the vendors' trademarks)");
  if (stat("stencils") > 0)
    b.note(js_count(stat("stencils")) + " stencil shapes (icons from shape libraries) are drawn as plain icon boxes");
  if (!keep) b.note("Positions and sizes were not imported; DiaDraw lays the diagram out");
  return finish();
}

}  // namespace dd::io
