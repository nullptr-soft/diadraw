// Port of packages/library/src: semver.ts, manifest.ts, bundle.ts and set.ts.

#include "diadraw/library/library.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/library/builtin.hpp"
#include "diadraw/model/json.hpp"

namespace dd::library {

using js::Regex;

namespace {

std::string trim(std::string_view s) { return std::string(js_trim(s)); }

/// JavaScript `String(v)` of a JSON value; `undefined` when missing.
std::string js_string(const Json* v) {
  if (!v) return "undefined";
  if (v->is_null()) return "null";
  if (v->is_bool()) return v->as_bool() ? "true" : "false";
  if (v->is_number()) return js_number_string(v->as_number());
  if (v->is_string()) return v->as_string();
  if (v->is_array()) {
    std::string out;
    for (std::size_t i = 0; i < v->as_array().size(); ++i) {
      if (i) out += ",";
      const Json& x = v->as_array()[i];
      if (!x.is_null()) out += js_string(&x);
    }
    return out;
  }
  return "[object Object]";
}
/// JavaScript `Number(v)`.
double js_number(const Json* v) {
  if (!v) return std::nan("");
  if (v->is_null()) return 0;
  if (v->is_bool()) return v->as_bool() ? 1 : 0;
  if (v->is_number()) return v->as_number();
  if (v->is_string()) return js_to_number(v->as_string());
  if (v->is_array()) return js_to_number(js_string(v));
  return std::nan("");
}
/// JavaScript truthiness of a JSON value.
bool truthy(const Json* v) {
  if (!v || v->is_null()) return false;
  if (v->is_bool()) return v->as_bool();
  if (v->is_number()) return v->as_number() != 0 && !std::isnan(v->as_number());
  if (v->is_string()) return !v->as_string().empty();
  return true;
}
const std::string* string_of(const Json* v) { return v && v->is_string() ? &v->as_string() : nullptr; }
const Json::Object& members(const Json* v) {
  static const Json::Object kEmpty;
  return v && v->is_object() ? v->as_object() : kEmpty;
}

/// `parseFloat(text)`.
double parse_float(std::string_view text) {
  static const Regex kFloat(R"re(^[+-]?(Infinity|(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?))re");
  std::string_view t = js_trim(text);
  t = text.substr(static_cast<std::size_t>(t.data() - text.data()));
  auto m = kFloat.exec(t);
  if (!m) return std::nan("");
  return js_to_number(m->view(0));
}

std::string title_case(std::string_view id) {
  static const Regex kUnderscore("_", "g");
  static const Regex kCamel("([a-z])([A-Z])", "g");
  static const Regex kFirst("^\\w");
  std::string s = kUnderscore.replace(id, " ", true);
  s = kCamel.replace(s, "$1 $2", true);
  return kFirst.replace(s, [](const js::Match& m) { return js::to_upper(m.view(0)); }, false);
}

// ── semver internals ───────────────────────────────────────────────────────────────────────────

enum class Op { Lt, Le, Gt, Ge, Eq };
struct Comparator {
  Op op;
  Version v;
};

Version v3(double major, double minor = 0, double patch = 0) { return {major, minor, patch, {}}; }

struct Partial {
  std::optional<double> parts[3];
  std::string pre;
};

std::optional<Partial> partial(std::string_view text) {
  static const Regex kPartial(R"re(^v?(\d+|x|X|\*)?(?:\.(\d+|x|X|\*))?(?:\.(\d+|x|X|\*))?(?:-([0-9A-Za-z.-]+))?$)re");
  auto m = kPartial.exec(text);
  if (!m) return std::nullopt;
  Partial p;
  for (int i = 0; i < 3; ++i) {
    if (!m->has(static_cast<std::size_t>(i + 1))) continue;
    std::string s = m->str(static_cast<std::size_t>(i + 1));
    if (s == "x" || s == "X" || s == "*") continue;
    p.parts[i] = js_to_number(s);
  }
  p.pre = m->str(4);
  return p;
}

std::string fill(std::string_view text) {
  std::string t(text);
  if (!t.empty() && t[0] == 'v') t.erase(0, 1);
  std::vector<std::string> p;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= t.size(); ++i)
    if (i == t.size() || t[i] == '.') {
      p.push_back(t.substr(start, i - start));
      start = i + 1;
    }
  while (p.size() < 3) p.emplace_back("0");
  std::string out;
  for (std::size_t i = 0; i < p.size(); ++i) out += (i ? "." : "") + p[i];
  return out;
}

/// The upper bound of a partial version: `<=1.2` means `<1.3.0`, `<1.2` means `<1.2.0`.
std::vector<Comparator> upper(const std::optional<double> (&parts)[3], Op op) {
  const auto& a = parts[0];
  const auto& b = parts[1];
  const auto& c = parts[2];
  if (!a) return {};
  if (!b) return {{Op::Lt, op == Op::Le ? v3(*a + 1) : v3(*a)}};
  if (!c) return {{Op::Lt, op == Op::Le ? v3(*a, *b + 1) : v3(*a, *b)}};
  return {{op, v3(*a, *b, *c)}};
}

std::optional<std::vector<Comparator>> comparators(std::string_view term) {
  static const Regex kSpaces("\\s+");
  static const Regex kOp("^(\\^|~|>=|<=|>|<|=)?(.*)$");
  std::vector<std::string> tokens;
  for (auto& t : kSpaces.split(js_trim(term)))
    if (!t.empty()) tokens.push_back(t);
  std::vector<Comparator> out;
  // `1.2 - 2.3` hyphen ranges.
  if (tokens.size() == 3 && tokens[1] == "-") {
    auto lo = parse_version(fill(tokens[0]));
    auto hi = partial(tokens[2]);
    if (!lo || !hi) return std::nullopt;
    out.push_back({Op::Ge, *lo});
    for (auto& c : upper(hi->parts, Op::Le)) out.push_back(c);
    return out;
  }
  for (const auto& token : tokens) {
    auto m = kOp.exec(token);
    if (!m) return std::nullopt;
    std::string op = m->str(1);
    auto p = partial(m->view(2));
    if (!p) return std::nullopt;
    const auto& a = p->parts[0];
    const auto& b = p->parts[1];
    const auto& c = p->parts[2];
    const bool exact = a && b && c;
    Version base = exact ? *parse_version(js_number_string(*a) + "." + js_number_string(*b) + "." +
                                         js_number_string(*c) + (p->pre.empty() ? "" : "-" + p->pre))
                         : v3(a.value_or(0), b.value_or(0), c.value_or(0));
    if (!a) {
      if (op == "<" || op == ">") return std::vector<Comparator>{{Op::Lt, v3(0)}};  // nothing
      continue;                                                                       // `*`
    }
    if (op == "^") {
      out.push_back({Op::Ge, base});
      out.push_back({Op::Lt, *a > 0 || !b ? v3(*a + 1) : *b > 0 || !c ? v3(0, *b + 1) : v3(0, 0, *c + 1)});
    } else if (op == "~") {
      out.push_back({Op::Ge, base});
      out.push_back({Op::Lt, !b ? v3(*a + 1) : v3(*a, *b + 1)});
    } else if (op == ">=" || op == ">") {
      if (exact || op == ">=") out.push_back({op == ">=" ? Op::Ge : Op::Gt, base});
      else out.push_back({Op::Ge, !b ? v3(*a + 1) : v3(*a, *b + 1)});
    } else if (op == "<") {
      out.push_back({Op::Lt, base});
    } else if (op == "<=") {
      for (auto& u : upper(p->parts, Op::Le)) out.push_back(u);
    } else if (exact) {
      out.push_back({Op::Eq, base});
    } else {
      out.push_back({Op::Ge, base});
      for (auto& u : upper(p->parts, Op::Le)) out.push_back(u);
    }
  }
  return out;
}

bool test(const Comparator& c, const Version& x) {
  int d = compare_versions(x, c.v);
  switch (c.op) {
    case Op::Lt: return d < 0;
    case Op::Le: return d <= 0;
    case Op::Gt: return d > 0;
    case Op::Ge: return d >= 0;
    case Op::Eq: return d == 0;
  }
  return false;
}

std::vector<std::string> split_on(std::string_view s, std::string_view sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (;;) {
    auto at = s.find(sep, start);
    if (at == std::string_view::npos) {
      out.emplace_back(s.substr(start));
      return out;
    }
    out.emplace_back(s.substr(start, at - start));
    start = at + sep.size();
  }
}

}  // namespace

