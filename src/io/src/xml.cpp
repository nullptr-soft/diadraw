// Port of `parseXml` in packages/import/src/drawio.ts. The TypeScript version is one global
// regular expression with these alternatives, tried in order at each position:
//   <!--…-->   <?…?>   <![CDATA[…]]>   <!DOCTYPE…>   a start/end tag   a run of text
// Where none matches (a stray `<`), the search moves on by one character. This scanner does the
// same by hand, which keeps it linear and free of regex stack limits on large files.

#include "diadraw/io/xml.hpp"

#include "diadraw/io/builder.hpp"

namespace dd::io {

namespace {

bool is_name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' ||
         c == '.' || c == '-';
}

/// JavaScript `\s` over the ASCII range (XML markup is ASCII where it matters).
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

struct Tag {
  bool closing = false;
  bool self_closing = false;
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::size_t end = 0;  ///< position after `>`
};

/// `<(\/?)([\w:.-]+)((?:\s+[\w:.-]+\s*=\s*(?:"[^"]*"|'[^']*'))*)\s*(\/?)>` at `at`.
bool read_tag(std::string_view s, std::size_t at, Tag& tag) {
  std::size_t i = at + 1;
  if (i < s.size() && s[i] == '/') {
    tag.closing = true;
    ++i;
  }
  std::size_t name_start = i;
  while (i < s.size() && is_name_char(s[i])) ++i;
  if (i == name_start) return false;
  tag.name = std::string(s.substr(name_start, i - name_start));
  // Attributes: each needs leading whitespace, a name, `=` and a quoted value.
  while (true) {
    std::size_t j = i;
    while (j < s.size() && is_space(s[j])) ++j;
    if (j == i) break;
    std::size_t an = j;
    while (j < s.size() && is_name_char(s[j])) ++j;
    if (j == an) break;
    std::string attr_name(s.substr(an, j - an));
    while (j < s.size() && is_space(s[j])) ++j;
    if (j >= s.size() || s[j] != '=') break;
    ++j;
    while (j < s.size() && is_space(s[j])) ++j;
    if (j >= s.size() || (s[j] != '"' && s[j] != '\'')) break;
    char quote = s[j];
    auto close = s.find(quote, j + 1);
    if (close == std::string_view::npos) break;
    std::string value = decode_entities(s.substr(j + 1, close - j - 1));
    bool replaced = false;
    for (auto& [k, v] : tag.attrs)
      if (k == attr_name) {
        v = value;
        replaced = true;
      }
    if (!replaced) tag.attrs.emplace_back(std::move(attr_name), std::move(value));
    i = close + 1;
  }
  while (i < s.size() && is_space(s[i])) ++i;
  if (i < s.size() && s[i] == '/') {
    tag.self_closing = true;
    ++i;
  }
  if (i >= s.size() || s[i] != '>') return false;
  tag.end = i + 1;
  return true;
}

bool starts(std::string_view s, std::size_t at, std::string_view prefix) {
  return s.size() >= at + prefix.size() && s.compare(at, prefix.size(), prefix) == 0;
}

}  // namespace

std::unique_ptr<XmlNode> parse_xml(std::string_view xml) {
  auto root = std::make_unique<XmlNode>();
  root->name = "#document";
  std::vector<XmlNode*> stack{root.get()};
  std::size_t i = 0;
  while (i < xml.size()) {
    XmlNode* top = stack.back();
    if (xml[i] != '<') {
      auto next = xml.find('<', i);
      if (next == std::string_view::npos) next = xml.size();
      top->text += decode_entities(xml.substr(i, next - i));
      i = next;
      continue;
    }
    if (starts(xml, i, "<!--")) {
      auto end = xml.find("-->", i + 4);
      if (end != std::string_view::npos) {
        i = end + 3;
        continue;
      }
    }
    if (starts(xml, i, "<?")) {
      auto end = xml.find("?>", i + 2);
      if (end != std::string_view::npos) {
        i = end + 2;
        continue;
      }
    }
    if (starts(xml, i, "<![CDATA[")) {
      auto end = xml.find("]]>", i + 9);
      if (end != std::string_view::npos) {
        top->text += std::string(xml.substr(i + 9, end - i - 9));
        i = end + 3;
        continue;
      }
    }
    if (starts(xml, i, "<!DOCTYPE")) {
      auto end = xml.find('>', i);
      if (end != std::string_view::npos) {
        i = end + 1;
        continue;
      }
    }
    Tag tag;
    if (read_tag(xml, i, tag)) {
      i = tag.end;
      if (tag.closing) {
        if (stack.size() > 1) stack.pop_back();
        continue;
      }
      auto node = std::make_unique<XmlNode>();
      node->name = std::move(tag.name);
      node->attrs = std::move(tag.attrs);
      XmlNode* raw = node.get();
      top->children.push_back(std::move(node));
      if (!tag.self_closing) stack.push_back(raw);
      continue;
    }
    // A stray `<`: nothing matches here, so the search resumes at the next character.
    ++i;
  }
  return root;
}

}  // namespace dd::io
