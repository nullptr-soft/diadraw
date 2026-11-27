// Port of packages/render/src/index.ts and shapes.ts. Every string is built the way the TypeScript
// template literals build it, and every number goes through `num`, so the output is identical.

#include "diadraw/render/svg.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/layout/layout.hpp"

namespace dd {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// ── JavaScript semantics ──────────────────────────────────────────────────────────────────────

/// `String(Math.round(v * 100) / 100)`
std::string num(double v) { return js_number_string(js_round(v * 100) / 100); }


std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    auto at = s.find(sep, start);
    out.emplace_back(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
    if (at == std::string_view::npos) break;
    start = at + 1;
  }
  return out;
}

/// `s.replace(/fill="[^"]*"/, with)`: the first fill attribute only.
std::string replace_fill(const std::string& s, const std::string& with) {
  auto at = s.find("fill=\"");
  if (at == std::string::npos) return s;
  auto close = s.find('"', at + 6);
  if (close == std::string::npos) return s;
  return s.substr(0, at) + with + s.substr(close + 1);
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  std::string out;
  std::size_t start = 0;
  while (true) {
    auto at = s.find(from, start);
    if (at == std::string::npos) break;
    out.append(s, start, at - start);
    out += to;
    start = at + from.size();
  }
  out.append(s, start);
  return out;
}

const std::string* style_value(const std::optional<Style>& style, std::string_view key) {
  return style ? style->find(key) : nullptr;
}


/// `color(v, fallback)`: a colour from a style, escaped for an attribute value (a document can
/// put anything in one), or `fallback`, which must be safe there already: a literal or a colour of
/// the theme render_scene draws with (see attribute_safe).
std::string color(const std::string* v, const std::string& fallback) {
  if (!v || v->empty() || *v == "default") return fallback;
  return escape_xml(*v);
}

const std::string* prop(const Element& e, std::string_view key) { return e.properties.find(key); }
const std::string* prop(const Relationship& r, std::string_view key) { return r.properties.find(key); }

}  // namespace

// ── Theme ──────────────────────────────────────────────────────────────────────────────────────