// ── semver ─────────────────────────────────────────────────────────────────────────────────────

std::optional<Version> parse_version(std::string_view text) {
  static const Regex kVersion(R"re(^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)re");
  static const Regex kDigits("^\\d+$");
  auto m = kVersion.exec(js_trim(text));
  if (!m) return std::nullopt;
  Version v{js_to_number(m->view(1)), js_to_number(m->view(2)), js_to_number(m->view(3)), {}};
  if (m->has(4) && !m->view(4).empty())
    for (const auto& p : split_on(m->view(4), ".")) {
      if (kDigits.test(p)) v.pre.emplace_back(js_to_number(p));
      else v.pre.emplace_back(p);
    }
  return v;
}

bool is_version(std::string_view text) { return parse_version(text).has_value(); }

int compare_versions(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  // A pre-release sorts before its release.
  if (a.pre.empty() || b.pre.empty()) {
    auto d = static_cast<long>(b.pre.size()) - static_cast<long>(a.pre.size());
    return d < 0 ? -1 : d > 0 ? 1 : 0;
  }
  for (std::size_t i = 0; i < std::max(a.pre.size(), b.pre.size()); ++i) {
    if (i >= a.pre.size()) return -1;
    if (i >= b.pre.size()) return 1;
    const auto& x = a.pre[i];
    const auto& y = b.pre[i];
    if (x == y) continue;
    const double* xn = std::get_if<double>(&x);
    const double* yn = std::get_if<double>(&y);
    if (xn && yn) return *xn < *yn ? -1 : 1;
    if (xn) return -1;
    if (yn) return 1;
    return std::get<std::string>(x) < std::get<std::string>(y) ? -1 : 1;
  }
  return 0;
}

bool is_valid_range(std::string_view range) {
  for (const auto& term : split_on(range, "||"))
    if (!comparators(term)) return false;
  return true;
}

bool satisfies(std::string_view version, std::string_view range) {
  auto x = parse_version(version);
  if (!x) return false;
  for (const auto& term : split_on(range, "||")) {
    auto cs = comparators(term);
    if (!cs) continue;
    // Pre-releases only match a range that names a pre-release of the same version.
    if (!x->pre.empty() && std::none_of(cs->begin(), cs->end(), [&](const Comparator& c) {
          return !c.v.pre.empty() && c.v.major == x->major && c.v.minor == x->minor && c.v.patch == x->patch;
        }))
      continue;
    if (std::all_of(cs->begin(), cs->end(), [&](const Comparator& c) { return test(c, *x); })) return true;
  }
  return false;
}

std::optional<std::string> max_satisfying(const std::vector<std::string>& versions, std::string_view range) {
  std::vector<std::string> ok;
  for (const auto& v : versions)
    if (satisfies(v, range)) ok.push_back(v);
  if (ok.empty()) return std::nullopt;
  std::stable_sort(ok.begin(), ok.end(), [](const std::string& a, const std::string& b) {
    return compare_versions(*parse_version(a), *parse_version(b)) < 0;
  });
  return ok.back();
}

// ── manifests ──────────────────────────────────────────────────────────────────────────────────

bool is_library_name(std::string_view name) {
  static const Regex kName("^(@[a-z0-9][a-z0-9._-]*\\/)?[a-z0-9][a-z0-9._-]*$");
  return kName.test(name) && utf16_length(name) <= 214;
}

std::vector<ManifestIssue> validate_manifest(const Json& m) {
  static const Regex kShapeId("^[A-Za-z_][A-Za-z0-9_]*$");
  std::vector<ManifestIssue> issues;
  auto error = [&](std::string message, std::optional<std::string> path = std::nullopt) {
    issues.push_back({true, std::move(message), path && !path->empty() ? path : std::nullopt});
  };
  auto warn = [&](std::string message, std::optional<std::string> path = std::nullopt) {
    issues.push_back({false, std::move(message), path && !path->empty() ? path : std::nullopt});
  };
  if (!m.is_object() && !m.is_array()) {
    error("The manifest is not a JSON object");
    return issues;
  }
  const std::string* name = string_of(m.get("name"));
  if (!name || !is_library_name(*name))
    error("name must be lowercase letters, digits, \"-\", \".\" or \"_\", optionally @scope/", "name");
  const std::string* version = string_of(m.get("version"));
  if (!version || !is_version(*version)) error("version must be a semver version such as 1.0.0", "version");
  const std::string* license = string_of(m.get("license"));
  if (!license || js_trim(*license).empty()) error("license must be an SPDX licence expression", "license");
  const Json* shapes = m.get("shapes");
  const Json* composites = m.get("composites");
  const Json* fragments = m.get("fragments");
  for (const auto& [id, s] : members(shapes)) {
    if (!kShapeId.test(id)) error("Shape name \"" + id + "\" is not an identifier", "shapes." + id);
    if (!truthy(&s) || !(s.is_object() || s.is_array())) error("A shape is an object", "shapes." + id);
    else if (!truthy(s.get("svg")) && !truthy(s.get("base")))
      warn("Shape has neither svg nor base; it draws as a box", "shapes." + id);
  }
  static const std::set<std::string, std::less<>> kThemeKeys(theme_keys().begin(), theme_keys().end());
  for (const auto& [id, t] : members(m.get("themes"))) {
    std::string path = "themes." + id;
    if (!kShapeId.test(id)) error("Theme name \"" + id + "\" is not an identifier", path);
    if (!t.is_object()) {
      error("A theme is an object", path);
      continue;
    }
    for (const auto& [k, v] : t.as_object()) {
      if (k == "extends" || k == "label" || k == "description") continue;
      if (k == "nodes") {
        for (const auto& [kind, look] : members(&v))
          if (!look.is_object()) error("A theme node is an object of fill, stroke and text", path + ".nodes." + kind);
        continue;
      }
      if (!kThemeKeys.count(k)) warn("Unknown theme key \"" + k + "\"", path + "." + k);
    }
  }
  const Json* dependencies = m.get("dependencies");
  for (const auto& [id, c] : members(composites)) {
    if (!kShapeId.test(id)) error("Composite name \"" + id + "\" is not an identifier", "composites." + id);
    if (truthy(shapes ? shapes->get(id) : nullptr))
      error("\"" + id + "\" is both a shape and a composite", "composites." + id);
    if (!(js_number(c.get("width")) > 0) || !(js_number(c.get("height")) > 0))
      error("A composite needs a width and height", "composites." + id);
    const Json* parts = c.get("parts");
    if (parts && parts->is_array())
      for (std::size_t i = 0; i < parts->as_array().size(); ++i) {
        const Json& part = parts->as_array()[i];
        std::string shape = js_string(part.get("shape"));
        bool local = shape.find('.') == std::string::npos;
        std::string path = "composites." + id + ".parts." + std::to_string(i);
        if (local && !truthy(shapes ? shapes->get(shape) : nullptr))
          error("Part \"" + shape + "\" is not a shape of this library", path);
        std::string alias = shape.substr(0, shape.find('.'));
        bool known = false;
        for (const auto& [dep, _] : members(dependencies)) known = known || default_library_alias(dep) == alias;
        if (!local && !known) warn("Part \"" + shape + "\" refers to a library that is not a dependency", path);
      }
  }
  for (const auto& [id, k] : members(m.get("kinds"))) {
    std::string path = "kinds." + id;
    if (!kShapeId.test(id)) error("Kind name \"" + id + "\" is not an identifier", path);
    if (!truthy(&k) || !k.is_object()) {
      error("A kind is an object", path);
      continue;
    }
    for (const char* key :
         {"label", "description", "category", "shape", "base", "fill", "stroke", "fontColor", "stereotype"})
      if (const Json* v = k.get(key); v && !v->is_null() && !v->is_string())
        error(std::string(key) + " must be a string", path + "." + key);
    if (const std::string* shape = string_of(k.get("shape"))) {
      if (shape->find('.') == std::string::npos) {
        if (!truthy(shapes ? shapes->get(*shape) : nullptr) && !truthy(composites ? composites->get(*shape) : nullptr))
          error("Shape \"" + *shape + "\" is not a shape or composite of this library", path + ".shape");
      } else {
        std::string alias = shape->substr(0, shape->find('.'));
        bool known = false;
        for (const auto& [dep, _] : members(dependencies)) known = known || default_library_alias(dep) == alias;
        if (!known) warn("Shape \"" + *shape + "\" refers to a library that is not a dependency", path + ".shape");
      }
    } else if (!truthy(k.get("base"))) {
      warn("Kind has neither shape nor base; its elements draw as boxes", path);
    }
  }
  for (const auto& [id, f] : members(fragments)) {
    if (!kShapeId.test(id)) error("Fragment name \"" + id + "\" is not an identifier", "fragments." + id);
    if (!string_of(f.get("text"))) error("A fragment needs text", "fragments." + id);
  }
  for (const auto& [dep, range] : members(dependencies)) {
    if (!is_library_name(dep)) error("Dependency \"" + dep + "\" is not a library name", "dependencies." + dep);
    const std::string* r = string_of(&range);
    if (!r || !is_valid_range(*r))
      error("\"" + js_string(&range) + "\" is not a version range", "dependencies." + dep);
  }
  return issues;
}

std::string expand_fragment(const Json& fragment, const std::map<std::string, std::string, std::less<>>& values,
                            const std::string& alias) {
  std::map<std::string, std::string, std::less<>> defaults;
  const Json* params = fragment.get("params");
  if (params && params->is_array())
    for (const auto& p : params->as_array()) {
      std::string name = js_string(p.get("name"));
      const Json* d = p.get("default");
      defaults[name] = d && !d->is_null() ? js_string(d) : name;
    }
  static const Regex kPlaceholder("\\$\\{(\\w+)\\}", "g");
  const std::string* text = string_of(fragment.get("text"));
  return kPlaceholder.replace(
      text ? *text : std::string(),
      [&](const js::Match& m) -> std::string {
        std::string name = m.str(1);
        if (name == "alias") return alias;
        if (auto it = values.find(name); it != values.end()) return it->second;
        if (auto it = defaults.find(name); it != defaults.end()) return it->second;
        return m.str(0);
      },
      true);
}