const Theme& default_theme() {
  static const Theme theme = [] {
    Theme t;
    t.name = "default";
    t.background = "#ffffff";
    t.font_family = "Inter, system-ui, -apple-system, Segoe UI, sans-serif";
    t.font_size = 13;
    t.small_font_size = 11;
    t.edge = "#475569";
    t.edge_text = "#334155";
    t.boundary = "#94a3b8";
    t.boundary_text = "#475569";
    t.nodes = {
        {"default", {"#eef2ff", "#4f46e5", "#1e1b4b"}},   {"person", {"#1e3a8a", "#1e3a8a", "#ffffff"}},
        {"system", {"#1d4ed8", "#1e40af", "#ffffff"}},    {"container", {"#3b82f6", "#2563eb", "#ffffff"}},
        {"component", {"#93c5fd", "#3b82f6", "#0f172a"}}, {"database", {"#3b82f6", "#2563eb", "#ffffff"}},
        {"start", {"#dcfce7", "#16a34a", "#14532d"}},     {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
        {"decision", {"#fef9c3", "#ca8a04", "#422006"}},  {"note", {"#fefce8", "#a16207", "#422006"}},
    };
    t.external = {"#94a3b8", "#64748b", "#ffffff"};
    return t;
  }();
  return theme;
}

namespace {

Theme dark_theme() {
  Theme t = default_theme();
  t.name = "dark";
  t.background = "#0f172a";
  t.edge = "#94a3b8";
  t.edge_text = "#cbd5e1";
  t.boundary = "#64748b";
  t.boundary_text = "#94a3b8";
  t.surface = "#1e293b";
  t.divider_fill = "#334155";
  t.label_text = "#e2e8f0";
  t.nodes = {
      {"default", {"#1e293b", "#818cf8", "#e0e7ff"}},   {"person", {"#1e40af", "#60a5fa", "#ffffff"}},
      {"system", {"#1d4ed8", "#60a5fa", "#ffffff"}},    {"container", {"#2563eb", "#93c5fd", "#ffffff"}},
      {"component", {"#1e3a8a", "#93c5fd", "#e0f2fe"}}, {"database", {"#2563eb", "#93c5fd", "#ffffff"}},
      {"start", {"#14532d", "#4ade80", "#dcfce7"}},     {"end", {"#7f1d1d", "#f87171", "#fee2e2"}},
      {"decision", {"#713f12", "#facc15", "#fef9c3"}},  {"note", {"#422006", "#ca8a04", "#fef3c7"}},
  };
  t.external = {"#334155", "#64748b", "#e2e8f0"};
  return t;
}

/// Black on white with grey fills, for print and for documents that bring their own colours.
Theme mono_theme() {
  Theme t = default_theme();
  t.name = "mono";
  t.edge = "#333333";
  t.edge_text = "#333333";
  t.boundary = "#888888";
  t.boundary_text = "#555555";
  t.label_text = "#111111";
  t.nodes = {
      {"default", {"#ffffff", "#333333", "#111111"}},   {"person", {"#e5e5e5", "#333333", "#111111"}},
      {"system", {"#d4d4d4", "#333333", "#111111"}},    {"container", {"#e5e5e5", "#333333", "#111111"}},
      {"component", {"#f5f5f5", "#333333", "#111111"}}, {"database", {"#e5e5e5", "#333333", "#111111"}},
      {"start", {"#ffffff", "#333333", "#111111"}},     {"end", {"#d4d4d4", "#333333", "#111111"}},
      {"decision", {"#f5f5f5", "#333333", "#111111"}},  {"note", {"#fafafa", "#666666", "#111111"}},
  };
  t.external = {"#ffffff", "#999999", "#555555"};
  return t;
}

/// Cyan and teal on a pale blue-white page.
Theme ocean_theme() {
  Theme t = default_theme();
  t.name = "ocean";
  t.background = "#f5fdff";
  t.edge = "#0e7490";
  t.edge_text = "#164e63";
  t.boundary = "#22d3ee";
  t.boundary_text = "#0e7490";
  t.surface = "#ffffff";
  t.divider_fill = "#cffafe";
  t.label_text = "#083344";
  t.nodes = {
      {"default", {"#ffffff", "#0891b2", "#083344"}},
      {"person", {"#164e63", "#083344", "#ffffff"}},
      {"system", {"#155e75", "#164e63", "#ffffff"}},
      {"container", {"#0e7490", "#155e75", "#ffffff"}},
      {"component", {"#a5f3fc", "#0891b2", "#083344"}},
      {"database", {"#0e7490", "#164e63", "#ffffff"}},
      {"start", {"#dcfce7", "#16a34a", "#14532d"}},
      {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
      {"decision", {"#fef3c7", "#d97706", "#451a03"}},
      {"note", {"#fefce8", "#a16207", "#422006"}},
  };
  t.external = {"#cbd5e1", "#64748b", "#0f172a"};
  return t;
}

/// Emerald greens on a soft green-white page.
Theme forest_theme() {
  Theme t = default_theme();
  t.name = "forest";
  t.background = "#f6fbf7";
  t.edge = "#047857";
  t.edge_text = "#064e3b";
  t.boundary = "#34d399";
  t.boundary_text = "#047857";
  t.surface = "#ffffff";
  t.divider_fill = "#d1fae5";
  t.label_text = "#022c22";
  t.nodes = {
      {"default", {"#ffffff", "#059669", "#022c22"}},
      {"person", {"#064e3b", "#022c22", "#ffffff"}},
      {"system", {"#065f46", "#064e3b", "#ffffff"}},
      {"container", {"#047857", "#065f46", "#ffffff"}},
      {"component", {"#a7f3d0", "#059669", "#022c22"}},
      {"database", {"#047857", "#064e3b", "#ffffff"}},
      {"start", {"#ecfccb", "#65a30d", "#365314"}},
      {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
      {"decision", {"#fef3c7", "#d97706", "#451a03"}},
      {"note", {"#fefce8", "#a16207", "#422006"}},
  };
  t.external = {"#d6d3d1", "#78716c", "#1c1917"};
  return t;
}

/// Warm oranges and rose on a cream page.
Theme sunset_theme() {
  Theme t = default_theme();
  t.name = "sunset";
  t.background = "#fffaf5";
  t.edge = "#c2410c";
  t.edge_text = "#7c2d12";
  t.boundary = "#fb923c";
  t.boundary_text = "#c2410c";
  t.surface = "#ffffff";
  t.divider_fill = "#ffedd5";
  t.label_text = "#431407";
  t.nodes = {
      {"default", {"#ffffff", "#ea580c", "#431407"}},
      {"person", {"#9f1239", "#881337", "#ffffff"}},
      {"system", {"#9a3412", "#7c2d12", "#ffffff"}},
      {"container", {"#c2410c", "#9a3412", "#ffffff"}},
      {"component", {"#fed7aa", "#f97316", "#431407"}},
      {"database", {"#c2410c", "#7c2d12", "#ffffff"}},
      {"start", {"#dcfce7", "#16a34a", "#14532d"}},
      {"end", {"#ffe4e6", "#e11d48", "#881337"}},
      {"decision", {"#fef9c3", "#ca8a04", "#422006"}},
      {"note", {"#fffbeb", "#b45309", "#451a03"}},
  };
  t.external = {"#d6d3d1", "#78716c", "#1c1917"};
  return t;
}

/// Violet and purple on a lilac-white page.
Theme lavender_theme() {
  Theme t = default_theme();
  t.name = "lavender";
  t.background = "#fbfaff";
  t.edge = "#6d28d9";
  t.edge_text = "#4c1d95";
  t.boundary = "#a78bfa";
  t.boundary_text = "#6d28d9";
  t.surface = "#ffffff";
  t.divider_fill = "#ede9fe";
  t.label_text = "#2e1065";
  t.nodes = {
      {"default", {"#ffffff", "#7c3aed", "#2e1065"}},
      {"person", {"#581c87", "#3b0764", "#ffffff"}},
      {"system", {"#5b21b6", "#4c1d95", "#ffffff"}},
      {"container", {"#6d28d9", "#5b21b6", "#ffffff"}},
      {"component", {"#ddd6fe", "#7c3aed", "#2e1065"}},
      {"database", {"#6d28d9", "#4c1d95", "#ffffff"}},
      {"start", {"#dcfce7", "#16a34a", "#14532d"}},
      {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
      {"decision", {"#fef3c7", "#d97706", "#451a03"}},
      {"note", {"#fefce8", "#a16207", "#422006"}},
  };
  t.external = {"#cbd5e1", "#64748b", "#0f172a"};
  return t;
}

/// Earthy browns and stone greys on a warm beige page.
Theme sand_theme() {
  Theme t = default_theme();
  t.name = "sand";
  t.background = "#fbf7f0";
  t.edge = "#57534e";
  t.edge_text = "#292524";
  t.boundary = "#a8a29e";
  t.boundary_text = "#57534e";
  t.surface = "#ffffff";
  t.divider_fill = "#e7e5e4";
  t.label_text = "#1c1917";
  t.nodes = {
      {"default", {"#fffdf8", "#d97706", "#451a03"}},
      {"person", {"#78350f", "#451a03", "#ffffff"}},
      {"system", {"#92400e", "#78350f", "#ffffff"}},
      {"container", {"#57534e", "#44403c", "#ffffff"}},
      {"component", {"#fde68a", "#d97706", "#451a03"}},
      {"database", {"#44403c", "#292524", "#ffffff"}},
      {"start", {"#dcfce7", "#16a34a", "#14532d"}},
      {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
      {"decision", {"#fef9c3", "#ca8a04", "#422006"}},
      {"note", {"#fafaf9", "#44403c", "#0c0a09"}},
  };
  t.external = {"#d6d3d1", "#78716c", "#1c1917"};
  return t;
}

/// Neutral greys with a sky-blue accent, for technical documents.
Theme graphite_theme() {
  Theme t = default_theme();
  t.name = "graphite";
  t.background = "#fafafa";
  t.edge = "#3f3f46";
  t.edge_text = "#18181b";
  t.boundary = "#a1a1aa";
  t.boundary_text = "#52525b";
  t.surface = "#ffffff";
  t.divider_fill = "#f4f4f5";
  t.label_text = "#09090b";
  t.nodes = {
      {"default", {"#ffffff", "#52525b", "#09090b"}},
      {"person", {"#18181b", "#09090b", "#ffffff"}},
      {"system", {"#27272a", "#18181b", "#ffffff"}},
      {"container", {"#3f3f46", "#27272a", "#ffffff"}},
      {"component", {"#e0f2fe", "#0ea5e9", "#082f49"}},
      {"database", {"#0369a1", "#075985", "#ffffff"}},
      {"start", {"#dcfce7", "#16a34a", "#14532d"}},
      {"end", {"#fee2e2", "#dc2626", "#7f1d1d"}},
      {"decision", {"#fef3c7", "#d97706", "#451a03"}},
      {"note", {"#fefce8", "#a16207", "#422006"}},
  };
  t.external = {"#d4d4d8", "#71717a", "#18181b"};
  return t;
}

/// A different soft pastel per kind, with dark text.
Theme pastel_theme() {
  Theme t = default_theme();
  t.name = "pastel";
  t.background = "#fffdfa";
  t.edge = "#475569";
  t.edge_text = "#334155";
  t.boundary = "#cbd5e1";
  t.boundary_text = "#475569";
  t.surface = "#ffffff";
  t.divider_fill = "#f1f5f9";
  t.label_text = "#1e293b";
  t.nodes = {
      {"default", {"#f0f9ff", "#7dd3fc", "#0c4a6e"}},
      {"person", {"#fbcfe8", "#f472b6", "#500724"}},
      {"system", {"#ddd6fe", "#a78bfa", "#2e1065"}},
      {"container", {"#bae6fd", "#38bdf8", "#082f49"}},
      {"component", {"#ccfbf1", "#2dd4bf", "#134e4a"}},
      {"database", {"#fde68a", "#fbbf24", "#451a03"}},
      {"start", {"#bbf7d0", "#4ade80", "#14532d"}},
      {"end", {"#fecdd3", "#fb7185", "#881337"}},
      {"decision", {"#fef08a", "#facc15", "#713f12"}},
      {"note", {"#fff7ed", "#fdba74", "#7c2d12"}},
  };
  t.external = {"#f1f5f9", "#cbd5e1", "#334155"};
  return t;
}

/// A different saturated colour per kind, with white text.
Theme vivid_theme() {
  Theme t = default_theme();
  t.name = "vivid";
  t.background = "#ffffff";
  t.edge = "#27272a";
  t.edge_text = "#18181b";
  t.boundary = "#a1a1aa";
  t.boundary_text = "#52525b";
  t.surface = "#ffffff";
  t.divider_fill = "#f4f4f5";
  t.label_text = "#18181b";
  t.nodes = {
      {"default", {"#ffffff", "#2563eb", "#172554"}},
      {"person", {"#a21caf", "#86198f", "#ffffff"}},
      {"system", {"#1d4ed8", "#1e40af", "#ffffff"}},
      {"container", {"#c2410c", "#9a3412", "#ffffff"}},
      {"component", {"#0f766e", "#115e59", "#ffffff"}},
      {"database", {"#6d28d9", "#5b21b6", "#ffffff"}},
      {"start", {"#15803d", "#166534", "#ffffff"}},
      {"end", {"#b91c1c", "#991b1b", "#ffffff"}},
      {"decision", {"#facc15", "#ca8a04", "#422006"}},
      {"note", {"#fef3c7", "#f59e0b", "#451a03"}},
  };
  t.external = {"#71717a", "#52525b", "#ffffff"};
  return t;
}

/// Black on white with bold fills and larger text, for accessibility and projectors.
Theme contrast_theme() {
  Theme t = default_theme();
  t.name = "contrast";
  t.background = "#ffffff";
  t.font_size = 14;
  t.small_font_size = 12;
  t.edge = "#000000";
  t.edge_text = "#000000";
  t.boundary = "#000000";
  t.boundary_text = "#000000";
  t.surface = "#ffffff";
  t.divider_fill = "#ffffff";
  t.label_text = "#000000";
  t.nodes = {
      {"default", {"#ffffff", "#000000", "#000000"}},
      {"person", {"#000000", "#000000", "#ffffff"}},
      {"system", {"#003399", "#000000", "#ffffff"}},
      {"container", {"#ffffff", "#000000", "#000000"}},
      {"component", {"#fff4a3", "#000000", "#000000"}},
      {"database", {"#dbe7ff", "#000000", "#000000"}},
      {"start", {"#d6f5d6", "#000000", "#000000"}},
      {"end", {"#000000", "#000000", "#ffffff"}},
      {"decision", {"#ffe14d", "#000000", "#000000"}},
      {"note", {"#ffffff", "#000000", "#000000"}},
  };
  t.external = {"#ffffff", "#595959", "#000000"};
  return t;
}

/// White lines on blueprint blue.
Theme blueprint_theme() {
  Theme t = default_theme();
  t.name = "blueprint";
  t.background = "#123a6b";
  t.edge = "#e0f2fe";
  t.edge_text = "#ffffff";
  t.boundary = "#7dd3fc";
  t.boundary_text = "#bae6fd";
  t.surface = "#123a6b";
  t.divider_fill = "#1a4a85";
  t.label_text = "#ffffff";
  t.nodes = {
      {"default", {"#17457f", "#bae6fd", "#f0f9ff"}},
      {"person", {"#0b2a52", "#ffffff", "#ffffff"}},
      {"system", {"#1d5aa6", "#ffffff", "#ffffff"}},
      {"container", {"#1a4f94", "#e0f2fe", "#ffffff"}},
      {"component", {"#15417a", "#7dd3fc", "#e0f2fe"}},
      {"database", {"#1a4f94", "#e0f2fe", "#ffffff"}},
      {"start", {"#123a6b", "#86efac", "#dcfce7"}},
      {"end", {"#123a6b", "#fca5a5", "#fee2e2"}},
      {"decision", {"#123a6b", "#fde047", "#fef9c3"}},
      {"note", {"#0f3360", "#93c5fd", "#e0f2fe"}},
  };
  t.external = {"#123a6b", "#94a3b8", "#cbd5e1"};
  return t;
}

/// Violet and fuchsia on a near-black indigo page.
Theme midnight_theme() {
  Theme t = default_theme();
  t.name = "midnight";
  t.background = "#0c0a1f";
  t.edge = "#a78bfa";
  t.edge_text = "#ddd6fe";
  t.boundary = "#7c3aed";
  t.boundary_text = "#c4b5fd";
  t.surface = "#17142e";
  t.divider_fill = "#1e293b";
  t.label_text = "#ede9fe";
  t.nodes = {
      {"default", {"#17142e", "#a78bfa", "#ede9fe"}},
      {"person", {"#86198f", "#e879f9", "#ffffff"}},
      {"system", {"#5b21b6", "#a78bfa", "#ffffff"}},
      {"container", {"#6d28d9", "#c4b5fd", "#ffffff"}},
      {"component", {"#4c1d95", "#c4b5fd", "#ede9fe"}},
      {"database", {"#5b21b6", "#c4b5fd", "#ffffff"}},
      {"start", {"#14532d", "#4ade80", "#dcfce7"}},
      {"end", {"#7f1d1d", "#f87171", "#fee2e2"}},
      {"decision", {"#713f12", "#facc15", "#fef9c3"}},
      {"note", {"#422006", "#ca8a04", "#fef3c7"}},
  };
  t.external = {"#334155", "#64748b", "#f1f5f9"};
  return t;
}

/// Teal and violet on a dark sea-green page.
Theme aurora_theme() {
  Theme t = default_theme();
  t.name = "aurora";
  t.background = "#06181a";
  t.edge = "#2dd4bf";
  t.edge_text = "#99f6e4";
  t.boundary = "#0d9488";
  t.boundary_text = "#5eead4";
  t.surface = "#0c2a2d";
  t.divider_fill = "#1e293b";
  t.label_text = "#ccfbf1";
  t.nodes = {
      {"default", {"#0c2a2d", "#2dd4bf", "#ccfbf1"}},
      {"person", {"#6d28d9", "#a78bfa", "#ffffff"}},
      {"system", {"#115e59", "#2dd4bf", "#ffffff"}},
      {"container", {"#0f766e", "#5eead4", "#ffffff"}},
      {"component", {"#164e63", "#67e8f9", "#cffafe"}},
      {"database", {"#115e59", "#5eead4", "#ffffff"}},
      {"start", {"#14532d", "#4ade80", "#dcfce7"}},
      {"end", {"#7f1d1d", "#f87171", "#fee2e2"}},
      {"decision", {"#713f12", "#facc15", "#fef9c3"}},
      {"note", {"#422006", "#ca8a04", "#fef3c7"}},
  };
  t.external = {"#334155", "#64748b", "#f1f5f9"};
  return t;
}

/// Orange and red embers on a dark brown page.
Theme ember_theme() {
  Theme t = default_theme();
  t.name = "ember";
  t.background = "#1a120d";
  t.edge = "#fb923c";
  t.edge_text = "#fed7aa";
  t.boundary = "#ea580c";
  t.boundary_text = "#fdba74";
  t.surface = "#2a1c14";
  t.divider_fill = "#292524";
  t.label_text = "#ffedd5";
  t.nodes = {
      {"default", {"#2a1c14", "#fb923c", "#ffedd5"}},
      {"person", {"#991b1b", "#f87171", "#ffffff"}},
      {"system", {"#9a3412", "#fb923c", "#ffffff"}},
      {"container", {"#c2410c", "#fdba74", "#ffffff"}},
      {"component", {"#7c2d12", "#fdba74", "#ffedd5"}},
      {"database", {"#9a3412", "#fdba74", "#ffffff"}},
      {"start", {"#14532d", "#4ade80", "#dcfce7"}},
      {"end", {"#7f1d1d", "#f87171", "#fee2e2"}},
      {"decision", {"#713f12", "#facc15", "#fef9c3"}},
      {"note", {"#422006", "#ca8a04", "#fef3c7"}},
  };
  t.external = {"#44403c", "#78716c", "#f5f5f4"};
  return t;
}

/// Phosphor green on black.
Theme terminal_theme() {
  Theme t = default_theme();
  t.name = "terminal";
  t.background = "#0a0f0a";
  t.edge = "#22c55e";
  t.edge_text = "#86efac";
  t.boundary = "#15803d";
  t.boundary_text = "#4ade80";
  t.surface = "#0a0f0a";
  t.divider_fill = "#052e16";
  t.label_text = "#bbf7d0";
  t.nodes = {
      {"default", {"#04140a", "#22c55e", "#bbf7d0"}},
      {"person", {"#14532d", "#4ade80", "#f0fdf4"}},
      {"system", {"#166534", "#4ade80", "#f0fdf4"}},
      {"container", {"#052e16", "#4ade80", "#dcfce7"}},
      {"component", {"#04140a", "#16a34a", "#86efac"}},
      {"database", {"#052e16", "#22c55e", "#dcfce7"}},
      {"start", {"#052e16", "#a3e635", "#ecfccb"}},
      {"end", {"#450a0a", "#f87171", "#fee2e2"}},
      {"decision", {"#1a2e05", "#facc15", "#fef9c3"}},
      {"note", {"#0a1a0f", "#15803d", "#86efac"}},
  };
  t.external = {"#111811", "#3f6212", "#a3e635"};
  return t;
}

const std::vector<Theme>& builtin_themes() {
  static const std::vector<Theme> themes{
      default_theme(),   dark_theme(),      mono_theme(),      ocean_theme(),     forest_theme(),
      sunset_theme(),    lavender_theme(),  sand_theme(),      graphite_theme(),  pastel_theme(),
      vivid_theme(),     contrast_theme(),  blueprint_theme(), midnight_theme(),  aurora_theme(),
      ember_theme(),     terminal_theme(),
  };
  return themes;
}

}  // namespace

const Theme* find_theme(std::string_view name) {
  for (const auto& t : builtin_themes())
    if (t.name == name) return &t;
  return nullptr;
}

std::vector<std::string> theme_names() {
  std::vector<std::string> out;
  for (const auto& t : builtin_themes()) out.push_back(t.name);
  return out;
}

const std::vector<std::string>& theme_keys() {
  static const std::vector<std::string> keys{"background", "fontFamily", "fontSize",    "smallFontSize",
                                             "edge",       "edgeText",   "boundary",    "boundaryText",
                                             "surface",    "dividerFill", "labelText"};
  return keys;
}

const std::vector<std::string>& theme_node_keys() {
  static const std::vector<std::string> keys{"fill", "stroke", "text"};
  return keys;
}

namespace {

void apply_theme(Theme& t, const ThemeDef& def) {
  auto text = [&](std::string_view key, std::string& into) {
    if (const std::string* v = def.values.find(key)) into = *v;
  };
  auto number = [&](std::string_view key, double& into) {
    if (const std::string* v = def.values.find(key)) {
      double n = js_to_number(*v);
      if (n > 0) into = n;
    }
  };
  text("background", t.background);
  text("fontFamily", t.font_family);
  number("fontSize", t.font_size);
  number("smallFontSize", t.small_font_size);
  text("edge", t.edge);
  text("edgeText", t.edge_text);
  text("boundary", t.boundary);
  text("boundaryText", t.boundary_text);
  text("surface", t.surface);
  text("dividerFill", t.divider_fill);
  text("labelText", t.label_text);
  for (const auto& [kind, style] : def.nodes) {
    NodeStyle* n = nullptr;
    if (kind == "external") {
      n = &t.external;
    } else {
      auto it = t.nodes.find(kind);
      if (it == t.nodes.end()) it = t.nodes.emplace(kind, t.nodes.at("default")).first;
      n = &it->second;
    }
    if (const std::string* v = style.find("fill")) n->fill = *v;
    if (const std::string* v = style.find("stroke")) n->stroke = *v;
    if (const std::string* v = style.find("text")) n->text = *v;
  }
}

std::optional<Theme> resolve_theme_at(std::string_view name, const std::vector<ThemeDef>& declared,
                                      const ThemeResolver& library, int depth) {
  if (depth > 16) return std::nullopt;  // an `extends` cycle
  std::optional<ThemeDef> def;
  for (const auto& d : declared)
    if (d.name == name) def = d;
  if (!def && name.find('.') != std::string_view::npos && library) def = library(std::string(name));
  if (!def) {
    const Theme* builtin = find_theme(name);
    return builtin ? std::optional<Theme>(*builtin) : std::nullopt;
  }
  auto base = resolve_theme_at(def->base.value_or("default"), declared, library, depth + 1);
  if (!base) return std::nullopt;
  apply_theme(*base, *def);
  base->name = std::string(name);
  return base;
}

}  // namespace

std::optional<Theme> resolve_theme(std::string_view name, const std::vector<ThemeDef>& declared,
                                   const ThemeResolver& library) {
  return resolve_theme_at(name, declared, library, 0);
}

Theme theme_for(const ResolvedView& view, const ThemeResolver& library) {
  if (!view.theme) return default_theme();
  auto t = resolve_theme(*view.theme, view.themes, library);
  return t ? std::move(*t) : default_theme();
}

// ── shapes.ts ──────────────────────────────────────────────────────────────────────────────────

namespace {

const std::set<std::string, std::less<>> kBaseShapes{
    "rect",    "rounded", "ellipse", "circle", "rhombus", "triangle", "hexagon", "octagon",  "cylinder", "parallelogram",
    "trapezoid", "document", "cloud", "actor",  "person",  "note",     "card",    "step",     "arrow",    "process",
    "stadium", "cube",    "swimlane", "folder", "queue",   "interface", "text",   "image",    "invisible", "bar",
    "frame",   "module",  "lifeline"};

const std::map<std::string, std::string, std::less<>> kAliases{
    {"decision", "rhombus"}, {"start", "stadium"},     {"end", "stadium"},     {"database", "cylinder"},
    {"container", "rounded"}, {"component", "rounded"}, {"system", "rounded"}, {"process", "rounded"}};

}  // namespace

std::string base_shape(const std::optional<std::string>& shape, const std::string& kind) {
  if (shape) {
    if (kBaseShapes.count(*shape)) return *shape;
    if (auto it = kAliases.find(*shape); it != kAliases.end()) return it->second;
  }
  if (kind == "person") return "person";
  if (kind == "note") return "note";
  auto it = kAliases.find(kind);
  return it == kAliases.end() ? "rect" : it->second;
}

namespace {

// A UML frame (draw.io's umlFrame): an unfilled outline with the name in a filled tab at its top
// left, whose bottom right corner is cut off. The tab is at least draw.io's 60 × 30.
constexpr double kFrameTabWidth = 60;
constexpr double kFrameTabHeight = 30;
constexpr double kFrameTabCut = 10;

/// The tab of a frame on `b`, `width` × `height` at the least, never larger than the frame.
Box frame_tab(const Box& b, double width, double height) {
  return {b.x, b.y, std::min(b.width, std::max(kFrameTabWidth, width)),
          std::min(b.height, std::max(kFrameTabHeight, height))};
}

/// The outline and tab of a frame. The outline is unfilled but takes pointer events, so the web
/// canvas, which hit-tests the DOM, picks the frame in the empty parts of its box as the native
/// canvases do; frames are drawn behind the other nodes and the edges, so what is inside wins.
std::string frame_svg(const Box& b, const Box& tab, const std::string& paint, bool tab_takes_clicks = true) {
  const double x = tab.x, y = tab.y, w = tab.width, h = tab.height;
  return "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
         num(b.height) + "\" " + replace_fill(paint, "fill=\"none\"") + " pointer-events=\"all\"/>" +
         "<path d=\"M" + num(x) + "," + num(y) + " H" + num(x + w) + " V" + num(y + std::max(0.0, h - 1.5 * kFrameTabCut)) +
         " L" + num(x + std::max(0.0, w - kFrameTabCut)) + "," + num(y + h) + " H" + num(x) + " Z\" " + paint +
         (tab_takes_clicks ? "" : " pointer-events=\"none\"") + "/>";
}

/// A finite number for a style value, or nothing; `get` looks a key up.
template <class Get>
std::optional<double> style_number_of(const Get& get, std::string_view key) {
  const std::string* v = get(key);
  if (!v || v->empty()) return std::nullopt;
  double d = js_to_number(*v);
  return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

/// A cube's corners flipped for its direction: draw.io's default (east) has its depth at the top
/// left; the other directions turn it a quarter at a time, which for a cube is a mirror image.
struct CubeFlip {
  bool x = false, y = false;
};
/// The flip of a cube drawn draw.io's way: a size without a direction keeps DiaDraw's own corner,
/// the top right (draw.io's south).
CubeFlip cube_flip(const ShapeParams& params) {
  std::string_view direction = params.direction.empty() ? "south" : std::string_view(params.direction);
  return {direction == "south" || direction == "west", direction == "west" || direction == "north"};
}

/// Whether a cube is drawn draw.io's way, with its size and direction.
bool drawio_cube(const ShapeParams& params) { return params.size || !params.direction.empty(); }

// A UML lifeline (draw.io's umlLifeline): a head `size` deep (40 by default) at the top of its box
// and a dashed line down the middle from there to the bottom.
constexpr double kLifelineHead = 40;

/// The head of a lifeline on `b`: the top `size` of it, never deeper than the box.
Box lifeline_head(const Box& b, const ShapeParams& params) {
  return {b.x, b.y, b.width, std::clamp(params.size.value_or(kLifelineHead), 0.0, b.height)};
}

/// draw.io's UML stick figure; its `actor` is another shape (mxActor), drawn as a box here.
bool actor_participant(const ShapeParams& params) { return params.participant == "umlActor"; }

/// The head (a box, or a stick figure for an actor) and the line. The rest of the box is unpainted
/// but takes pointer events, so the web canvas picks the lifeline anywhere in its box as the
/// native canvases do; the activation bars and messages on it are drawn in front and win.
std::string lifeline_svg(const Box& b, const std::string& paint, const ShapeParams& params) {
  const Box head = lifeline_head(b, params);
  std::string out = "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
                    num(b.height) + "\" fill=\"none\" stroke=\"none\" pointer-events=\"all\"/>";
  if (head.height < b.height) {
    // A dashed style (dashed=1) already gives the paint its dashes.
    const bool dashed_paint = paint.find("stroke-dasharray=") != std::string::npos;
    out += "<path d=\"M" + num(b.x + b.width / 2) + "," + num(head.y + head.height) + " V" + num(b.y + b.height) +
           "\" " + replace_fill(paint, "fill=\"none\"") +
           (params.lifeline_dashed && !dashed_paint ? " stroke-dasharray=\"3 3\"" : "") + "/>";
  }
  if (actor_participant(params)) {
    out += shape_svg("actor", head, paint);
    return out;
  }
  out += "<rect x=\"" + num(head.x) + "\" y=\"" + num(head.y) + "\" width=\"" + num(head.width) + "\" height=\"" +
         num(head.height) + "\" " + paint + "/>";
  return out;
}

template <class Get>
ShapeParams shape_params_from(const Get& get) {
  ShapeParams p;
  auto non_negative = [&](std::string_view key) {
    auto v = style_number_of(get, key);
    return v && *v >= 0 ? v : std::nullopt;
  };
  p.size = non_negative("size");
  if (const std::string* d = get("direction"); d && (*d == "east" || *d == "south" || *d == "west" || *d == "north"))
    p.direction = *d;
  p.tab_width = non_negative("tabWidth");
  p.tab_height = non_negative("tabHeight");
  if (const std::string* tp = get("tabPosition")) p.tab_position = *tp;
  if (auto v = non_negative("jettyWidth")) p.jetty_width = *v;
  if (auto v = non_negative("jettyHeight")) p.jetty_height = *v;
  if (const std::string* v = get("participant")) p.participant = *v;
  if (const std::string* v = get("lifelineDashed")) p.lifeline_dashed = *v != "0";
  return p;
}

}  // namespace

ShapeParams shape_params(const Style& style) {
  return shape_params_from([&](std::string_view key) { return style.find(key); });
}

std::string shape_svg(const std::string& shape, const Box& b, const std::string& paint, bool rounded,
                      const ShapeParams& params) {
  const double x = b.x, y = b.y, w = b.width, h = b.height;
  auto poly = [&](std::initializer_list<std::pair<double, double>> pts) {
    std::string s = "<polygon points=\"";
    bool first = true;
    for (const auto& [px, py] : pts) {
      if (!first) s += " ";
      first = false;
      s += num(px) + "," + num(py);
    }
    return s + "\" " + paint + "/>";
  };
  auto rect = [&](double rx) {
    return "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\" rx=\"" +
           num(rx) + "\" " + paint + "/>";
  };
  auto path = [&](const std::string& d) { return "<path d=\"" + d + "\" " + paint + "/>"; };
  auto fill_none = [&] { return replace_fill(paint, "fill=\"none\""); };

  if (shape == "rect") return rect(rounded ? std::min(10.0, h / 4) : 0);
  if (shape == "rounded") return rect(std::min(8.0, h / 4));
  if (shape == "stadium") return rect(h / 2);
  if (shape == "ellipse")
    return "<ellipse cx=\"" + num(x + w / 2) + "\" cy=\"" + num(y + h / 2) + "\" rx=\"" + num(w / 2) + "\" ry=\"" +
           num(h / 2) + "\" " + paint + "/>";
  if (shape == "circle" || shape == "interface") {
    double r = std::min(w, h) / 2;
    return "<circle cx=\"" + num(x + w / 2) + "\" cy=\"" + num(y + h / 2) + "\" r=\"" + num(r) + "\" " + paint + "/>";
  }
  if (shape == "rhombus") return poly({{x + w / 2, y}, {x + w, y + h / 2}, {x + w / 2, y + h}, {x, y + h / 2}});
  if (shape == "triangle") return poly({{x, y}, {x + w, y + h / 2}, {x, y + h}});
  if (shape == "hexagon") {
    double d = std::min(w / 4, h / 2);
    return poly({{x + d, y}, {x + w - d, y}, {x + w, y + h / 2}, {x + w - d, y + h}, {x + d, y + h}, {x, y + h / 2}});
  }
  if (shape == "octagon") {
    double d = std::min(w, h) * 0.29;
    return poly({{x + d, y}, {x + w - d, y}, {x + w, y + d}, {x + w, y + h - d}, {x + w - d, y + h}, {x + d, y + h},
                 {x, y + h - d}, {x, y + d}});
  }
  if (shape == "parallelogram") {
    double d = std::min(w * 0.2, h);
    return poly({{x + d, y}, {x + w, y}, {x + w - d, y + h}, {x, y + h}});
  }
  if (shape == "trapezoid") {
    double d = std::min(w * 0.2, h);
    return poly({{x + d, y}, {x + w - d, y}, {x + w, y + h}, {x, y + h}});
  }
  if (shape == "step" || shape == "arrow") {
    double d = std::min(w * 0.25, h / 2);
    if (shape == "step")
      return poly({{x, y}, {x + w - d, y}, {x + w, y + h / 2}, {x + w - d, y + h}, {x, y + h}, {x + d, y + h / 2}});
    return poly({{x, y + h * 0.25},
                 {x + w - d, y + h * 0.25},
                 {x + w - d, y},
                 {x + w, y + h / 2},
                 {x + w - d, y + h},
                 {x + w - d, y + h * 0.75},
                 {x, y + h * 0.75}});
  }
  if (shape == "cylinder" || shape == "queue") {
    if (shape == "queue" && w > h) {
      double rx = std::min(10.0, w / 6);
      return path("M" + num(x + rx) + "," + num(y) + " h" + num(w - 2 * rx) + " a" + num(rx) + "," + num(h / 2) +
                  " 0 0 1 0," + num(h) + " h" + num(-(w - 2 * rx)) + " a" + num(rx) + "," + num(h / 2) + " 0 0 1 0," +
                  num(-h) + " z") +
             "<path d=\"M" + num(x + w - rx) + "," + num(y) + " a" + num(rx) + "," + num(h / 2) + " 0 0 0 0," + num(h) +
             "\" fill=\"none\" " + replace_fill(paint, "") + "/>";
    }
    double ry = std::min(10.0, h / 6);
    return path("M" + num(x) + "," + num(y + ry) + " v" + num(h - 2 * ry) + " a" + num(w / 2) + "," + num(ry) +
                " 0 0 0 " + num(w) + ",0 v" + num(-(h - 2 * ry)) + " a" + num(w / 2) + "," + num(ry) + " 0 0 0 " +
                num(-w) + ",0 z") +
           "<path d=\"M" + num(x) + "," + num(y + ry) + " a" + num(w / 2) + "," + num(ry) + " 0 0 0 " + num(w) + ",0\" " +
           fill_none() + "/>";
  }
  if (shape == "document") {
    double wave = h * 0.12;
    return path("M" + num(x) + "," + num(y) + " h" + num(w) + " v" + num(h - wave) + " q" + num(-w / 4) + "," +
                num(-wave * 1.6) + " " + num(-w / 2) + ",0 t" + num(-w / 2) + ",0 z");
  }
  if (shape == "cloud") {
    return path("M" + num(x + w * 0.25) + "," + num(y + h * 0.8) + " a" + num(w * 0.2) + "," + num(h * 0.25) +
                " 0 0 1 " + num(-w * 0.05) + "," + num(-h * 0.45) + " " + "a" + num(w * 0.25) + "," + num(h * 0.3) +
                " 0 0 1 " + num(w * 0.35) + "," + num(-h * 0.2) + " a" + num(w * 0.22) + "," + num(h * 0.28) +
                " 0 0 1 " + num(w * 0.35) + "," + num(h * 0.15) + " " + "a" + num(w * 0.18) + "," + num(h * 0.25) +
                " 0 0 1 " + num(-w * 0.02) + "," + num(h * 0.5) + " z");
  }
  // A stick figure across [fx, fx + fw] and the full height: the head takes the top quarter
  // (draw.io's umlActor proportions), narrowed to half the width for slim boxes; the arms at 0.35h
  // always clear it.
  auto stick_figure = [&](double fx, double fw, const std::string& head) {
    double r = std::min(fw / 4, h / 8);
    double cx = fx + fw / 2;
    return "<circle cx=\"" + num(cx) + "\" cy=\"" + num(y + r) + "\" r=\"" + num(r) + "\" " + head + "/>" +
           "<path d=\"M" + num(cx) + "," + num(y + 2 * r) + " V" + num(y + h * 0.65) + " M" + num(fx) + "," +
           num(y + h * 0.35) + " H" + num(fx + fw) + " M" + num(cx) + "," + num(y + h * 0.65) + " L" + num(fx) + "," +
           num(y + h) + " M" + num(cx) + "," + num(y + h * 0.65) + " L" + num(fx + fw) + "," + num(y + h) + "\" " +
           fill_none() + "/>";
  };
  // An actor stretches to its box and has a hollow head; pointer-events keeps the head clickable
  // on the web canvas, which hit-tests the DOM.
  if (shape == "actor") return stick_figure(x, w, fill_none() + " pointer-events=\"all\"");
  // A person stays upright, at most 0.6 as wide as tall, centred in its box; its head takes the
  // fill so the theme's person colour still shows.
  if (shape == "person") {
    double fw = std::min(w, h * 0.6);
    return stick_figure(x + (w - fw) / 2, fw, paint);
  }
  if (shape == "note" && params.size) {
    // draw.io's note: the corner folded down by `size`, the fold drawn over it.
    double f = std::min({*params.size, w, h});
    return path("M" + num(x) + "," + num(y) + " H" + num(x + w - f) + " L" + num(x + w) + "," + num(y + f) + " V" +
                num(y + h) + " H" + num(x) + " Z") +
           "<path d=\"M" + num(x + w - f) + "," + num(y) + " V" + num(y + f) + " H" + num(x + w) + "\" " + fill_none() +
           "/>";
  }
  if (shape == "note" || shape == "card") {
    double f = std::min({12.0, w / 4, h / 4});
    if (shape == "note")
      return path("M" + num(x) + "," + num(y) + " h" + num(w - f) + " l" + num(f) + "," + num(f) + " v" + num(h - f) +
                  " h" + num(-w) + " z");
    return path("M" + num(x + f) + "," + num(y) + " h" + num(w - f) + " v" + num(h) + " h" + num(-w) + " v" +
                num(-(h - f)) + " z");
  }
  if (shape == "process") {
    double d = std::min(w * 0.1, 12.0);
    return rect(0) + "<path d=\"M" + num(x + d) + "," + num(y) + " V" + num(y + h) + " M" + num(x + w - d) + "," +
           num(y) + " V" + num(y + h) + "\" " + fill_none() + "/>";
  }
  if (shape == "cube" && drawio_cube(params)) {
    // draw.io's cube: the front face and, `size` deep, the top and one side; east (its default)
    // puts the depth at the top left.
    const double s = std::min({params.size.value_or(20.0), w, h});
    const CubeFlip flip = cube_flip(params);
    auto px = [&](double u) { return num(flip.x ? x + w - u : x + u); };
    auto py = [&](double v) { return num(flip.y ? y + h - v : y + v); };
    auto pt = [&](double u, double v) { return px(u) + "," + py(v); };
    return "<path d=\"M" + pt(0, 0) + " L" + pt(w - s, 0) + " L" + pt(w, s) + " L" + pt(w, h) + " L" + pt(s, h) + " L" +
           pt(0, h - s) + " Z\" " + paint + "/>" + "<path d=\"M" + pt(s, h) + " L" + pt(s, s) + " L" + pt(0, 0) + " M" +
           pt(s, s) + " L" + pt(w, s) + "\" " + fill_none() + "/>";
  }
  if (shape == "cube") {
    double d = std::min(w, h) * 0.15;
    return poly({{x, y + d}, {x + d, y}, {x + w, y}, {x + w, y + h - d}, {x + w - d, y + h}, {x, y + h}}) +
           "<path d=\"M" + num(x) + "," + num(y + d) + " H" + num(x + w - d) + " V" + num(y + h) + " M" +
           num(x + w - d) + "," + num(y + d) + " L" + num(x + w) + "," + num(y) + "\" " + fill_none() + "/>";
  }
  if (shape == "swimlane") {
    double head = std::min(26.0, h);
    return rect(0) + "<path d=\"M" + num(x) + "," + num(y + head) + " H" + num(x + w) + "\" " + fill_none() + "/>";
  }
  if (shape == "folder" && (params.tab_width || params.tab_height)) {
    // draw.io's folder: a square tab on the top edge, left or right, over the body.
    const double tw = std::min(w, params.tab_width.value_or(60.0));
    const double th = std::min(h, params.tab_height.value_or(20.0));
    const bool left = params.tab_position == "left";
    const double tx = left ? x : x + w - tw;
    return path("M" + num(x) + "," + num(y + th) + " H" + num(tx) + " V" + num(y) + " H" + num(tx + tw) + " V" +
                num(y + th) + " H" + num(x + w) + " V" + num(y + h) + " H" + num(x) + " Z") +
           "<path d=\"M" + num(tx) + "," + num(y + th) + " H" + num(tx + tw) + "\" " + fill_none() + "/>";
  }
  if (shape == "module") {
    // draw.io's module (a UML component): two jetties stick out of its left edge, the body starts
    // half a jetty in.
    const double jw = std::min(params.jetty_width, w), jh = std::min(params.jetty_height, h);
    const double x0 = jw / 2;
    const double y0 = std::max(0.0, std::min(jh, h - jh));
    const double y1 = std::max(0.0, std::min(y0 + 2 * jh, h - jh));
    auto at = [&](double u, double v) { return num(x + u) + "," + num(y + v); };
    return path("M" + at(x0, 0) + " L" + at(w, 0) + " L" + at(w, h) + " L" + at(x0, h) + " L" + at(x0, y1 + jh) + " L" +
                at(0, y1 + jh) + " L" + at(0, y1) + " L" + at(x0, y1) + " L" + at(x0, y0 + jh) + " L" + at(0, y0 + jh) +
                " L" + at(0, y0) + " L" + at(x0, y0) + " Z") +
           "<path d=\"M" + at(x0, y0) + " L" + at(jw, y0) + " L" + at(jw, y0 + jh) + " L" + at(x0, y0 + jh) + " M" +
           at(x0, y1) + " L" + at(jw, y1) + " L" + at(jw, y1 + jh) + " L" + at(x0, y1 + jh) + "\" " + fill_none() + "/>";
  }
  if (shape == "folder") {
    double tab = std::min(w * 0.35, 40.0);
    return path("M" + num(x) + "," + num(y + 8) + " v" + num(h - 8) + " h" + num(w) + " v" + num(-(h - 8)) + " h" +
                num(-(w - tab)) + " l-6,-8 h" + num(-(tab - 6)) + " z");
  }
  if (shape == "frame") return frame_svg(b, frame_tab(b, 0, 0), paint);
  if (shape == "lifeline") return lifeline_svg(b, paint, params);
  if (shape == "image") {
    // A filled icon box stands for the icon; an unfilled one shows an image placeholder.
    if (paint.find("fill=\"none\"") == std::string::npos && paint.find("fill=\"#ffffff\"") == std::string::npos)
      return rect(std::min(4.0, h / 8));
    return rect(0) + "<path d=\"M" + num(x) + "," + num(y) + " L" + num(x + w) + "," + num(y + h) + " M" + num(x + w) +
           "," + num(y) + " L" + num(x) + "," + num(y + h) + "\" " + fill_none() + " stroke-opacity=\"0.3\"/>";
  }
  if (shape == "bar")
    return "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) +
           "\" rx=\"2\" fill=\"#1f2937\" stroke=\"#1f2937\"/>";
  return "";  // text, invisible
}

// ── Text ───────────────────────────────────────────────────────────────────────────────────────

namespace {

void escape_in_place(std::string& s) {
  if (s.find_first_of("&<>\"") != std::string::npos) s = escape_xml(s);
}

/// `t` with every colour and the font family escaped for an attribute value. Themes come from
/// documents, libraries and hosts, and the renderer writes their values into attributes as they
/// are, so it draws with this copy only.
Theme attribute_safe(Theme t) {
  for (std::string* s : {&t.background, &t.font_family, &t.edge, &t.edge_text, &t.boundary, &t.boundary_text,
                         &t.surface, &t.divider_fill, &t.label_text})
    escape_in_place(*s);
  auto node = [](NodeStyle& n) {
    escape_in_place(n.fill);
    escape_in_place(n.stroke);
    escape_in_place(n.text);
  };
  for (auto& [_, n] : t.nodes) node(n);
  node(t.external);
  return t;
}

}  // namespace

namespace {

/// A code point's estimated width, in ems.
double glyph_units(std::uint32_t ch) {
  if (ch < 0x80 && std::string_view("il.,:;|!'`").find(static_cast<char>(ch)) != std::string_view::npos) return 0.3;
  if (ch < 0x80 && std::string_view("mwMW@").find(static_cast<char>(ch)) != std::string_view::npos) return 0.85;
  if (ch == ' ') return 0.3;
  if (ch >= 'A' && ch <= 'Z') return 0.66;
  return 0.55;
}

/// `units` plus the width of each code point of `text`, added in order.
double add_units(double units, std::string_view text) {
  for (std::size_t i = 0; i < text.size();) units += glyph_units(next_code_point(text, i));
  return units;
}

}  // namespace

double text_width(std::string_view text, double font_size) { return add_units(0, text) * font_size; }

std::vector<std::string> wrap_text(std::string_view content, double width, double font_size) {
  std::vector<std::string> lines;
  for (const auto& paragraph : split(content, '\n')) {
    // paragraph.split(/\s+/).filter(Boolean)
    std::vector<std::string> words;
    std::string word;
    for (std::size_t i = 0; i < paragraph.size();) {
      std::size_t start = i;
      std::uint32_t cp = next_code_point(paragraph, i);
      if (is_js_whitespace(cp)) {
        if (!word.empty()) words.push_back(std::move(word));
        word.clear();
      } else {
        word.append(paragraph, start, i - start);
      }
    }
    if (!word.empty()) words.push_back(std::move(word));
    // The line's width is kept as it grows, in the same order text_width(line) adds it up, so each
    // word costs its own length rather than the line's.
    std::string line;
    double units = 0;
    for (auto& w : words) {
      if (line.empty()) {
        units = add_units(0, w);
        line = std::move(w);
        continue;
      }
      double next = add_units(units + glyph_units(' '), w);
      if (next * font_size > width) {
        lines.push_back(std::move(line));
        units = add_units(0, w);
        line = std::move(w);
      } else {
        units = next;
        line += ' ';
        line += w;
      }
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

namespace {

struct TextOpts {
  std::string anchor = "middle";
  std::string weight;
  bool italic = false;
  bool underline = false;
  bool strike = false;
};

/// The font-style and text-decoration attributes of italic, underlined or struck-through text.
std::string slant_and_decoration(bool italic, bool underline, bool strike) {
  std::string out = italic ? " font-style=\"italic\"" : "";
  if (underline || strike)
    out += std::string(" text-decoration=\"") + (underline ? "underline" : "") + (underline && strike ? " " : "") +
           (strike ? "line-through" : "") + "\"";
  return out;
}

/// The opening tag of a <text>, without its weight, slant and decoration. `t` is attribute-safe.
std::string text_open(Point at, double size, const Theme& t, const std::string& anchor) {
  return "<text x=\"" + num(at.x) + "\" y=\"" + num(at.y) + "\" text-anchor=\"" + anchor +
         "\" dominant-baseline=\"central\" " + "font-family=\"" + t.font_family + "\" font-size=\"" +
         num(size) + "\"";
}

std::string text(const std::string& content, Point at, const std::string& fill, double size, const Theme& t,
                 const TextOpts& o = {}) {
  return text_open(at, size, t, o.anchor) + (o.weight.empty() ? "" : " font-weight=\"" + o.weight + "\"") +
         slant_and_decoration(o.italic, o.underline, o.strike) + " fill=\"" + fill + "\">" + escape_xml(content) +
         "</text>";
}

std::optional<ShapeArt> art_of(const Element& e, const ShapeResolver& shapes) {
  if (!e.shape || e.shape->find('.') == std::string::npos || !shapes) return std::nullopt;
  return shapes(*e.shape);
}

Size icon_size(const ShapeArt& art) {
  double w = art.width.value_or(48);
  double h = art.height.value_or(48);
  double scale = std::min({1.0, 96 / w, 64 / h});
  return {w * scale, h * scale};
}

std::optional<std::string> tech_line(const Element& e) {
  if (!e.technology) return std::nullopt;
  return "[" + *e.technology + "]";
}

bool has_tag(const Element& e, std::string_view tag) {
  return std::find(e.tags.begin(), e.tags.end(), tag) != e.tags.end();
}

const NodeStyle& node_style(const Element& e, const Theme& t) {
  if (has_tag(e, "external")) return t.external;
  auto it = t.nodes.find(e.kind);
  return it == t.nodes.end() ? t.nodes.at("default") : it->second;
}

struct Paint {
  std::string fill;
  std::string stroke;
  double stroke_width = 1.5;
  bool dashed = false;
  /// The `stroke-dasharray` of a dashed outline: its `dashPattern`, or plain dashes.
  std::string dash_pattern = "6 4";
  double opacity = 1;
  bool rounded = false;
  std::string text;
  double font_size = 13;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strike = false;
  std::string align = "center";
  std::string vertical_align = "middle";
  /// The outline's shape keys (cube depth, folder tab, …).
  ShapeParams shape;
  /// draw.io's spacingTop, spacingLeft, spacingRight and spacingBottom: room kept clear around
  /// the label inside the shape.
  double spacing_top = 0, spacing_left = 0, spacing_right = 0, spacing_bottom = 0;
  /// draw.io's boundedLbl: the label stays inside the shape's face (off a cube's depth, below a
  /// note's fold or a folder's tab).
  bool bounded_label = false;
  /// lineFontStyles: the fontStyle bits of each line of the name, when the lines differ.
  std::vector<int> line_font_styles;
  /// fontRuns, read when the name is laid out (name_lines).
  std::string font_runs;
};

/// An item's inline style over the style its `style` blocks give it.
struct Look {
  const std::optional<Style>& inline_style;
  const Style& rules;
  [[nodiscard]] const std::string* get(std::string_view key) const {
    const std::string* v = style_value(inline_style, key);
    return v ? v : rules.find(key);
  }
  [[nodiscard]] bool is(std::string_view key, std::string_view value) const {
    const std::string* v = get(key);
    return v && *v == value;
  }
  [[nodiscard]] double number(std::string_view key, double fallback) const {
    const std::string* v = get(key);
    return v ? js_to_number(*v) : fallback;
  }
};

/// The fontStyle bits of each code point of `name` that fontRuns (`count:bits,…`, counting code
/// points, line breaks too) gives; empty when the runs are malformed or do not add up to the name.
std::vector<int> font_run_bits(std::string_view runs, std::string_view name) {
  std::size_t points = 0;
  for (std::size_t i = 0; i < name.size(); ++points) next_code_point(name, i);
  std::vector<int> out;
  out.reserve(points);
  for (const auto& run : split(runs, ',')) {
    const auto colon = run.find(':');
    if (colon == std::string::npos) return {};
    const double count = js_to_number(run.substr(0, colon));
    // Also rejects NaN; a count past the name is caught before it is used.
    if (!(count >= 0 && count <= static_cast<double>(points - out.size())) || count != std::floor(count)) return {};
    out.insert(out.end(), static_cast<std::size_t>(count), js_to_int32(js_to_number(run.substr(colon + 1))) & 15);
  }
  if (out.size() != points) return {};
  return out;
}

const Style& no_rules() {
  static const Style none;
  return none;
}

Paint element_paint(const Element& e, const Theme& t, const Style& rules = no_rules()) {
  const NodeStyle& theme = node_style(e, t);
  // Only an inline style switches to draw.io's defaults; `style` blocks restyle the theme's look.
  bool styled = e.style.has_value();
  Look st{e.style, rules};
  auto is = [&](std::string_view key, std::string_view value) { return st.is(key, value); };
  auto font_style = js_to_int32(st.number("fontStyle", 0));
  Paint p;
  // A styled element without a fill is drawn the way draw.io draws it: white.
  p.fill = color(st.get("fill"), styled ? "#ffffff" : theme.fill);
  p.stroke = color(st.get("stroke"), styled ? "#000000" : theme.stroke);
  p.stroke_width = st.number("strokeWidth", 1.5);
  p.dashed = is("dashed", "1");
  if (const std::string* pattern = st.get("dashPattern"); pattern && !pattern->empty()) p.dash_pattern = *pattern;
  const std::string* opacity = st.get("opacity");
  p.opacity = opacity ? js_to_number(*opacity) / 100 : 1;
  p.rounded = is("rounded", "1");
  p.text = color(st.get("fontColor"), styled ? "#000000" : theme.text);
  p.font_size = st.number("fontSize", t.font_size);
  // Model elements are bold unless a style says otherwise.
  bool font_set = st.get("bold") || st.get("fontStyle");
  p.bold = font_set ? is("bold", "1") || (font_style & 1) == 1 : !styled;
  p.italic = is("italic", "1") || (font_style & 2) == 2;
  p.underline = (font_style & 4) == 4;
  p.strike = (font_style & 8) == 8;
  p.align = is("align", "left") ? "left" : is("align", "right") ? "right" : "center";
  p.vertical_align = is("verticalAlign", "top") ? "top" : is("verticalAlign", "bottom") ? "bottom" : "middle";
  auto get = [&](std::string_view key) { return st.get(key); };
  p.shape = shape_params_from(get);
  auto spacing = [&](std::string_view key) {
    auto v = style_number_of(get, key);
    return v ? std::max(0.0, *v) : 0.0;
  };
  p.spacing_top = spacing("spacingTop");
  p.spacing_left = spacing("spacingLeft");
  p.spacing_right = spacing("spacingRight");
  p.spacing_bottom = spacing("spacingBottom");
  p.bounded_label = is("boundedLbl", "1");
  if (const std::string* lines = st.get("lineFontStyles")) {
    for (const auto& part : split(*lines, ',')) {
      p.line_font_styles.push_back(js_to_int32(js_to_number(part)) & 15);
    }
  }
  if (const std::string* runs = st.get("fontRuns")) p.font_runs = *runs;
  return p;
}

std::string paint_attrs(const Paint& p) {
  std::string out = "fill=\"" + p.fill + "\" stroke=\"" + p.stroke + "\" stroke-width=\"" + num(p.stroke_width) + "\"";
  if (p.dashed) out += " stroke-dasharray=\"" + escape_xml(p.dash_pattern) + "\"";
  if (p.opacity < 1) out += " opacity=\"" + num(p.opacity) + "\"";
  return out;
}

/// A run of a line in its own fontStyle bits.
struct Span {
  std::string text;
  int font_style = 0;
};

struct Line {
  Line(std::string text_in, double size_in, bool bold_in = false, std::optional<int> font_style_in = std::nullopt)
      : text(std::move(text_in)), size(size_in), bold(bold_in), font_style(font_style_in) {}
  std::string text;
  double size;
  bool bold = false;
  /// The line's own fontStyle bits (lineFontStyles), over the paint's bold, italic, underline and
  /// strike-through.
  std::optional<int> font_style;
  /// The runs of `text` when they differ in fontStyle (fontRuns): their bits stand for the
  /// line's and the paint's.
  std::vector<Span> spans;
};

/// The height text_block gives `lines`.
double lines_height(const std::vector<Line>& lines) {
  double total = 0;
  for (const auto& l : lines) total += l.size * 1.25;
  return total;
}

std::string text_block(const std::vector<Line>& lines, const Box& b, const Paint& p, const Theme& t, double offset_y = 0) {
  auto line_height = [](double size) { return size * 1.25; };
  double total = lines_height(lines);
  const double inset = 4;
  double y = p.vertical_align == "top"      ? b.y + inset
             : p.vertical_align == "bottom" ? b.y + b.height - inset - total
                                            : b.y + b.height / 2 - total / 2 + offset_y;
  double x = p.align == "left" ? b.x + inset : p.align == "right" ? b.x + b.width - inset : b.x + b.width / 2;
  std::string anchor = p.align == "left" ? "start" : p.align == "right" ? "end" : "middle";
  std::string out;
  for (const auto& l : lines) {
    double h = line_height(l.size);
    if (!l.spans.empty()) {
      out += text_open({x, y + h / 2}, l.size, t, anchor) + " fill=\"" + p.text + "\">";
      for (const auto& sp : l.spans) {
        const int bits = sp.font_style;
        out += std::string("<tspan") + ((bits & 1) != 0 ? " font-weight=\"600\"" : "") +
               slant_and_decoration((bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0) + ">" + escape_xml(sp.text) + "</tspan>";
      }
      out += "</text>";
    } else if (!l.text.empty()) {
      TextOpts o;
      o.anchor = anchor;
      const bool bold = l.font_style ? (*l.font_style & 1) != 0 : l.bold;
      if (bold) o.weight = "600";
      o.italic = l.font_style ? (*l.font_style & 2) != 0 : p.italic;
      o.underline = l.font_style ? (*l.font_style & 4) != 0 : p.underline;
      o.strike = l.font_style ? (*l.font_style & 8) != 0 : p.strike;
      out += text(l.text, {x, y + h / 2}, p.text, l.size, t, o);
    }
    y += h;
  }
  return out;
}

std::vector<Line> lines_of(const std::vector<std::string>& texts, double size, bool bold) {
  std::vector<Line> out;
  for (const auto& s : texts) out.push_back({s, size, bold});
  return out;
}

/// `name` wrapped to `width` as wrap_text wraps it, each line in runs of the fontStyle bits
/// `bits` gives its code points (one per code point of the name). A line all in one style takes
/// it as its font_style; a space wrap_text puts between two words takes the style of the first
/// blank between them.
std::vector<Line> wrap_styled_name(std::string_view name, const std::vector<int>& bits, double width, double size,
                                   bool bold) {
  struct Word {
    std::vector<Span> chars;  // one per code point
    int gap = 0;              // the style of the blank before it
  };
  std::vector<Line> out;
  std::size_t i = 0, k = 0;
  while (true) {
    // One paragraph: up to the next line break.
    const std::size_t start = i;
    std::vector<Word> words;
    std::optional<int> gap;
    bool in_word = false;
    while (i < name.size() && name[i] != '\n') {
      const std::size_t at = i;
      const std::uint32_t cp = next_code_point(name, i);
      const int b = k < bits.size() ? bits[k] : 0;
      ++k;
      if (is_js_whitespace(cp)) {
        in_word = false;
        if (!gap) gap = b;
        continue;
      }
      if (!in_word) words.push_back({{}, gap.value_or(0)});
      in_word = true;
      gap.reset();
      words.back().chars.push_back({std::string(name.substr(at, i - at)), b});
    }
    const std::string_view paragraph = name.substr(start, i - start);
    std::size_t next = 0;
    for (auto& text : wrap_text(paragraph, width, size)) {
      std::vector<Span> spans;
      auto add = [&](const Span& c) {
        if (!spans.empty() && spans.back().font_style == c.font_style) spans.back().text += c.text;
        else spans.push_back(c);
      };
      const std::size_t count =
          text.empty() ? 0 : static_cast<std::size_t>(std::count(text.begin(), text.end(), ' ')) + 1;
      for (std::size_t w = 0; w < count && next < words.size(); ++w, ++next) {
        if (w > 0) add({" ", words[next].gap});
        for (const auto& c : words[next].chars) add(c);
      }
      Line line(std::move(text), size, bold);
      if (spans.size() == 1) line.font_style = spans.front().font_style;
      else if (spans.size() > 1) line.spans = std::move(spans);
      out.push_back(std::move(line));
    }
    if (i >= name.size()) break;
    ++i;  // the line break
    ++k;
  }
  return out;
}

/// The widest box a description widens an element to; longer lines wrap.
constexpr double kDescriptionWidth = 240;
/// The space between the name (and technology) and the description, as an empty line of this size.
constexpr double kDescriptionGap = 4;

/// The description as drawn: without trailing blanks and line breaks, and empty when it is blank.
std::string_view drawn_description(const Element& e) {
  // Whitespace as wrap_text drops it, so a blank last line takes no room.
  return e.description ? trim_end(*e.description) : std::string_view();
}

/// An element's description under its label: a small gap, then its lines in the theme's small type,
/// wrapped to `width`. Line breaks in the description are kept; so are blank lines inside it.
void append_description(std::vector<Line>& lines, const Element& e, double width, const Theme& t) {
  std::string_view d = drawn_description(e);
  if (d.empty()) return;
  lines.push_back({"", kDescriptionGap, false});
  for (auto& s : wrap_text(d, width, t.small_font_size)) lines.push_back({std::move(s), t.small_font_size, false});
}

/// The height append_description adds for a description wrapped to `width`, as lines_height adds
/// it up.
double description_height(const Element& e, double width, const Theme& t) {
  std::string_view d = drawn_description(e);
  if (d.empty()) return 0;
  double h = kDescriptionGap * 1.25;
  for (std::size_t n = wrap_text(d, width, t.small_font_size).size(); n > 0; --n) h += t.small_font_size * 1.25;
  return h;
}

/// The width a description asks of its element's box (with `padding` beside the text): its widest
/// line, up to kDescriptionWidth.
double description_width(const Element& e, double padding, const Theme& t) {
  std::string_view d = drawn_description(e);
  if (d.empty()) return 0;
  double widest = 0;
  for (std::size_t start = 0; start <= d.size();) {
    std::size_t nl = std::min(d.find('\n', start), d.size());
    widest = std::max(widest, text_width(d.substr(start, nl - start), t.small_font_size));
    start = nl + 1;
  }
  return std::min(std::ceil(widest + padding), kDescriptionWidth);
}

/// How much of a node's width its label wraps to: less in a diamond or an ellipse, whose sides
/// close in. `shape` is the base shape.
double label_width_factor(std::string_view shape) { return shape == "rhombus" || shape == "ellipse" ? 0.75 : 1; }

/// Whether a node of this base shape shows its description. A circle keeps its size from its short
/// side, so it shows the name only.
bool shows_description(std::string_view shape) { return shape != "circle" && shape != "interface"; }

// ── Classes ──

bool is_class_like(const Element& e) {
  return e.kind == "class" || e.kind == "object" || prop(e, "uml.attributes") || prop(e, "uml.methods");
}

struct ClassContent {
  std::string title;
  std::optional<std::string> stereotype;
  std::vector<std::string> attributes;
  std::vector<std::string> methods;
};

ClassContent class_content(const Element& e) {
  auto split_prop = [&](std::string_view key) {
    const std::string* v = prop(e, key);
    return v && !v->empty() ? split(*v, '\n') : std::vector<std::string>{};
  };
  ClassContent c{e.name, std::nullopt, split_prop("uml.attributes"), split_prop("uml.methods")};
  if (const std::string* st = prop(e, "stereotype"); st && !st->empty()) c.stereotype = "\xC2\xAB" + *st + "\xC2\xBB";
  return c;
}

constexpr double kLine = 16;

struct ClassHeights {
  double head, attrs, methods, total;
};

ClassHeights class_heights(const ClassContent& c) {
  double head = c.stereotype ? 40 : 28;
  double attrs = static_cast<double>(std::max<std::size_t>(1, c.attributes.size())) * kLine + 8;
  double methods = c.methods.empty() ? 0 : static_cast<double>(c.methods.size()) * kLine + 8;
  return {head, attrs, methods, head + attrs + methods};
}

std::string class_box(const Element& e, const Box& b, const Theme& t, const Style& rules) {
  Paint p = element_paint(e, t, rules);
  ClassContent c = class_content(e);
  ClassHeights h = class_heights(c);
  bool object = e.kind == "object";
  std::string out = "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
                    num(b.height) + "\" " + paint_attrs(p) + "/>";
  double cx = b.x + b.width / 2;
  if (c.stereotype) {
    TextOpts o;
    o.italic = true;
    out += text(*c.stereotype, {cx, b.y + 12}, p.text, t.small_font_size, t, o);
  }
  TextOpts title;
  title.weight = "600";
  const std::string st = c.stereotype.value_or("");
  title.italic = st.find("abstract") != std::string::npos || st.find("interface") != std::string::npos;
  title.underline = object;
  out += text(c.title, {cx, b.y + h.head - 13}, p.text, t.font_size, t, title);
  auto line = [&](double y) {
    return "<path d=\"M" + num(b.x) + "," + num(y) + " H" + num(b.x + b.width) + "\" stroke=\"" + p.stroke +
           "\" stroke-width=\"1\"/>";
  };
  out += line(b.y + h.head);
  TextOpts start;
  start.anchor = "start";
  for (std::size_t i = 0; i < c.attributes.size(); ++i)
    out += text(c.attributes[i], {b.x + 8, b.y + h.head + 12 + static_cast<double>(i) * kLine}, p.text,
                t.small_font_size + 1, t, start);
  if (!c.methods.empty()) {
    out += line(b.y + h.head + h.attrs);
    for (std::size_t i = 0; i < c.methods.size(); ++i)
      out += text(c.methods[i], {b.x + 8, b.y + h.head + h.attrs + 12 + static_cast<double>(i) * kLine}, p.text,
                  t.small_font_size + 1, t, start);
  }
  return out;
}

// ── Library drawings ──

std::string recolor(const ShapeArt& art, const std::optional<Style>& style) {
  std::string body = art.body.value_or("");
  auto swap = [&](const std::vector<std::string>& from, const std::string* to) {
    if (from.empty() || !to || to->empty() || *to == "default") return;
    for (const auto& c : from) body = replace_all(body, "\"" + c + "\"", "\"" + escape_xml(*to) + "\"");
  };
  auto pick = [&](std::string_view a, std::string_view b) {
    const std::string* v = style_value(style, a);
    return v ? v : style_value(style, b);
  };
  swap(art.fill_colors, pick("fill", "fillColor"));
  swap(art.stroke_colors, pick("stroke", "strokeColor"));
  return body;
}

/// Where a style puts the label outside the shape (draw.io's labelPosition /
/// verticalLabelPosition); actors default to below.
struct LabelPlacement {
  bool below = false;
  bool above = false;
  std::string side;  // "left", "right" or empty
  [[nodiscard]] bool outside() const { return below || above || !side.empty(); }
};

LabelPlacement label_placement(const Element& e, std::string_view shape) {
  const std::string* vlp = style_value(e.style, "verticalLabelPosition");
  const std::string* lp = style_value(e.style, "labelPosition");
  LabelPlacement out;
  out.below = (vlp && *vlp == "bottom") || (shape == "actor" && (!vlp || vlp->empty()));
  out.above = vlp && *vlp == "top";
  if (lp && (*lp == "left" || *lp == "right")) out.side = *lp;
  return out;
}

// A person is a stick figure with its name (and technology) underneath, both inside its box.
// Measure sizes the box for the figure's natural height plus the label.
constexpr double kPersonFigureHeight = 56;

std::vector<Line> person_lines(const Element& e, double width, double font_size, bool bold, const Theme& t) {
  auto lines = lines_of(wrap_text(e.name, std::max(20.0, width - 10), font_size), font_size, bold);
  if (auto tech = tech_line(e)) lines.push_back({*tech, t.small_font_size, false});
  append_description(lines, e, std::max(20.0, width - 10), t);
  return lines;
}

/// The label's height as text_block lays it out from the top (4 px inset each side).
double person_label_height(const std::vector<Line>& lines) { return 8 + lines_height(lines); }

/// The figure fills the top of the box down to the label. When the box is too short for both, it
/// keeps half the box, or its natural height when that is less, and the label runs past the bottom.
std::string person_node(const Element& e, const Box& b, const Paint& p, const Theme& t, const Style& rules) {
  auto lines = person_lines(e, b.width, p.font_size, p.bold, t);
  double figure_h = std::max(b.height - person_label_height(lines), std::min(b.height / 2, kPersonFigureHeight));
  Paint label = p;
  // The label sits on the canvas, not on a fill, so it takes the theme's labelText unless a style
  // block sets fontColor; an inline style keeps draw.io's colours (black unless it sets one). A
  // theme's `node person { text }` is the text on a filled person's shape.
  if (!e.style && !rules.find("fontColor")) label.text = t.label_text;
  label.align = "center";
  label.vertical_align = "top";
  // An invisible box keeps the whole element, label included, clickable on the web canvas.
  return "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
         num(b.height) + "\" fill=\"none\" stroke=\"none\" pointer-events=\"all\"/>" +
         shape_svg("person", Box{b.x, b.y, b.width, figure_h}, paint_attrs(p)) +
         text_block(lines, Box{b.x, b.y + figure_h, b.width, b.height - figure_h}, label, t);
}

/// The label inside a node: name, technology and (unless `description` is false) description.
std::string node_label(const Element& e, const Box& b, const Paint& p, const Theme& t, const std::string& shape,
                       bool description = true);

std::string library_node(const Element& e, const Box& b, const ShapeArt& art, const Theme& t, const Style& rules) {
  std::string default_view_box = "0 0 " + js_number_string(art.width.value_or(48)) + " " +
                                 js_number_string(art.height.value_or(48));
  std::string view_box_of = art.view_box.value_or(default_view_box);
  if (e.style) {
    const std::string* tile_color = style_value(e.style, "iconBackground");
    bool tile_on = tile_color && !tile_color->empty();
    double inset = tile_on ? std::min(b.width, b.height) * 0.15 : 0;
    std::string tile = tile_on ? "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) +
                                     "\" height=\"" + num(b.height) + "\" fill=\"" + escape_xml(*tile_color) + "\"/>"
                               : "";
    const std::string* aspect_style = style_value(e.style, "aspect");
    std::string aspect = (aspect_style && *aspect_style == "fixed") || tile_on ? "xMidYMid meet" : "none";
    return tile + "<svg x=\"" + num(b.x + inset) + "\" y=\"" + num(b.y + inset) + "\" width=\"" +
           num(b.width - 2 * inset) + "\" height=\"" + num(b.height - 2 * inset) + "\" viewBox=\"" +
           escape_xml(view_box_of) + "\" preserveAspectRatio=\"" + aspect + "\" overflow=\"visible\">" +
           recolor(art, e.style) + "</svg>" +
           // The name sits over the drawing; a description there would cover it.
           node_label(e, b, element_paint(e, t, rules), t, "rect", false);
  }
  if (art.label_inside) {
    Paint p = element_paint(e, t, rules);
    p.text = t.label_text;
    auto lines = lines_of(wrap_text(e.name, b.width - 12, p.font_size), p.font_size, p.bold);
    Paint centred = p;
    centred.vertical_align = "middle";
    centred.align = "center";
    return "<svg x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
           num(b.height) + "\" viewBox=\"" + escape_xml(view_box_of) +
           "\" preserveAspectRatio=\"none\" overflow=\"visible\">" + art.body.value_or("undefined") + "</svg>" +
           text_block(lines, b, centred, t);
  }
  Size natural = icon_size(art);
  double line_h = element_paint(e, t, rules).font_size * 1.25;
  // A box sized to the drawing alone gets the drawing filling it and the label underneath.
  bool outside = b.height < natural.height + 8 + line_h;
  double scale = outside ? std::min(b.width / natural.width, b.height / natural.height) : 1;
  Size icon{natural.width * scale, natural.height * scale};
  double x = b.x + (b.width - icon.width) / 2;
  double y = outside ? b.y + (b.height - icon.height) / 2 : b.y + 4;
  std::string view_box = art.view_box.value_or(default_view_box);
  Paint p = element_paint(e, t, rules);
  p.fill = "none";
  p.stroke = "none";
  p.vertical_align = "top";
  auto tech = tech_line(e);
  double wrap_width = outside ? std::max(b.width + 60, 100.0) : b.width - 8;
  auto lines = lines_of(wrap_text(e.name, wrap_width, p.font_size), p.font_size, p.bold);
  if (tech) lines.push_back({*tech, t.small_font_size, false});
  append_description(lines, e, wrap_width, t);
  Box label_box = outside ? Box{b.x + b.width / 2 - wrap_width / 2, b.y + b.height + 4, wrap_width, lines_height(lines)}
                          : Box{b.x, y + icon.height + 4, b.width, b.y + b.height - (y + icon.height + 4)};
  Paint label_paint = p;
  label_paint.text = color(rules.find("fontColor"), t.label_text);
  return "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
         num(b.height) + "\" fill=\"none\" stroke=\"none\"/>" + "<svg x=\"" + num(x) + "\" y=\"" + num(y) +
         "\" width=\"" + num(icon.width) + "\" height=\"" + num(icon.height) + "\" viewBox=\"" + escape_xml(view_box) +
         "\" preserveAspectRatio=\"xMidYMid meet\">" + art.body.value_or("undefined") + "</svg>" +
         text_block(lines, label_box, label_paint, t);
}

/// The name wrapped to `width`, each line in its own font style where lineFontStyles gives one per
/// line of the name (an edited name with another number of lines takes the element's).
std::vector<Line> name_lines(const Element& e, double width, const Paint& p) {
  if (!p.font_runs.empty())
    if (auto bits = font_run_bits(p.font_runs, e.name); !bits.empty())
      return wrap_styled_name(e.name, bits, width, p.font_size, p.bold);
  if (p.line_font_styles.empty()) return lines_of(wrap_text(e.name, width, p.font_size), p.font_size, p.bold);
  const auto source = split(e.name, '\n');
  if (p.line_font_styles.size() != source.size())
    return lines_of(wrap_text(e.name, width, p.font_size), p.font_size, p.bold);
  std::vector<Line> out;
  for (std::size_t i = 0; i < source.size(); ++i)
    for (auto& part : wrap_text(source[i], width, p.font_size))
      out.push_back({std::move(part), p.font_size, p.bold, p.line_font_styles[i]});
  return out;
}

/// The part of a node's box its label goes in: less the style's spacing and, with boundedLbl, the
/// parts of the outline that are not its face (a cube's depth, a note's fold, a folder's tab).
Box label_area(const Box& b, const Paint& p, std::string_view shape) {
  double top = p.spacing_top, left = p.spacing_left, right = p.spacing_right, bottom = p.spacing_bottom;
  const ShapeParams& sp = p.shape;
  if (p.bounded_label) {
    if (shape == "note" && sp.size) top += std::min({*sp.size, b.width, b.height});
    if (shape == "cube" && drawio_cube(sp)) {
      const double depth = std::min({sp.size.value_or(20.0), b.width, b.height});
      const CubeFlip flip = cube_flip(sp);
      (flip.x ? right : left) += depth;
      (flip.y ? bottom : top) += depth;
    }
    if (shape == "folder" && (sp.tab_width || sp.tab_height)) top += std::min(b.height, sp.tab_height.value_or(20.0));
  }
  return {b.x + left, b.y + top, std::max(0.0, b.width - left - right), std::max(0.0, b.height - top - bottom)};
}

std::string node_label(const Element& e, const Box& box, const Paint& p, const Theme& t, const std::string& shape,
                       bool description) {
  auto tech = tech_line(e);
  const Box b = label_area(box, p, shape);
  double width = std::max(20.0, label_width_factor(shape) * b.width - 10);
  auto lines = name_lines(e, width, p);
  if (tech) lines.push_back({*tech, t.small_font_size, false});
  if (description && shows_description(shape)) append_description(lines, e, width, t);
  double offset = shape == "cylinder" ? 4 : 0;
  const LabelPlacement place = label_placement(e, shape);
  const bool below = place.below;
  const std::string& side = place.side;
  if (place.outside()) {
    double line_h = p.font_size * 1.25;
    // Outside the shape, the label goes by the whole box.
    const Box& n = box;
    auto outside = name_lines(e, std::max(n.width + 80, 120.0), p);
    double h = line_h * static_cast<double>(outside.size());
    Box at = !side.empty() ? Box{side == "left" ? n.x - 4 - 200 : n.x + n.width + 4, n.y, 200, n.height}
                           : Box{n.x - 40, below ? n.y + n.height + 2 : n.y - 2 - h, n.width + 80, h};
    Paint q = p;
    q.align = side == "left" ? "right" : side == "right" ? "left" : "center";
    q.vertical_align = !side.empty() ? p.vertical_align : "top";
    return text_block(outside, at, q, t);
  }
  return text_block(lines, b, p, t, offset);
}

/// The width a frame's tab takes for `name`: its widest line with room for the cut corner.
double frame_tab_width(std::string_view name, double font_size) {
  double widest = 0;
  for (const auto& line : split(name, '\n')) widest = std::max(widest, text_width(line, font_size));
  return std::ceil(widest + kFrameTabCut + 10);
}

/// A container frame's name, on one line.
std::string frame_container_name(const Element& e) {
  std::string name = e.name;
  std::replace(name.begin(), name.end(), '\n', ' ');
  return name;
}

/// A container frame's tab: one line of the default height, the room layout leaves above its
/// children, as wide as the name even where the children leave the frame narrower (clamped, the
/// centred name would spill over both edges of the frame).
Box frame_container_tab(const Box& b, std::string_view name, double font_size) {
  return {b.x, b.y, std::max(kFrameTabWidth, frame_tab_width(name, font_size)), std::min(b.height, kFrameTabHeight)};
}

/// A frame with its name in the tab, which widens and deepens to fit the name. Like a boundary, a
/// frame shows its name only. A container's tab may reach past the frame's right edge; that part
/// takes no clicks, as the native canvases pick nodes by their boxes.
std::string frame_node(const Element& e, const Box& b, const Paint& p, const Theme& t, bool container) {
  std::vector<Line> lines;
  Box tab;
  if (container) {
    std::string name = frame_container_name(e);
    tab = frame_container_tab(b, name, p.font_size);
    lines.push_back({std::move(name), p.font_size, p.bold});
  } else {
    const double width = frame_tab(b, frame_tab_width(e.name, p.font_size), 0).width;
    lines = lines_of(wrap_text(e.name, std::max(20.0, width - kFrameTabCut - 10), p.font_size), p.font_size, p.bold);
    tab = frame_tab(b, width, std::ceil(lines_height(lines) + 8));
  }
  // The label stays clear of the cut corner; in a tab wider than the frame it takes no clicks.
  const bool inside = tab.width <= b.width;
  const std::string label =
      text_block(lines, Box{tab.x, tab.y, std::max(0.0, tab.width - kFrameTabCut / 2), tab.height}, p, t);
  return frame_svg(b, tab, paint_attrs(p), inside) + (inside ? label : "<g pointer-events=\"none\">" + label + "</g>");
}

/// A lifeline with its name in the head, which shows the name only (as draw.io draws it).
std::string lifeline_node(const Element& e, const Box& b, const Paint& p, const Theme& t) {
  return lifeline_svg(b, paint_attrs(p), p.shape) + node_label(e, lifeline_head(b, p.shape), p, t, "rect", false);
}

/// A container drawn as its own shape (styled imports): the label sits at the top, or where its
/// style's verticalAlign puts it (no description: it would cover the children).
std::string styled_container(const Element& e, const Box& b, const Theme& t, const Style& rules) {
  Paint p = element_paint(e, t, rules);
  std::string shape = base_shape(e.shape, e.kind);
  if (shape == "invisible") return "";
  if (shape == "frame") return frame_node(e, b, p, t, true);
  if (shape == "lifeline") return lifeline_node(e, b, p, t);
  const std::string* horizontal = style_value(e.style, "horizontal");
  if (shape == "swimlane" && horizontal && *horizontal == "0") {
    // A lane or pool with its header down the left side and the name turned up.
    double head = std::min(30.0, b.width);
    double cx = b.x + head / 2;
    double cy = b.y + b.height / 2;
    TextOpts o;
    if (p.bold) o.weight = "600";
    return "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
           num(b.height) + "\" " + paint_attrs(p) + "/>" + "<path d=\"M" + num(b.x + head) + "," + num(b.y) + " V" +
           num(b.y + b.height) + "\" " + replace_fill(paint_attrs(p), "fill=\"none\"") + "/>" +
           "<g transform=\"rotate(-90 " + num(cx) + " " + num(cy) + ")\">" + text(e.name, {cx, cy}, p.text, p.font_size, t, o) +
           "</g>";
  }
  // A container whose style places its label (draw.io imports: in the middle by default) puts it
  // there, as a leaf would; otherwise the label sits in a header at the top.
  if (shape != "swimlane" && (style_value(e.style, "verticalAlign") || rules.find("verticalAlign")))
    return shape_svg(shape, b, paint_attrs(p), p.rounded, p.shape) + node_label(e, b, p, t, shape, false);
  double head = shape == "swimlane" ? std::min(26.0, b.height) : 24;
  Paint q = p;
  q.vertical_align = "middle";
  return shape_svg(shape, b, paint_attrs(p), p.rounded, p.shape) +
         text_block({{e.name, p.font_size, p.bold}}, Box{b.x, b.y, b.width, head}, q, t);
}

/// A dashed frame around a container's children or a group; `style` blocks can recolour it.
std::string boundary(const std::string& title, const std::optional<std::string>& subtitle, const Box& b,
                     const Theme& t, const std::string& dash, const Style& rules = no_rules()) {
  TextOpts o;
  o.anchor = "start";
  o.weight = "600";
  const std::string* dashed = rules.find("dashed");
  const std::string* width = rules.find("strokeWidth");
  return "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
         num(b.height) + "\" rx=\"8\" fill=\"" + color(rules.find("fill"), "none") + "\" stroke=\"" +
         color(rules.find("stroke"), t.boundary) + "\" stroke-width=\"" + (width ? num(js_to_number(*width)) : "1.25") +
         "\"" + (dashed && *dashed == "0" ? "" : " stroke-dasharray=\"" + dash + "\"") + "/>" +
         text(subtitle && !subtitle->empty() ? title + " " + *subtitle : title, {b.x + 10, b.y + 14},
              color(rules.find("fontColor"), t.boundary_text), t.small_font_size, t, o);
}

// ── Edges ──

std::string arrow_kind(const std::string& v) {
  std::string_view base = v;
  if (base.ends_with("Thin")) base.remove_suffix(4);
  if (base == "blockThin" || base == "classicThin") return "classic";
  // Ends with a shape of their own: draw.io's names, so imported files keep them.
  static constexpr std::array<std::string_view, 14> kShaped{
      "ERone",     "ERmandOne",  "ERmany",   "ERoneToMany", "ERzeroToOne", "ERzeroToMany", "async",
      "openAsync", "halfCircle", "baseDash", "circlePlus",  "cross",       "box",          "doubleBlock"};
  if (std::ranges::find(kShaped, v) != kShaped.end()) return v;
  if (v.starts_with("ER")) return "open";
  if (base == "circle") return "oval";
  static constexpr std::array<std::string_view, 7> kArrows{"classic", "block", "open", "diamond",
                                                           "oval",    "dash",  "none"};
  return std::ranges::find(kArrows, base) != kArrows.end() ? std::string(base) : "classic";
}

struct MarkerSpec {
  std::string kind;
  std::string color;
  bool filled = true;
};

std::string marker_id(const MarkerSpec& m, bool start) {
  std::string color;
  for (char c : m.color)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) color.push_back(c);
  return "dd-m-" + m.kind + "-" + (m.filled ? "f" : "o") + "-" + color + (start ? "-s" : "");
}

/// The ends that take two symbols (a minimum and a maximum) get a box twice as long.
bool wide_marker(std::string_view kind) {
  return kind == "ERoneToMany" || kind == "ERzeroToOne" || kind == "ERzeroToMany";
}

/// The shape of an end in its box: the line comes in from the left along y=5 and ends at x=9
/// (x=19 in a wide box).
std::string marker_shape(const MarkerSpec& m, const Theme& t) {
  std::string fill = m.filled ? m.color : t.surface;
  auto lines = [&](const std::string& d) {
    return "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + m.color + "\" stroke-width=\"1.4\"/>";
  };
  auto shape = [&](const std::string& d, const std::string& paint) {
    return "<path d=\"" + d + "\" fill=\"" + paint + "\" stroke=\"" + m.color + "\"/>";
  };
  auto ring = [&](double cx, double r) {
    return "<circle cx=\"" + num(cx) + "\" cy=\"5\" r=\"" + num(r) + "\" fill=\"" + t.surface + "\" stroke=\"" +
           m.color + "\" stroke-width=\"1.4\"/>";
  };
  const std::string& k = m.kind;
  if (k == "open") return "<path d=\"M1,1 L9,5 L1,9\" fill=\"none\" stroke=\"" + m.color + "\" stroke-width=\"1.4\"/>";
  if (k == "dash") return "<path d=\"M2,9 L6,1\" fill=\"none\" stroke=\"" + m.color + "\" stroke-width=\"1.4\"/>";
  if (k == "diamond") return "<path d=\"M0,5 L5,1 L10,5 L5,9 z\" fill=\"" + fill + "\" stroke=\"" + m.color + "\"/>";
  if (k == "oval") return "<circle cx=\"5\" cy=\"5\" r=\"4\" fill=\"" + fill + "\" stroke=\"" + m.color + "\"/>";
  if (k == "async") return shape("M0,0 L10,5 L0,5 z", fill);
  if (k == "openAsync") return lines("M1,1 L9,5");
  if (k == "halfCircle") return lines("M9,1 A4,4 0 0 0 9,9");
  if (k == "baseDash") return lines("M6,0 V10");
  if (k == "cross") return lines("M3,1 L9,9 M3,9 L9,1");
  if (k == "box") return shape("M1,1 H9 V9 H1 z", fill);
  if (k == "doubleBlock") return shape("M0,1 L5,5 L0,9 z M5,1 L10,5 L5,9 z", fill);
  if (k == "circlePlus") return ring(5, 4) + lines("M5,1 V9 M1,5 H9");
  if (k == "ERone") return lines("M6,0 V10");
  if (k == "ERmandOne") return lines("M4,0 V10 M7,0 V10");
  if (k == "ERmany") return lines("M9,0 L2,5 L9,10");
  if (k == "ERoneToMany") return lines("M19,0 L12,5 L19,10 M9,0 V10");
  if (k == "ERzeroToOne") return ring(6, 3.5) + lines("M14,0 V10");
  if (k == "ERzeroToMany") return ring(6, 3.5) + lines("M19,0 L12,5 L19,10");
  return "<path d=\"M0,0 L10,5 L0,10 z\" fill=\"" + (m.kind == "block" && !m.filled ? t.surface : m.color) +
         "\" stroke=\"" + m.color + "\"/>";
}

std::string marker_def(const MarkerSpec& m, bool start, const Theme& t) {
  bool wide = wide_marker(m.kind);
  return "<marker id=\"" + marker_id(m, start) + "\" viewBox=\"0 0 " + (wide ? "20" : "10") + " 10\" refX=\"" +
         (wide ? "19" : "9") + "\" refY=\"5\" markerWidth=\"" + (wide ? "14" : "7") +
         "\" markerHeight=\"7\" orient=\"" + (start ? "auto-start-reverse" : "auto") + "\">" + marker_shape(m, t) +
         "</marker>";
}

/// Markers in first-use order, as a JavaScript Map keeps them.
struct Markers {
  std::vector<std::pair<std::string, std::string>> items;
  void add(const std::string& id, const std::function<std::string()>& def) {
    for (const auto& [k, _] : items)
      if (k == id) return;
    items.emplace_back(id, def());
  }
};

Box midpoint_box(const std::vector<Point>& points, Size size) {
  auto i = static_cast<std::size_t>(std::floor((static_cast<double>(points.size()) - 1) / 2));
  const Point& a = points[i];
  const Point& b = points[i + 1];
  return {(a.x + b.x) / 2 - size.width / 2, (a.y + b.y) / 2 - size.height / 2, size.width, size.height};
}

/// Appends `op` and the point `p` to path data, as ` Lx,y` (`op` carries its leading space).
void add_point(std::string& d, std::string_view op, Point p) {
  d += op;
  d += num(p.x);
  d += ',';
  d += num(p.y);
}

/// Room for path data with `points` points: a command, two numbers and their separators come to
/// about ten characters for the coordinates diagrams have.
std::string path_buffer(std::size_t points) {
  std::string d;
  d.reserve(points * 12);
  return d;
}

std::string path_d(const std::vector<Point>& points) {
  std::string d = path_buffer(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) add_point(d, i == 0 ? "M" : " L", points[i]);
  return d;
}

/// A line through `points` that bends smoothly: each corner is the control point of a quadratic
/// curve between the midpoints of its two segments, as draw.io draws `curved=1`.
std::string curved_d(const std::vector<Point>& points) {
  std::string d = path_buffer(2 * points.size());
  add_point(d, "M", points[0]);
  for (std::size_t i = 1; i + 1 < points.size(); ++i) {
    const Point& c = points[i];
    Point to = i + 2 == points.size() ? points[i + 1]
                                      : Point{(c.x + points[i + 1].x) / 2, (c.y + points[i + 1].y) / 2};
    add_point(d, " Q", c);
    add_point(d, " ", to);
  }
  return d;
}

/// A polyline whose corners are rounded off with a radius of up to `radius` (draw.io's `rounded=1`).
std::string rounded_d(const std::vector<Point>& points, double radius) {
  std::string d = path_buffer(3 * points.size());
  add_point(d, "M", points[0]);
  for (std::size_t i = 1; i + 1 < points.size(); ++i) {
    const Point& a = points[i - 1];
    const Point& c = points[i];
    const Point& b = points[i + 1];
    double in = std::hypot(c.x - a.x, c.y - a.y);
    double out = std::hypot(b.x - c.x, b.y - c.y);
    double r = std::min({radius, in / 2, out / 2});
    if (r <= 0) {
      add_point(d, " L", c);
      continue;
    }
    add_point(d, " L", {c.x - (c.x - a.x) / in * r, c.y - (c.y - a.y) / in * r});
    add_point(d, " Q", c);
    add_point(d, " ", {c.x + (b.x - c.x) / out * r, c.y + (b.y - c.y) / out * r});
  }
  add_point(d, " L", points.back());
  return d;
}

std::string edge(const Relationship& r, const std::vector<Point>& points, const std::optional<Box>& label,
                 const Theme& t, Markers& markers, const Style& rules) {
  if (points.size() < 2) return "";
  Look st{r.style, rules};
  // Styled (imported) edges default to black, as in draw.io; model edges use the theme.
  std::string stroke_color = color(st.get("stroke"), r.style ? "#000000" : t.edge);
  std::string d = points.size() > 2 && st.is("curved", "1")    ? curved_d(points)
                  : points.size() > 2 && st.is("rounded", "1") ? rounded_d(points, 10)
                                                               : path_d(points);
  const std::string* dashed_style = st.get("dashed");
  bool dashed = dashed_style ? *dashed_style == "1" : r.line == LineStyle::Dashed;
  const std::string* pattern = st.get("dashPattern");
  std::string dash = dashed ? " stroke-dasharray=\"" + escape_xml(pattern && !pattern->empty() ? *pattern : "6 4") + "\"" : "";
  auto marker = [&](const std::string* kind_in, const std::string* fill, bool start) -> std::string {
    std::string kind;
    if (!kind_in) {
      if (start && !r.bidirectional) return "";
      kind = "classic";
    } else {
      kind = *kind_in;
    }
    std::string k = arrow_kind(kind);
    if (k == "none" || kind == "none") return "";
    MarkerSpec spec{k, stroke_color, !(fill && *fill == "0")};
    std::string id = marker_id(spec, start);
    markers.add(id, [&] { return marker_def(spec, start, t); });
    return std::string(" marker-") + (start ? "start" : "end") + "=\"url(#" + id + ")\"";
  };
  std::string start_marker = marker(st.get("startArrow"), st.get("startFill"), true);
  std::string end_marker = marker(st.get("endArrow"), st.get("endFill"), false);
  double width = st.number("strokeWidth", 1.25);
  std::string out;
  if (st.is("double", "1")) {
    // Two lines: a wide one with the background drawn down its middle. The ends go on a third,
    // unpainted path of the plain width, so they are the size they would be on a single line.
    out = "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + stroke_color + "\" stroke-width=\"" + num(width * 3) +
          "\"" + dash + "/>" + "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + t.background +
          "\" stroke-width=\"" + num(width) + "\"" + dash + "/>";
    if (!start_marker.empty() || !end_marker.empty())
      out += "<path d=\"" + d + "\" fill=\"none\" stroke=\"none\" stroke-width=\"" + num(width) + "\"" +
             start_marker + end_marker + "/>";
  } else {
    out = "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + stroke_color + "\" stroke-width=\"" + num(width) +
          "\"" + dash + start_marker + end_marker + "/>";
  }
  // UML multiplicities sit just beside the ends of the line.
  auto near = [&](const Point& a, const Point& b, const std::string* v) -> std::string {
    if (!v || v->empty()) return "";
    double len = std::hypot(b.x - a.x, b.y - a.y);
    if (len == 0 || std::isnan(len)) len = 1;
    double x = a.x + ((b.x - a.x) / len) * 14 + ((b.y - a.y) / len) * 8;
    double y = a.y + ((b.y - a.y) / len) * 14 - ((b.x - a.x) / len) * 8;
    return text(*v, {x, y}, color(st.get("fontColor"), t.edge_text), t.small_font_size, t);
  };
  out += near(points[0], points[1], prop(r, "uml.sourceMultiplicity"));
  out += near(points[points.size() - 1], points[points.size() - 2], prop(r, "uml.targetMultiplicity"));
  if (r.label && !r.label->empty()) {
    double size = st.number("fontSize", t.small_font_size);
    Box box = label ? *label : midpoint_box(points, {text_width(*r.label, size) + 8, 16});
    double cx = box.x + box.width / 2;
    std::string fg = color(st.get("fontColor"), t.edge_text);
    out += "<rect x=\"" + num(box.x) + "\" y=\"" + num(box.y) + "\" width=\"" + num(box.width) + "\" height=\"" +
           num(box.height) + "\" fill=\"" + t.background + "\" fill-opacity=\"0.85\"/>";
    if (r.technology) {
      out += text(*r.label, {cx, box.y + 8}, fg, size, t);
      out += text("[" + *r.technology + "]", {cx, box.y + 22}, fg, size - 1, t);
    } else {
      out += text(*r.label, {cx, box.y + box.height / 2}, fg, size, t);
    }
  }
  return out;
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

std::vector<std::string> sequence_svg(const SequenceGeometry& seq, const Theme& t, Markers& markers) {
  std::vector<std::string> out;
  const std::string& ink = t.edge;
  double small = t.small_font_size;
  TextOpts start;
  start.anchor = "start";
  TextOpts start_bold = start;
  start_bold.weight = "600";
  for (const auto& id : seq.lifelines.keys()) {
    const Lifeline& l = *seq.lifelines.find(id);
    out.push_back("<path class=\"dd-lifeline\" data-id=\"" + escape_xml(id) + "\" d=\"M" + num(l.x) + "," + num(l.top) +
                  " V" + num(l.bottom) + "\" stroke=\"" + t.boundary + "\" stroke-width=\"1\" stroke-dasharray=\"5 4\"/>");
  }
  for (const auto& d : seq.delays) {
    out.push_back("<rect x=\"" + num(seq.left) + "\" y=\"" + num(d.y) + "\" width=\"" + num(seq.right - seq.left) +
                  "\" height=\"" + num(d.height) + "\" fill=\"" + t.background + "\"/>");
    if (d.label && !d.label->empty()) {
      TextOpts o;
      o.italic = true;
      out.push_back(text(*d.label, {(seq.left + seq.right) / 2, d.y + d.height / 2}, t.boundary_text, small, t, o));
    }
  }
  for (const auto& f : seq.frames) {
    const Box& b = f.box;
    std::string tag(frame_name(f.frame));
    double tag_w = text_width(tag, small) + 18;
    std::string s = "<g class=\"dd-frame\"><rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" +
                    num(b.width) + "\" height=\"" + num(b.height) + "\" fill=\"none\" stroke=\"" + ink +
                    "\" stroke-width=\"1\"/>" + "<path d=\"M" + num(b.x) + "," + num(b.y + 20) + " H" +
                    num(b.x + tag_w - 6) + " L" + num(b.x + tag_w) + "," + num(b.y + 14) + " V" + num(b.y) +
                    "\" fill=\"" + t.background + "\" stroke=\"" + ink + "\" stroke-width=\"1\"/>" +
                    text(tag, {b.x + 6, b.y + 10}, ink, small, t, start_bold);
    if (f.label && !f.label->empty()) {
      s += "<rect x=\"" + num(b.x + tag_w + 4) + "\" y=\"" + num(b.y + 2) + "\" width=\"" +
           num(text_width("[" + *f.label + "]", small) + 8) + "\" height=\"16\" fill=\"" + t.background + "\"/>" +
           text("[" + *f.label + "]", {b.x + tag_w + 8, b.y + 10}, ink, small, t, start);
    }
    for (const auto& sep : f.separators) {
      s += "<path d=\"M" + num(b.x) + "," + num(sep.y) + " H" + num(b.x + b.width) + "\" stroke=\"" + ink +
           "\" stroke-dasharray=\"4 3\"/>";
      if (sep.label && !sep.label->empty()) s += text("[" + *sep.label + "]", {b.x + 8, sep.y + 10}, ink, small, t, start);
    }
    out.push_back(s + "</g>");
  }
  for (const auto& [element, box] : seq.activations) {
    out.push_back("<rect class=\"dd-activation\" x=\"" + num(box.x) + "\" y=\"" + num(box.y) + "\" width=\"" +
                  num(box.width) + "\" height=\"" + num(box.height) + "\" fill=\"" + t.surface + "\" stroke=\"" + ink +
                  "\" stroke-width=\"1\"/>");
  }
  for (const auto& m : seq.messages) {
    MarkerSpec spec{m.style == MessageStyle::Sync ? "classic" : "open", ink, true};
    std::string id = marker_id(spec, false);
    markers.add(id, [&] { return marker_def(spec, false, t); });
    std::string dash = m.style == MessageStyle::Reply ? " stroke-dasharray=\"6 4\"" : "";
    out.push_back("<path class=\"dd-message\" d=\"" + path_d(m.points) + "\" fill=\"none\" stroke=\"" + ink +
                  "\" stroke-width=\"1.2\"" + dash + " marker-end=\"url(#" + id + ")\"/>");
    if (m.text && !m.text->empty() && m.label) {
      auto lines = split(*m.text, '\n');
      for (std::size_t i = 0; i < lines.size(); ++i)
        out.push_back(text(lines[i], {m.label->x, m.label->y + 8 + static_cast<double>(i) * 16}, t.edge_text, small, t, start));
    }
  }
  const NodeStyle& note = t.nodes.at("note");
  for (const auto& [b, body] : seq.notes) {
    out.push_back(shape_svg("note", b, "fill=\"" + note.fill + "\" stroke=\"" + note.stroke + "\" stroke-width=\"1\""));
    auto lines = split(body, '\n');
    for (std::size_t i = 0; i < lines.size(); ++i)
      out.push_back(text(lines[i], {b.x + 8, b.y + 12 + static_cast<double>(i) * 16}, note.text, small, t, start));
  }
  for (const auto& [b, label] : seq.refs) {
    out.push_back("<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
                  num(b.height) + "\" fill=\"" + t.background + "\" stroke=\"" + ink + "\"/>" + "<path d=\"M" +
                  num(b.x) + "," + num(b.y + 18) + " H" + num(b.x + 30) + " L" + num(b.x + 36) + "," + num(b.y + 12) +
                  " V" + num(b.y) + "\" fill=\"none\" stroke=\"" + ink + "\"/>" +
                  text("ref", {b.x + 6, b.y + 9}, ink, small, t, start_bold) +
                  text(label, {b.x + b.width / 2, b.y + b.height / 2 + 4}, ink, t.font_size, t));
  }
  for (const auto& d : seq.dividers) {
    std::string label = d.label.value_or("");
    double w = text_width(label, small) + 20;
    double cx = (seq.left + seq.right) / 2;
    std::string s = "<path d=\"M" + num(seq.left) + "," + num(d.y - 2) + " H" + num(seq.right) + " M" + num(seq.left) +
                    "," + num(d.y + 2) + " H" + num(seq.right) + "\" stroke=\"" + ink + "\" stroke-width=\"1\"/>";
    if (!label.empty()) {
      TextOpts o;
      o.weight = "600";
      s += "<rect x=\"" + num(cx - w / 2) + "\" y=\"" + num(d.y - 10) + "\" width=\"" + num(w) +
           "\" height=\"20\" fill=\"" + t.divider_fill + "\" stroke=\"" + ink + "\"/>" + text(label, {cx, d.y}, ink, small, t, o);
    }
    out.push_back(s);
  }
  return out;
}

// ── Decorations ──

double style_number(const std::optional<Style>& style, std::string_view key, double fallback) {
  return Look{style, no_rules()}.number(key, fallback);
}

struct DecorationText {
  std::vector<std::string> lines;
  double size = 13;
};

/// A note's text wraps at 200 px; free text breaks only at newlines unless it has a width.
DecorationText decoration_text(const Decoration& d, const Theme& t, std::optional<double> width) {
  bool note = d.kind == DecorationKind::Note;
  DecorationText out;
  out.size = style_number(d.style, "fontSize", note ? t.small_font_size : t.font_size);
  std::string body = d.text.value_or("");
  if (note || width)
    out.lines = wrap_text(body, width.value_or(200), out.size);
  else
    out.lines = split(body, '\n');
  return out;
}

Paint decoration_paint(const Decoration& d, const Theme& t) {
  const auto& st = d.style;
  auto is = [&](std::string_view key, std::string_view value) {
    const std::string* v = style_value(st, key);
    return v && *v == value;
  };
  auto font_style = js_to_int32(style_number(st, "fontStyle", 0));
  const NodeStyle& note = t.nodes.count("note") ? t.nodes.at("note") : t.nodes.at("default");
  Paint p;
  switch (d.kind) {
    case DecorationKind::Note:
      p.fill = color(style_value(st, "fill"), note.fill);
      p.stroke = color(style_value(st, "stroke"), note.stroke);
      p.stroke_width = style_number(st, "strokeWidth", 1);
      p.text = color(style_value(st, "fontColor"), note.text);
      p.font_size = style_number(st, "fontSize", t.small_font_size);
      p.align = "left";
      p.vertical_align = "top";
      break;
    case DecorationKind::Text:
      p.fill = color(style_value(st, "fill"), "none");
      p.stroke = color(style_value(st, "stroke"), "none");
      p.stroke_width = style_number(st, "strokeWidth", 1);
      p.text = color(style_value(st, "fontColor"), t.edge_text);
      p.font_size = style_number(st, "fontSize", t.font_size);
      break;
    case DecorationKind::Box:
      p.fill = color(style_value(st, "fill"), "none");
      p.stroke = color(style_value(st, "stroke"), t.boundary);
      p.stroke_width = style_number(st, "strokeWidth", 1.25);
      p.text = color(style_value(st, "fontColor"), t.boundary_text);
      p.font_size = style_number(st, "fontSize", t.small_font_size);
      p.bold = true;
      break;
  }
  // Boxes are dashed and rounded unless the style says otherwise.
  p.dashed = d.kind == DecorationKind::Box ? !is("dashed", "0") : is("dashed", "1");
  if (const std::string* pattern = style_value(st, "dashPattern"); pattern && !pattern->empty())
    p.dash_pattern = *pattern;
  p.rounded = d.kind == DecorationKind::Box ? !is("rounded", "0") : is("rounded", "1");
  const std::string* opacity = style_value(st, "opacity");
  p.opacity = opacity ? js_to_number(*opacity) / 100 : 1;
  p.bold = p.bold || is("bold", "1") || (font_style & 1) == 1;
  p.italic = is("italic", "1") || (font_style & 2) == 2;
  p.underline = (font_style & 4) == 4;
  p.strike = (font_style & 8) == 8;
  if (is("align", "left")) p.align = "left";
  if (is("align", "right")) p.align = "right";
  if (is("align", "center")) p.align = "center";
  if (is("verticalAlign", "top")) p.vertical_align = "top";
  if (is("verticalAlign", "middle")) p.vertical_align = "middle";
  if (is("verticalAlign", "bottom")) p.vertical_align = "bottom";
  return p;
}

std::string decoration_attrs(const Decoration& d, std::size_t index) {
  std::string kind = d.kind == DecorationKind::Note ? "note" : d.kind == DecorationKind::Text ? "text" : "box";
  std::string out = "class=\"dd-decoration dd-" + kind + "\" data-decoration=\"" + std::to_string(index) + "\"";
  if (d.name) out += " data-name=\"" + escape_xml(*d.name) + "\"";
  return out;
}

std::string decoration_svg(const Decoration& d, const DecorationGeometry& g, const Theme& t) {
  const Box& b = g.box;
  Paint p = decoration_paint(d, t);
  std::string out;
  if (d.kind == DecorationKind::Box) {
    out = "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
          num(b.height) + "\"" + (p.rounded ? " rx=\"8\"" : "") + " " + paint_attrs(p) + "/>";
    if (d.text && !d.text->empty()) {
      TextOpts o;
      o.anchor = "start";
      o.weight = "600";
      o.italic = p.italic;
      o.underline = p.underline;
      o.strike = p.strike;
      out += text(*d.text, {b.x + 10, b.y + 14}, p.text, p.font_size, t, o);
    }
    return out;
  }
  if (d.kind == DecorationKind::Note)
    out = shape_svg("note", b, paint_attrs(p));
  else if (p.fill != "none" || p.stroke != "none")
    out = "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.width) + "\" height=\"" +
          num(b.height) + "\"" + (p.rounded ? " rx=\"6\"" : "") + " " + paint_attrs(p) + "/>";
  bool note = d.kind == DecorationKind::Note;
  std::optional<double> wrap = note ? std::optional<double>(b.width - 16) : d.width ? std::optional<double>(b.width - 8) : std::nullopt;
  DecorationText content = decoration_text(d, t, wrap);
  Box inner = note ? Box{b.x + 4, b.y + 4, b.width - 8, b.height - 8} : b;
  return out + text_block(lines_of(content.lines, p.font_size, p.bold), inner, p, t);
}

const std::set<std::string, std::less<>> kC4Kinds{"person", "system", "container", "component"};

}  // namespace

// ── Measure ────────────────────────────────────────────────────────────────────────────────────

Element with_kind(const Element& element, const KindResolver& kinds) {
  if (element.shape || !kinds || element.kind.find('.') == std::string::npos) return element;
  auto kind = kinds(element.kind);
  if (!kind || !kind->shape) return element;
  Element out = element;
  out.shape = kind->shape;
  return out;
}

Measure create_measure(const Theme& theme_in, ShapeResolver shapes, KindResolver kinds) {
  const Theme theme = theme_in;
  Measure m;
  m.node = [theme, shapes, kinds](const Element& element_in) -> Size {
    Element element = with_kind(element_in, kinds);
    auto art = art_of(element, shapes);
    const bool drawing = art && art->body && !art->body->empty();  // `art?.body` is truthy
    if (drawing && art->label_inside) {
      double text_w = text_width(element.name, theme.font_size);
      auto lines = wrap_text(element.name, std::max(80.0, art->width.value_or(100) - 16), theme.font_size);
      return {std::max(art->width.value_or(100), std::min(std::ceil(text_w + 24), 160.0)),
              std::max(art->height.value_or(80), std::ceil(static_cast<double>(lines.size()) * theme.font_size * 1.25 + 24))};
    }
    if (drawing) {
      // A drawing with its label underneath.
      Size icon = icon_size(*art);
      auto tech = tech_line(element);
      double text_w = std::max(text_width(element.name, theme.font_size), tech ? text_width(*tech, theme.small_font_size) : 0);
      // A styled drawing has its name over it and no description (see library_node).
      const bool described = !element.style;
      double width = std::max({80.0, std::ceil(icon.width + 16), std::ceil(std::min(text_w, 200.0) + 16),
                               described ? description_width(element, 16, theme) : 0.0});
      double lines = static_cast<double>(wrap_text(element.name, width - 16, theme.font_size).size());
      double height = std::ceil(icon.height + 12 + lines * theme.font_size * 1.25 + (tech ? theme.small_font_size * 1.3 : 0) +
                                (described ? description_height(element, width - 8, theme) : 0));
      return {width, height};
    }
    if (art && art->base && !art->base->empty()) element.shape = art->base;
    if (is_class_like(element)) {
      ClassContent c = class_content(element);
      std::vector<std::string> lines{c.title};
      if (c.stereotype) lines.push_back(*c.stereotype);
      lines.insert(lines.end(), c.attributes.begin(), c.attributes.end());
      lines.insert(lines.end(), c.methods.begin(), c.methods.end());
      double widest = -kInf;
      for (const auto& l : lines) widest = std::max(widest, text_width(l, theme.font_size));
      return {std::max(100.0, std::ceil(widest + 24)), class_heights(c).total};
    }
    const std::string shape = element.shape.value_or(element.kind);
    const std::string base = base_shape(element.shape, element.kind);
    if (base == "person" && !label_placement(element, "person").outside()) {
      // A stick figure with the label underneath, as wide as the wider of the two. At the theme's
      // font the wrap width in render (width - 10) exceeds every line, so the name breaks only at
      // newlines.
      auto tech = tech_line(element);
      double text_w = tech ? text_width(*tech, theme.small_font_size) : 0;
      double name_lines = 0;
      for (const auto& line : split(element.name, '\n')) {
        text_w = std::max(text_w, text_width(line, theme.font_size));
        ++name_lines;
      }
      double width = std::max({60.0, std::ceil(text_w + 16), description_width(element, 16, theme)});
      // As person_label_height(person_lines(…)) adds it up, without building the lines.
      double label = 8 + std::max(name_lines, 1.0) * theme.font_size * 1.25 +
                     (tech ? theme.small_font_size * 1.25 : 0) + description_height(element, std::max(20.0, width - 10), theme);
      return {width, std::ceil(kPersonFigureHeight + label)};
    }
    // Flow markers: start/end dots and fork/join bars are small.
    if (shape == "bar") return {120, 8};
    // A frame holds other things: room for them under its name tab.
    if (base == "frame")
      return {std::max(200.0, frame_tab_width(element.name, theme.font_size) + 20), 120};
    // A lifeline: its name in the head and room below for the line.
    if (base == "lifeline")
      return {std::max(100.0, std::ceil(text_width(element.name, theme.font_size) + 24)), 300};
    if (shape == "circle" && element.name.empty()) return {28, 28};
    auto tech = tech_line(element);
    double text_w = std::max(text_width(element.name, theme.font_size), tech ? text_width(*tech, theme.small_font_size) : 0);
    double width = std::max(120.0, std::ceil(text_w + 32));
    double height = tech ? 64 : 52;
    if (shape == "decision") {
      width = std::max(140.0, std::ceil(text_w * 1.6 + 24));
      height = 80;
    } else if (shape == "person") {
      height += 18;  // room for the figure above a label placed outside
    } else if (shape == "database" || shape == "cylinder") {
      height += 14;
    } else if (shape == "start" || shape == "end") {
      height = 44;
    }
    if (drawn_description(element).empty()) return {width, height};
    // Only the name is drawn outside a shape or in a circle.
    if (label_placement(element, base).outside() || !shows_description(base)) return {width, height};
    // Wrapped as node_label wraps it, in a box widened for it.
    const double factor = label_width_factor(base);
    if (factor == 1) {
      width = std::max(width, description_width(element, 32, theme));
      return {width, height + std::ceil(description_height(element, std::max(20.0, width - 10), theme))};
    }
    // A diamond or an ellipse gives its text 3/4 of its width and narrows towards its top and
    // bottom, so it is made tall enough for the whole text block to fit inside its outline.
    width = std::max(width, std::ceil(description_width(element, 10, theme) / factor));
    const double wrap = std::max(20.0, factor * width - 10);
    auto lines = lines_of(wrap_text(element.name, wrap, theme.font_size), theme.font_size, true);
    if (tech) lines.push_back({*tech, theme.small_font_size, false});
    append_description(lines, element, wrap, theme);
    double widest = 0;
    for (const auto& l : lines) widest = std::max(widest, text_width(l.text, l.size));
    const double text_h = lines_height(lines) + 8;          // text_block's insets
    // The text's width, with a margin. Capped, so a word too long to wrap still runs past the
    // outline, as it runs past a box: the fit is for text that wraps.
    const double room = std::min(widest + 10, 0.9 * width);
    const double fit = base == "rhombus" ? text_h * width / (width - room)  // |x|/W + |y|/H <= 1/2
                                         : text_h / std::sqrt(1 - (room / width) * (room / width));
    return {width, std::max(height, std::ceil(fit))};
  };
  m.decoration = [theme](const Decoration& d) -> Size {
    DecorationText content = decoration_text(d, theme, std::nullopt);
    double widest = 0;
    for (const auto& l : content.lines) widest = std::max(widest, text_width(l, content.size));
    double lines = static_cast<double>(std::max<std::size_t>(content.lines.size(), 1));
    if (d.kind == DecorationKind::Note)
      return {std::max(60.0, std::ceil(widest + 24)), std::ceil(lines * content.size * 1.25 + 16)};
    return {std::ceil(widest + 8), std::ceil(lines * content.size * 1.25 + 8)};
  };
  m.edge_label = [theme](const Relationship& r) -> std::optional<Size> {
    if (!r.label) return std::nullopt;
    double tech = r.technology ? text_width("[" + *r.technology + "]", theme.small_font_size) : 0;
    return Size{std::ceil(std::max(text_width(*r.label, theme.small_font_size), tech) + 8), r.technology ? 30.0 : 16.0};
  };
  return m;
}

// ── Scene ──────────────────────────────────────────────────────────────────────────────────────

SvgScene render_scene(const ResolvedView& view, const Geometry& geometry, const RenderOptions& options) {
  Theme theme = options.theme ? *options.theme : theme_for(view, options.themes);
  // Library kinds with colours of their own become node styles of the theme, unless the theme
  // colours the kind itself.
  std::map<std::string, KindArt, std::less<>> kinds;
  if (options.kinds)
    for (const auto& e : view.elements) {
      if (e.kind.find('.') == std::string::npos || kinds.count(e.kind)) continue;
      auto kind = options.kinds(e.kind);
      if (!kind) continue;
      if ((kind->fill || kind->stroke || kind->text) && !theme.nodes.count(e.kind)) {
        const NodeStyle& fallback = theme.nodes.at("default");
        NodeStyle style{kind->fill.value_or(fallback.fill), kind->stroke.value_or(fallback.stroke),
                        kind->text.value_or(fallback.text)};
        theme.nodes.emplace(e.kind, std::move(style));
      }
      kinds.emplace(e.kind, std::move(*kind));
    }
  const Theme t = attribute_safe(std::move(theme));
  auto stereotype = [&](const Element& e) -> std::optional<std::string> {
    if (kC4Kinds.count(e.kind)) return "[" + e.kind + "]";
    auto it = kinds.find(e.kind);
    if (it != kinds.end() && it->second.stereotype) return "[" + *it->second.stereotype + "]";
    return std::nullopt;
  };
  const double pad = options.padding;
  // Pinned nodes can sit at negative coordinates, so the frame is the union of everything drawn.
  std::vector<Box> boxes{{0, 0, geometry.width, geometry.height}};
  for (const auto& id : geometry.nodes.keys()) {
    const double* turn = geometry.rotations.find(id);
    boxes.push_back(turn ? rotated_bounds(*geometry.nodes.find(id), *turn) : *geometry.nodes.find(id));
  }
  std::set<std::string, std::less<>> parents;
  for (const auto& e : view.elements)
    if (e.parent) parents.insert(*e.parent);
  // A container frame's tab can reach past its box.
  for (const auto& e : view.elements) {
    auto kind = kinds.find(e.kind);
    const auto& shape = !e.shape && kind != kinds.end() ? kind->second.shape : e.shape;
    if (!parents.count(e.id) || base_shape(shape, e.kind) != "frame") continue;
    if (const Box* frame = geometry.nodes.find(e.id))
      boxes.push_back(frame_container_tab(*frame, frame_container_name(e),
                                          element_paint(e, t, rule_style(view.styles, e)).font_size));
  }
  for (const auto& r : geometry.edges) {
    for (const auto& p : r.points) boxes.push_back({p.x, p.y, 0, 0});
    if (r.label) boxes.push_back(*r.label);
  }
  const auto& seq = geometry.sequence;
  if (seq) {
    for (const auto& f : seq->frames) boxes.push_back(f.box);
    for (const auto& n : seq->notes) boxes.push_back(n.first);
    for (const auto& r : seq->refs) boxes.push_back(r.first);
    for (const auto& m : seq->messages) {
      for (const auto& p : m.points) boxes.push_back({p.x, p.y, 0, 0});
      if (m.label) boxes.push_back(*m.label);
    }
    boxes.push_back({seq->left, 0, seq->right - seq->left, geometry.height});
  }
  for (const auto& d : geometry.decorations) {
    boxes.push_back(d.box);
    for (const auto& p : d.connector) boxes.push_back({p.x, p.y, 0, 0});
  }
  double min_x = kInf, min_y = kInf, max_x = -kInf, max_y = -kInf;
  for (const auto& b : boxes) {
    min_x = std::min(min_x, b.x);
    min_y = std::min(min_y, b.y);
    max_x = std::max(max_x, b.x + b.width);
    max_y = std::max(max_y, b.y + b.height);
  }
  min_x -= pad;
  min_y -= pad;
  double width = max_x + pad - min_x;
  double height = max_y + pad - min_y;
  std::vector<std::string> back, front, edges;
  const auto& decorations = view.view.decorations;
  // Boxes and note connectors sit behind everything; notes and text in front.
  for (const auto& g : geometry.decorations) {
    if (g.index >= decorations.size()) continue;
    const Decoration& d = decorations[g.index];
    if (d.kind == DecorationKind::Box)
      back.push_back("<g " + decoration_attrs(d, g.index) + ">" + decoration_svg(d, g, t) + "</g>");
    else if (g.connector.size() >= 2)
      back.push_back("<path class=\"dd-note-connector\" d=\"" + path_d(g.connector) + "\" fill=\"none\" stroke=\"" +
                     t.boundary + "\" stroke-width=\"1\" stroke-dasharray=\"4 3\"/>");
  }
  for (const auto& group : view.groups) {
    if (const Box* box = geometry.nodes.find(group_node_id(group.id)))
      back.push_back("<g class=\"dd-group\" data-id=\"" + escape_xml(group.id) + "\">" +
                     boundary(group.name, std::nullopt, *box, t, "2 3") + "</g>");
  }
  for (const auto& declared : view.elements) {
    const Box* box = geometry.nodes.find(declared.id);
    if (!box) continue;
    Element element = declared;
    if (auto kind = kinds.find(element.kind); kind != kinds.end() && kind->second.shape && !element.shape)
      element.shape = kind->second.shape;
    const Style rules = rule_style(view.styles, element);
    std::string attrs = "class=\"dd-node\" data-id=\"" + escape_xml(element.id) + "\"";
    // A turned node turns with its label, about the middle of its box.
    if (const double* turn = geometry.rotations.find(element.id); turn && !parents.count(element.id))
      attrs += " transform=\"rotate(" + num(*turn) + " " + num(box->x + box->width / 2) + " " +
               num(box->y + box->height / 2) + ")\"";
    if (parents.count(element.id)) {
      // Styled containers (imported drawings), frames and lifelines keep their look; other model
      // containers get a boundary.
      if (element.style || [&] {
            const std::string base = base_shape(element.shape, element.kind);
            return base == "frame" || base == "lifeline";
          }())
        back.push_back("<g " + attrs + ">" + styled_container(element, *box, t, rules) + "</g>");
      else
        back.push_back("<g " + attrs + ">" +
                       boundary(element.name, stereotype(element), *box, t, "6 4", rules) +
                       "</g>");
      continue;
    }
    if (is_class_like(element)) {
      front.push_back("<g " + attrs + ">" + class_box(element, *box, t, rules) + "</g>");
      continue;
    }
    auto art = art_of(element, options.shapes);
    if (art && art->body && !art->body->empty()) {
      front.push_back("<g " + attrs + ">" + library_node(element, *box, *art, t, rules) + "</g>");
      continue;
    }
    Paint p = element_paint(element, t, rules);
    std::string shape = base_shape(art && art->base ? art->base : element.shape, element.kind);
    if (shape == "person" && !label_placement(element, shape).outside()) {
      front.push_back("<g " + attrs + ">" + person_node(element, *box, p, t, rules) + "</g>");
      continue;
    }
    if (shape == "frame") {
      // A frame encloses other nodes, so it goes behind them and the edges.
      back.push_back("<g " + attrs + ">" + frame_node(element, *box, p, t, false) + "</g>");
      continue;
    }
    if (shape == "lifeline") {
      // Activation bars and messages sit on the lifeline, so it goes behind them.
      back.push_back("<g " + attrs + ">" + lifeline_node(element, *box, p, t) + "</g>");
      continue;
    }
    front.push_back("<g " + attrs + ">" + shape_svg(shape, *box, paint_attrs(p), p.rounded, p.shape) +
                    node_label(element, *box, p, t, shape) + "</g>");
  }
  for (const auto& g : geometry.decorations) {
    if (g.index >= decorations.size()) continue;
    const Decoration& d = decorations[g.index];
    if (d.kind != DecorationKind::Box)
      front.push_back("<g " + decoration_attrs(d, g.index) + ">" + decoration_svg(d, g, t) + "</g>");
  }
  Markers markers;
  if (seq) {
    auto parts = sequence_svg(*seq, t, markers);
    back.insert(back.end(), parts.begin(), parts.end());
  }
  for (const auto& r : view.relationships) {
    if (const EdgeRoute* route = geometry.edges.find(r.id))
      edges.push_back("<g class=\"dd-edge\" data-id=\"" + escape_xml(r.id) + "\">" +
                      edge(r, route->points, route->label, t, markers, rule_style(view.styles, r)) + "</g>");
  }

  std::string defs = "<defs>";
  for (const auto& [_, def] : markers.items) defs += def;
  defs += "</defs>";
  std::vector<std::string> content{defs, "<rect class=\"dd-background\" x=\"" + num(min_x) + "\" y=\"" + num(min_y) +
                                             "\" width=\"" + num(width) + "\" height=\"" + num(height) + "\" fill=\"" +
                                             t.background + "\"/>"};
  content.insert(content.end(), back.begin(), back.end());
  content.insert(content.end(), edges.begin(), edges.end());
  content.insert(content.end(), front.begin(), front.end());
  SvgScene scene;
  scene.width = width;
  scene.height = height;
  scene.view_box = num(min_x) + " " + num(min_y) + " " + num(width) + " " + num(height);
  for (std::size_t i = 0; i < content.size(); ++i) {
    if (i) scene.content += "\n";
    scene.content += content[i];
  }
  return scene;
}

std::string render_svg(const ResolvedView& view, const Geometry& geometry, const RenderOptions& options) {
  SvgScene scene = render_scene(view, geometry, options);
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(scene.width) + "\" height=\"" + num(scene.height) +
         "\" viewBox=\"" + scene.view_box + "\">\n" + scene.content + "\n</svg>";
}

std::string line_sample_svg(const Style& style, double width, double height, const Theme& theme) {
  Relationship r;
  r.style = style;
  Markers markers;
  std::string line =
      edge(r, {{4, height / 2}, {width - 4, height / 2}}, std::nullopt, attribute_safe(theme), markers, no_rules());
  std::string defs;
  for (const auto& [_, def] : markers.items) defs += def;
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(width) + "\" height=\"" + num(height) +
         "\" viewBox=\"0 0 " + num(width) + " " + num(height) + "\">" + (defs.empty() ? "" : "<defs>" + defs + "</defs>") +
         line + "</svg>";
}

}  // namespace dd