std::vector<ManifestIssue> validate_fragments(const Json& bundle, const std::string& alias) {
  std::vector<ManifestIssue> issues;
  for (const auto& [id, f] : members(bundle.get("fragments"))) {
    std::string text = expand_fragment(f, {}, alias);
    auto result = dsl::parse("model {\n" + text + "\n}\n");
    for (const auto& d : result.diagnostics)
      if (d.severity == dsl::Severity::Error)
        issues.push_back({true, "Fragment does not parse: " + d.message + " (line " + std::to_string(d.line - 1) + ")",
                          "fragments." + id});
  }
  return issues;
}

const char* to_string(LicenseStatus status) {
  switch (status) {
    case LicenseStatus::Allowed: return "allowed";
    case LicenseStatus::Restricted: return "restricted";
    case LicenseStatus::Disallowed: return "disallowed";
    case LicenseStatus::Unknown: return "unknown";
  }
  return "unknown";
}

const std::vector<std::string>& allowed_licenses() {
  static const std::vector<std::string> kAllowed{"MIT",          "MIT-0", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause",
                                                 "ISC",          "0BSD",  "CC0-1.0",    "CC-BY-4.0",    "Unlicense"};
  return kAllowed;
}

LicenseCheck check_license(const Json& m) {
  const std::string* license = string_of(m.get("license"));
  std::string expr = license ? trim(*license) : "";
  if (expr.empty()) return {LicenseStatus::Unknown, "No licence"};
  static const Regex kUnlicensed("^UNLICENSED$", "i");
  const Json* source = m.get("source");
  const std::string* kind = source ? string_of(source->get("kind")) : nullptr;
  if (kind && *kind == "original" && kUnlicensed.test(expr))
    return {LicenseStatus::Allowed, "Original work of this project"};
  static const Regex kParens("[()]", "g");
  static const Regex kOr("\\s+OR\\s+", "i");
  static const Regex kAnd("\\s+AND\\s+", "i");
  static const Regex kPlus("\\+$");
  std::vector<std::vector<std::string>> alternatives;
  for (const auto& alt : kOr.split(kParens.replace(expr, " ", true))) {
    std::vector<std::string> ids;
    for (const auto& x : kAnd.split(alt)) ids.push_back(kPlus.replace(trim(x), "", false));
    alternatives.push_back(std::move(ids));
  }
  auto ok = [](const std::string& id) {
    std::string lower = js::to_lower(id);
    return std::any_of(allowed_licenses().begin(), allowed_licenses().end(),
                       [&](const std::string& a) { return js::to_lower(a) == lower; });
  };
  if (std::any_of(alternatives.begin(), alternatives.end(),
                  [&](const auto& alt) { return std::all_of(alt.begin(), alt.end(), ok); })) {
    const std::string* notes = string_of(m.get("licenseNotes"));
    if (notes && !js_trim(*notes).empty())
      return {LicenseStatus::Restricted, expr + " with extra terms: " + trim(*notes)};
    return {LicenseStatus::Allowed, expr};
  }
  static const Regex kDisallowed("^(CC-BY(-SA)?-NC|CC-BY(-NC)?-ND|GPL|AGPL|LGPL|SSPL|BUSL|proprietary)", "i");
  if (std::all_of(alternatives.begin(), alternatives.end(), [&](const auto& alt) {
        return std::any_of(alt.begin(), alt.end(), [&](const std::string& id) { return kDisallowed.test(id); });
      }))
    return {LicenseStatus::Disallowed, expr + " is not a licence libraries can ship under"};
  return {LicenseStatus::Unknown, expr + " is not on the allow-list"};
}

// ── bundles ────────────────────────────────────────────────────────────────────────────────────

std::optional<SvgArt> sanitize_svg(std::string_view svg, const std::string& id_prefix) {
  static const Regex kXmlDecl("<\\?xml[^>]*\\?>", "g");
  static const Regex kDoctype("<!DOCTYPE[^>]*>", "gi");
  static const Regex kComment("<!--[\\s\\S]*?-->", "g");
  static const Regex kOpen("<svg\\b([^>]*)>", "i");
  std::string text = kComment.replace(kDoctype.replace(kXmlDecl.replace(svg, "", true), "", true), "", true);
  auto open = kOpen.exec(text);
  std::size_t close = text.rfind("</svg>");
  if (!open || close == std::string::npos || close < open->index()) return std::nullopt;
  std::string attrs = open->str(1);
  auto attr = [&](const std::string& name) -> std::optional<std::string> {
    Regex re("\\b" + name + "\\s*=\\s*[\"']([^\"']*)[\"']", "i");
    auto m = re.exec(attrs);
    return m ? m->get(1) : std::nullopt;
  };
  double w = parse_float(attr("width").value_or(""));
  double h = parse_float(attr("height").value_or(""));
  std::optional<std::string> view_box = attr("viewBox");
  if (!view_box && w > 0 && h > 0) view_box = "0 0 " + js_number_string(w) + " " + js_number_string(h);
  if (!view_box) return std::nullopt;
  static const Regex kSeparators("[\\s,]+");
  auto parts = kSeparators.split(*view_box);
  double vw = parts.size() > 2 ? js_to_number(parts[2]) : 0;
  double vh = parts.size() > 3 ? js_to_number(parts[3]) : 0;
  std::string body = text.substr(open->end(), close - open->end());
  static const Regex kScript("<script[\\s\\S]*?<\\/script\\s*>", "gi");
  static const Regex kScriptEmpty("<script[^>]*\\/>", "gi");
  static const Regex kForeign("<foreignObject[\\s\\S]*?<\\/foreignObject\\s*>", "gi");
  static const Regex kHandler("\\son\\w+\\s*=\\s*(\"[^\"]*\"|'[^']*')", "gi");
  static const Regex kHref("\\s(xlink:)?href\\s*=\\s*(\"([^\"]*)\"|'([^']*)')", "gi");
  static const Regex kDataImage("^data:image\\/(png|jpeg|gif|webp);", "i");
  static const Regex kUrl("url\\(\\s*(['\"]?)(?!#)[^)]*\\1\\s*\\)", "gi");
  body = kScript.replace(body, "", true);
  body = kScriptEmpty.replace(body, "", true);
  body = kForeign.replace(body, "", true);
  body = kHandler.replace(body, "", true);
  // External references: only same-document fragments and embedded images stay.
  body = kHref.replace(
      body,
      [](const js::Match& m) -> std::string {
        std::string url = m.has(3) ? m.str(3) : m.has(4) ? m.str(4) : "";
        return (!url.empty() && url[0] == '#') || kDataImage.test(url) ? m.str(0) : "";
      },
      true);
  body = trim(kUrl.replace(body, "none", true));
  // Prefix ids and their references.
  static const Regex kId("\\sid\\s*=\\s*[\"']([^\"']+)[\"']", "g");
  static const Regex kSpecials("[.*+?^${}()|[\\]\\\\]", "g");
  std::vector<std::string> ids;
  for (const auto& m : kId.match_all(body))
    if (std::find(ids.begin(), ids.end(), m.str(1)) == ids.end()) ids.push_back(m.str(1));
  for (const auto& id : ids) {
    std::string safe = kSpecials.replace(id, "\\$&", true);
    body = Regex("(\\sid\\s*=\\s*[\"'])" + safe + "([\"'])", "g").replace(body, "$1" + id_prefix + id + "$2", true);
    body = Regex("url\\(\\s*#" + safe + "\\s*\\)", "g").replace(body, "url(#" + id_prefix + id + ")", true);
    body = Regex("(href\\s*=\\s*[\"'])#" + safe + "([\"'])", "g").replace(body, "$1#" + id_prefix + id + "$2", true);
  }
  return SvgArt{body, *view_box, w > 0 ? w : vw, h > 0 ? h : vh};
}

BuildResult build_bundle(const Json& manifest, const FileReader& read) {
  BuildResult result;
  result.issues = validate_manifest(manifest);
  auto has_error = [&] {
    return std::any_of(result.issues.begin(), result.issues.end(), [](const ManifestIssue& i) { return i.error; });
  };
  if (has_error()) return result;
  static const Regex kNonName("[^a-z0-9]+", "gi");
  static const Regex kSvgTag("<svg\\b", "i");
  static const Regex kDrawFile("\\.draw$");
  static const Regex kTrailingSpace("\\s+$");
  std::string prefix = kNonName.replace(js_string(manifest.get("name")), "-", true) + "-";
  Json shapes = Json::object();
  for (const auto& [id, s] : members(manifest.get("shapes"))) {
    Json entry = s;
    const std::string* svg = string_of(s.get("svg"));
    if (svg && !svg->empty()) {
      std::optional<std::string> text;
      if (kSvgTag.test(*svg)) text = *svg;
      else if (!js_trim(*svg).empty() && js_trim(*svg).front() == '<') {
        const std::string* vb = string_of(s.get("viewBox"));
        text = "<svg viewBox=\"" + (vb ? *vb : std::string("0 0 48 48")) + "\">" + *svg + "</svg>";
      } else {
        text = read(*svg);
      }
      if (!text) {
        result.issues.push_back({true, "Cannot read " + *svg, "shapes." + id + ".svg"});
        continue;
      }
      auto art = sanitize_svg(*text, prefix + id + "-");
      if (!art) {
        result.issues.push_back({true, "Not an SVG with a viewBox or size", "shapes." + id + ".svg"});
        continue;
      }
      entry = s;
      entry.set("svg", art->body);
      entry.set("viewBox", art->view_box);
      const Json* sw = s.get("width");
      const Json* sh = s.get("height");
      entry.set("width", sw && !sw->is_null() ? *sw : Json(art->width != 0 && !std::isnan(art->width) ? art->width : 48.0));
      entry.set("height", sh && !sh->is_null() ? *sh : Json(art->height != 0 && !std::isnan(art->height) ? art->height : 48.0));
    }
    shapes.set(id, entry);
  }
  Json fragments = Json::object();
  for (const auto& [id, f] : members(manifest.get("fragments"))) {
    std::string raw = js_string(f.get("text"));
    std::string path = trim(raw);
    std::optional<std::string> text = kDrawFile.test(path) ? read(path) : std::optional<std::string>(raw);
    if (!text) {
      result.issues.push_back({true, "Cannot read " + raw, "fragments." + id + ".text"});
      continue;
    }
    Json entry = f;
    entry.set("text", kTrailingSpace.replace(*text, "", false));
    fragments.set(id, entry);
  }
  Json bundle(Json::Object{{"format", Json("diadraw-library")}, {"formatVersion", Json(1)}});
  for (const auto& [k, v] : manifest.as_object()) bundle.set(k, v);
  if (truthy(manifest.get("shapes"))) bundle.set("shapes", shapes);
  if (truthy(manifest.get("fragments"))) bundle.set("fragments", fragments);
  for (auto& i : validate_fragments(bundle)) result.issues.push_back(std::move(i));
  LicenseCheck license = check_license(manifest);
  if (license.status == LicenseStatus::Disallowed) result.issues.push_back({true, license.reason, "license"});
  else if (license.status != LicenseStatus::Allowed)
    result.issues.push_back({false,
                             std::string("Licence ") + to_string(license.status) + ": " + license.reason +
                                 ". Fine for local use; not for shipping with DiaDraw.",
                             "license"});
  if (!has_error()) result.bundle = std::move(bundle);
  return result;
}

std::optional<Json> parse_bundle(std::string_view text) {
  auto value = Json::parse(text);
  if (!value || !value->is_object()) return std::nullopt;
  const std::string* format = string_of(value->get("format"));
  const Json* version = value->get("formatVersion");
  if (!format || *format != "diadraw-library" || !version || !version->is_number() || version->as_number() != 1)
    return std::nullopt;
  auto issues = validate_manifest(*value);
  if (std::any_of(issues.begin(), issues.end(), [](const ManifestIssue& i) { return i.error; })) return std::nullopt;
  return std::move(*value);
}

std::string serialize_bundle(const Json& bundle) { return bundle.dump_pretty(1) + "\n"; }

// ── the libraries a document uses ──────────────────────────────────────────────────────────────

LibrarySet::LibrarySet(std::vector<ResolvedLibrary> libraries) : libraries_(std::move(libraries)) {
  for (const auto& l : libraries_) {
    bool found = false;
    for (auto& [alias, bundle] : by_alias_)
      if (alias == l.use.alias) {
        bundle = l.bundle;
        found = true;
      }
    if (!found) by_alias_.emplace_back(l.use.alias, l.bundle);
  }
}

std::vector<std::string> LibrarySet::aliases() const {
  std::vector<std::string> out;
  for (const auto& [alias, _] : by_alias_) out.push_back(alias);
  return out;
}

const Json* LibrarySet::library(std::string_view alias) const {
  for (const auto& [a, bundle] : by_alias_)
    if (a == alias) return bundle.get();
  return nullptr;
}

std::string LibrarySet::dependency_ref(const Json& lib, std::string_view ref) const {
  auto dot = ref.find('.');
  std::string dep_alias(ref.substr(0, dot));
  std::string id(dot == std::string_view::npos ? std::string_view() : ref.substr(dot + 1));
  std::optional<std::string> name;
  for (const auto& [d, _] : members(lib.get("dependencies")))
    if (default_library_alias(d) == dep_alias) {
      name = d;
      break;
    }
  std::string alias = dep_alias;
  if (name)
    for (const auto& l : libraries_) {
      const std::string* n = string_of(l.bundle->get("name"));
      if (n && *n == *name) {
        alias = l.use.alias;
        break;
      }
    }
  return alias + "." + id;
}

std::optional<Art> LibrarySet::art(std::string_view ref, int depth) const {
  auto dot = ref.find('.');
  if (dot == std::string_view::npos || dot == 0 || depth > 4) return std::nullopt;
  std::string alias(ref.substr(0, dot));
  std::string id(ref.substr(dot + 1));
  const Json* lib = library(alias);
  if (!lib) return std::nullopt;
  const Json* shapes = lib->get("shapes");
  if (const Json* shape = shapes ? shapes->get(id) : nullptr; truthy(shape)) {
    Art art;
    if (truthy(shape->get("svg"))) art.shape.body = js_string(shape->get("svg"));
    if (truthy(shape->get("viewBox"))) art.shape.view_box = js_string(shape->get("viewBox"));
    if (truthy(shape->get("base"))) art.shape.base = js_string(shape->get("base"));
    if (truthy(shape->get("width"))) art.shape.width = js_number(shape->get("width"));
    if (truthy(shape->get("height"))) art.shape.height = js_number(shape->get("height"));
    art.kind = kind_ref(*lib, alias, shape->get("kind"));
    const std::string* position = string_of(shape->get("labelPosition"));
    art.shape.label_inside = position && *position == "inside";
    if (const Json* colors = lib->get("colors"); truthy(colors)) {
      for (const auto& c : colors->get("fill") && colors->get("fill")->is_array() ? colors->get("fill")->as_array() : Json::Array{})
        art.shape.fill_colors.push_back(js_string(&c));
      for (const auto& c : colors->get("stroke") && colors->get("stroke")->is_array() ? colors->get("stroke")->as_array() : Json::Array{})
        art.shape.stroke_colors.push_back(js_string(&c));
    }
    const Json* label = shape->get("label");
    art.label = label && !label->is_null() ? js_string(label) : title_case(id);
    return art;
  }
  const Json* composites = lib->get("composites");
  if (const Json* composite = composites ? composites->get(id) : nullptr; truthy(composite)) {
    std::string body;
    const Json* parts = composite->get("parts");
    if (parts && parts->is_array())
      for (const auto& part : parts->as_array()) {
        std::string shape = js_string(part.get("shape"));
        auto inner = art(shape.find('.') != std::string::npos ? dependency_ref(*lib, shape) : alias + "." + shape, depth + 1);
        if (!inner || !inner->shape.body || inner->shape.body->empty()) continue;
        std::string view_box = inner->shape.view_box
                                   ? *inner->shape.view_box
                                   : "0 0 " + js_number_string(inner->shape.width.value_or(48)) + " " +
                                         js_number_string(inner->shape.height.value_or(48));
        body += "<svg x=\"" + js_string(part.get("x")) + "\" y=\"" + js_string(part.get("y")) + "\" width=\"" +
                js_string(part.get("width")) + "\" height=\"" + js_string(part.get("height")) + "\" viewBox=\"" +
                view_box + "\" preserveAspectRatio=\"xMidYMid meet\">" + *inner->shape.body + "</svg>";
      }
    Art out;
    out.shape.body = body;
    out.shape.view_box = "0 0 " + js_string(composite->get("width")) + " " + js_string(composite->get("height"));
    out.shape.width = js_number(composite->get("width"));
    out.shape.height = js_number(composite->get("height"));
    out.kind = kind_ref(*lib, alias, composite->get("kind"));
    const Json* label = composite->get("label");
    out.label = label && !label->is_null() ? js_string(label) : title_case(id);
    return out;
  }
  if (const Json* open = lib->get("open"); truthy(open)) {
    Art out;
    const Json* base = open->get("base");
    out.shape.base = base && !base->is_null() ? js_string(base) : "image";
    out.kind = kind_ref(*lib, alias, open->get("kind"));
    out.label = title_case(id);
    return out;
  }
  return std::nullopt;
}

std::optional<KindArt> LibrarySet::kind(std::string_view ref) const {
  auto dot = ref.find('.');
  if (dot == std::string_view::npos || dot == 0) return std::nullopt;
  std::string alias(ref.substr(0, dot));
  std::string id(ref.substr(dot + 1));
  const Json* lib = library(alias);
  const Json* kinds = lib ? lib->get("kinds") : nullptr;
  const Json* k = kinds ? kinds->get(id) : nullptr;
  if (!truthy(k) || !k->is_object()) return std::nullopt;
  auto text = [&](const char* key) -> std::optional<std::string> {
    const std::string* v = string_of(k->get(key));
    return v && !v->empty() ? std::optional<std::string>(*v) : std::nullopt;
  };
  KindArt out;
  if (auto shape = text("shape"))
    out.shape = shape->find('.') == std::string::npos ? alias + "." + *shape : dependency_ref(*lib, *shape);
  else
    out.shape = text("base");
  out.fill = text("fill");
  out.stroke = text("stroke");
  out.text = text("fontColor");
  out.stereotype = text("stereotype");
  out.label = text("label").value_or(title_case(id));
  return out;
}

std::optional<std::string> LibrarySet::kind_ref(const Json& lib, const std::string& alias, const Json* kind) const {
  if (!truthy(kind)) return std::nullopt;
  std::string id = js_string(kind);
  const Json* kinds = lib.get("kinds");
  // A library's own kind wins over a built-in kind of the same name.
  if (truthy(kinds ? kinds->get(id) : nullptr)) return alias + "." + id;
  return id;
}

KindResolver LibrarySet::kind_resolver() const {
  auto self = std::make_shared<LibrarySet>(*this);
  return [self](const std::string& ref) { return self->kind(ref); };
}

ShapeResolver LibrarySet::resolver() const {
  auto self = std::make_shared<LibrarySet>(*this);
  return [self](const std::string& ref) -> std::optional<ShapeArt> {
    auto a = self->art(ref);
    return a ? std::optional<ShapeArt>(a->shape) : std::nullopt;
  };
}

std::optional<ThemeDef> LibrarySet::theme(std::string_view ref) const {
  auto dot = ref.find('.');
  if (dot == std::string_view::npos) return std::nullopt;
  std::string alias(ref.substr(0, dot));
  std::string id(ref.substr(dot + 1));
  const Json* lib = library(alias);
  const Json* themes = lib ? lib->get("themes") : nullptr;
  const Json* entry = themes && themes->is_object() ? themes->get(id) : nullptr;
  if (!entry || !entry->is_object()) return std::nullopt;
  Json named = *entry;
  named.set("name", std::string(ref));
  auto def = theme_from_json(named);
  if (!def) return std::nullopt;
  // `extends` names a built-in, another theme of this library, or `dep.theme` of a dependency.
  if (def->base) {
    if (def->base->find('.') != std::string::npos)
      def->base = dependency_ref(*lib, *def->base);
    else if (!find_theme(*def->base) && themes->get(*def->base))
      def->base = alias + "." + *def->base;
  }
  return std::move(*def);
}

std::vector<std::string> LibrarySet::theme_refs() const {
  std::vector<std::string> out;
  for (const auto& [alias, bundle] : by_alias_)
    for (const auto& [id, _] : members(bundle->get("themes"))) out.push_back(alias + "." + id);
  return out;
}

ThemeResolver LibrarySet::theme_resolver() const {
  auto self = std::make_shared<LibrarySet>(*this);
  return [self](const std::string& ref) { return self->theme(ref); };
}

std::vector<PaletteItem> LibrarySet::palette() const {
  std::vector<PaletteItem> items;
  auto strings = [](const Json* v) {
    std::vector<std::string> out;
    if (v && v->is_array())
      for (const auto& x : v->as_array()) out.push_back(js_string(&x));
    return out;
  };
  for (const auto& [use, bundle] : libraries_) {
    std::string library_name = js_string(bundle->get("name"));
    auto label_of = [](const Json& e, const std::string& id) {
      const Json* l = e.get("label");
      return l && !l->is_null() ? js_string(l) : title_case(id);
    };
    auto category_of = [&](const Json& e, const std::string& fallback) {
      const Json* c = e.get("category");
      return c && !c->is_null() ? js_string(c) : fallback;
    };
    for (const char* type : {"shape", "composite"}) {
      for (const auto& [id, s] : members(bundle->get(std::string(type) == "shape" ? "shapes" : "composites"))) {
        PaletteItem item;
        item.type = type;
        item.alias = use.alias;
        item.library = library_name;
        item.id = id;
        item.ref = use.alias + "." + id;
        item.label = label_of(s, id);
        item.category = category_of(s, library_name);
        item.kind = kind_ref(*bundle, use.alias, s.get("kind"));
        item.keywords = strings(s.get("keywords"));
        item.art = art(item.ref);
        items.push_back(std::move(item));
      }
    }
    for (const auto& [id, k] : members(bundle->get("kinds"))) {
      auto kind = this->kind(use.alias + "." + id);
      if (!kind) continue;
      PaletteItem item;
      item.type = "kind";
      item.alias = use.alias;
      item.library = library_name;
      item.id = id;
      item.ref = use.alias + "." + id;
      item.label = kind->label;
      item.category = category_of(k, "Kinds");
      item.kind = item.ref;
      item.keywords = strings(k.get("keywords"));
      if (kind->shape) {
        item.art = art(*kind->shape);
        if (!item.art) {
          // A base shape: the palette draws its outline.
          Art base;
          base.shape.base = kind->shape;
          base.kind = item.ref;
          base.label = kind->label;
          item.art = std::move(base);
        }
      }
      if (truthy(k.get("description"))) item.description = js_string(k.get("description"));
      items.push_back(std::move(item));
    }
    for (const auto& [id, f] : members(bundle->get("fragments"))) {
      PaletteItem item;
      item.type = "fragment";
      item.alias = use.alias;
      item.library = library_name;
      item.id = id;
      item.ref = use.alias + "." + id;
      item.label = label_of(f, id);
      item.category = category_of(f, "Fragments");
      if (truthy(f.get("description"))) item.description = js_string(f.get("description"));
      items.push_back(std::move(item));
    }
  }
  return items;
}

const Json* LibrarySet::fragment(std::string_view alias, std::string_view id) const {
  const Json* lib = library(alias);
  const Json* fragments = lib ? lib->get("fragments") : nullptr;
  return fragments ? fragments->get(id) : nullptr;
}

std::optional<std::string> LibrarySet::expand(std::string_view alias, std::string_view id,
                                              const std::map<std::string, std::string, std::less<>>& values) const {
  const Json* f = fragment(alias, id);
  if (!f || !truthy(f)) return std::nullopt;
  return expand_fragment(*f, values, std::string(alias));
}

std::vector<std::pair<std::string, std::string>> LibrarySet::mapping(std::string_view format) const {
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto& [use, bundle] : libraries_) {
    const Json* mappings = bundle->get("mappings");
    for (const auto& [foreign, id] : members(mappings ? mappings->get(format) : nullptr)) {
      bool seen = std::any_of(out.begin(), out.end(), [&](const auto& p) { return p.first == foreign; });
      if (!seen) out.emplace_back(foreign, use.alias + "." + js_string(&id));
    }
  }
  return out;
}

Json to_json(const PaletteItem& item) {
  Json o(Json::Object{{"type", Json(item.type)},
                      {"alias", Json(item.alias)},
                      {"library", Json(item.library)},
                      {"id", Json(item.id)},
                      {"ref", Json(item.ref)},
                      {"label", Json(item.label)},
                      {"category", Json(item.category)}});
  if (item.kind) o.set("kind", Json(*item.kind));
  Json keywords = Json::array();
  for (const auto& k : item.keywords) keywords.push(Json(k));
  o.set("keywords", keywords);
  if (item.art) {
    const ShapeArt& s = item.art->shape;
    Json art = Json::object();
    if (s.body) art.set("body", Json(*s.body));
    if (s.view_box) art.set("viewBox", Json(*s.view_box));
    if (s.base) art.set("base", Json(*s.base));
    if (s.width) art.set("width", Json(*s.width));
    if (s.height) art.set("height", Json(*s.height));
    if (item.art->kind) art.set("kind", Json(*item.art->kind));
    if (s.label_inside) art.set("labelPosition", Json("inside"));
    if (!s.fill_colors.empty() || !s.stroke_colors.empty()) {
      Json colors = Json::object();
      Json fill = Json::array(), stroke = Json::array();
      for (const auto& c : s.fill_colors) fill.push(Json(c));
      for (const auto& c : s.stroke_colors) stroke.push(Json(c));
      if (!s.fill_colors.empty()) colors.set("fill", fill);
      if (!s.stroke_colors.empty()) colors.set("stroke", stroke);
      art.set("colors", colors);
    }
    art.set("label", Json(item.art->label));
    o.set("art", art);
  }
  if (item.description) o.set("description", Json(*item.description));
  return o;
}

std::vector<UnknownReference> unknown_references(const Model& model, const LibrarySet& set) {
  std::vector<UnknownReference> out;
  auto check = [&](const Element& e, const std::string& ref, bool kind) {
    auto dot = ref.find('.');
    if (dot == std::string::npos || !set.library(std::string_view(ref).substr(0, dot))) return;
    if (kind ? set.has_kind(ref) : set.has(ref)) return;
    out.push_back({e.id, kind, ref});
  };
  for (const auto& e : model.elements) {
    check(e, e.kind, true);
    if (e.shape) check(e, *e.shape, false);
  }
  return out;
}

LibrarySource memory_source(std::vector<std::shared_ptr<const Json>> bundles) {
  auto shared = std::make_shared<std::vector<std::shared_ptr<const Json>>>(std::move(bundles));
  LibrarySource s;
  s.versions = [shared](const std::string& name) {
    std::vector<std::string> out;
    for (const auto& b : *shared)
      if (const std::string* n = string_of(b->get("name")); n && *n == name) out.push_back(js_string(b->get("version")));
    return out;
  };
  s.load = [shared](const std::string& name, const std::string& version) -> std::shared_ptr<const Json> {
    for (const auto& b : *shared) {
      const std::string* n = string_of(b->get("name"));
      if (n && *n == name && js_string(b->get("version")) == version) return b;
    }
    return nullptr;
  };
  return s;
}

LibrarySource builtin_source() {
  static const std::vector<std::shared_ptr<const Json>> kBundles = [] {
    std::vector<std::shared_ptr<const Json>> out;
    for (const auto& b : builtin_bundles().as_array()) out.push_back(std::make_shared<const Json>(b));
    return out;
  }();
  return memory_source(kBundles);
}

std::optional<Lockfile> parse_lockfile(std::string_view text) {
  auto json = Json::parse(text);
  if (!json || !json->is_object()) return std::nullopt;
  const Json* version = json->get("lockfileVersion");
  const Json* libraries = json->get("libraries");
  if (!version || !version->is_number() || version->as_number() != 1 || !truthy(libraries)) return std::nullopt;
  Lockfile lock;
  for (const auto& [spec, entry] : members(libraries)) {
    LockedLibrary l;
    l.version = js_string(entry.get("version"));
    l.resolved = js_string(entry.get("resolved"));
    if (const std::string* integrity = string_of(entry.get("integrity"))) l.integrity = *integrity;
    lock.libraries[spec] = l;
  }
  return lock;
}

Resolution resolve_uses(const std::vector<LibraryUse>& uses, const std::vector<LibrarySource>& sources,
                        const Lockfile* lock) {
  std::vector<ResolvedLibrary> resolved;
  std::vector<UnresolvedLibrary> unresolved;
  for (const auto& use : uses) {
    std::string spec = library_spec(use);
    std::shared_ptr<const Json> found;
    if (lock)
      if (auto it = lock->libraries.find(spec); it != lock->libraries.end() && !it->second.version.empty() &&
                                                satisfies(it->second.version, use.range))
        for (const auto& s : sources)
          if ((found = s.load(use.name, it->second.version))) break;
    if (!found)
      for (const auto& s : sources) {
        auto version = max_satisfying(s.versions(use.name), use.range);
        if (version && (found = s.load(use.name, *version))) break;
      }
    if (found) {
      resolved.push_back({use, found});
      continue;
    }
    std::vector<std::string> known;
    for (const auto& s : sources)
      for (auto& v : s.versions(use.name)) known.push_back(std::move(v));
    std::string list;
    for (std::size_t i = 0; i < known.size(); ++i) list += (i ? ", " : "") + known[i];
    unresolved.push_back(
        {use, !known.empty() ? "No installed version of " + use.name + " matches " + use.range + " (have " + list +
                                   "); run `diadraw lib add`"
                             : "Library " + use.name + " is not installed; run `diadraw lib add " + spec + "`"});
  }
  return {LibrarySet(std::move(resolved)), std::move(unresolved)};
}

}  // namespace dd::library
