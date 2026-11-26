// Canvas edits of `style` blocks and themes (op::AddStyleRule, UpdateStyleRule, RemoveStyleRule,
// SetTheme, AddTheme, UpdateTheme, RemoveTheme): the model operations and the text they write.
// Each becomes the smallest edit of the statement or entry it touches, so comments, alignment
// and the order entries were written in stay as they were.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;
using namespace dd::dsl;

namespace {

const std::string kSource = R"(// Shop, container level
theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"   // lines
  node person { fill "#08427b" stroke "#052e56" }
}

theme brand

style container {
  fill "#438dd5"   // blue
  stroke "#3c7fc0"
}

style tag "external" { fill "#999999" dashed true }

model {
  shop = system "Shop" {
    web = container "Web app"
  }
}

view checkout of shop {
  include *
}
)";

const std::string kPlain = R"(// Shop
model {
  web = container "Web app"
}

view main {
  include *
}
)";

SourceUpdate update(const Operation& op, const std::string& source = kSource) {
  auto r = update_source(source, op);
  EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
  if (!r.ok()) return {};
  for (const auto& d : r->result.diagnostics) EXPECT_NE(d.severity, Severity::Error) << d.message;
  EXPECT_EQ(print(r->result.model), print(r->model));
  EXPECT_TRUE(r->minimal) << r->source;
  return std::move(*r);
}

StyleRule rule(const std::string& source, std::size_t index) { return parse(source).model.styles[index]; }
ThemeDef theme(const std::string& source, std::size_t index) { return parse(source).model.themes[index]; }

}  // namespace

TEST(StyleBlocks, ChangingAValueRewritesOnlyThatEntry) {
  StyleRule r = rule(kSource, 0);
  r.style.set("fill", "#1168bd");
  auto u = update(op::UpdateStyleRule{0, r});
  ASSERT_EQ(u.edits.size(), 1u);
  EXPECT_EQ(u.edits[0].insert, "fill \"#1168bd\"");
  EXPECT_NE(u.source.find("  fill \"#1168bd\"   // blue\n"), std::string::npos);
}

TEST(StyleBlocks, NewKeysGoAtTheEndAndDroppedOnesLeaveWithTheirLine) {
  StyleRule r = rule(kSource, 0);
  r.style = Style{{"fill", "#438dd5"}, {"fontColor", "#ffffff"}};
  auto u = update(op::UpdateStyleRule{0, r});
  EXPECT_NE(u.source.find("style container {\n  fill \"#438dd5\"   // blue\n  fontColor \"#ffffff\"\n}"),
            std::string::npos)
      << u.source;
}

TEST(StyleBlocks, OneLineBlocksStayOnOneLine) {
  StyleRule r = rule(kSource, 1);
  r.style = Style{{"fill", "#999999"}, {"dashed", "1"}, {"opacity", "60"}};
  auto u = update(op::UpdateStyleRule{1, r});
  EXPECT_NE(u.source.find("style tag \"external\" { fill \"#999999\" dashed true opacity 60 }"), std::string::npos)
      << u.source;
  r.style = Style{{"dashed", "1"}};
  u = update(op::UpdateStyleRule{1, r});
  EXPECT_NE(u.source.find("style tag \"external\" { dashed true }"), std::string::npos) << u.source;
}

TEST(StyleBlocks, ChangingTheSelectorKeepsTheBody) {
  StyleRule r = rule(kSource, 1);
  r.kind = "container";
  auto u = update(op::UpdateStyleRule{1, r});
  ASSERT_EQ(u.edits.size(), 1u);
  EXPECT_NE(u.source.find("style container tag \"external\" { fill \"#999999\" dashed true }"), std::string::npos);
}

TEST(StyleBlocks, AddedBlocksJoinTheOthers) {
  StyleRule r;
  r.target = StyleTarget::Relationships;
  r.tag = "async";
  r.style = Style{{"dashed", "1"}};
  auto u = update(op::AddStyleRule{r, std::nullopt});
  EXPECT_NE(u.source.find("{ fill \"#999999\" dashed true }\n\nstyle relationship tag \"async\" {\n  dashed 1\n}\n\nmodel"),
            std::string::npos)
      << u.source;
  u = update(op::AddStyleRule{r, 0});
  EXPECT_NE(u.source.find("theme brand\n\nstyle relationship tag \"async\" {\n  dashed 1\n}\n\nstyle container {"),
            std::string::npos)
      << u.source;
}

TEST(StyleBlocks, TheFirstBlockGoesAboveTheModelAndItsComment) {
  StyleRule r;
  r.kind = "container";
  r.style = Style{{"fill", "#438dd5"}};
  auto u = update(op::AddStyleRule{r, std::nullopt}, kPlain);
  EXPECT_EQ(u.source.substr(0, u.source.find("model")), "style container {\n  fill \"#438dd5\"\n}\n\n// Shop\n");
}

TEST(StyleBlocks, RemovingABlockTakesItsLinesAndOneBlankLine) {
  auto u = update(op::RemoveStyleRule{0});
  EXPECT_NE(u.source.find("theme brand\n\nstyle tag \"external\""), std::string::npos) << u.source;
  u = update(op::RemoveStyleRule{1});
  EXPECT_NE(u.source.find("  stroke \"#3c7fc0\"\n}\n\nmodel {"), std::string::npos) << u.source;
}

TEST(StyleBlocks, InversesUndoTheEdits) {
  Model model = parse(kSource).model;
  StyleRule r = rule(kSource, 0);
  r.style.set("fill", "#000000");
  for (const Operation& op : std::vector<Operation>{op::UpdateStyleRule{0, r}, op::RemoveStyleRule{1},
                                                    op::AddStyleRule{r, 1}}) {
    auto applied = apply_operation(model, op);
    ASSERT_TRUE(applied.ok());
    auto undone = apply_operation(applied->model, applied->inverse);
    ASSERT_TRUE(undone.ok());
    EXPECT_EQ(undone->model, model);
  }
}

TEST(StyleBlocks, RefusesWhatCannotBeWritten) {
  Model model = parse(kSource).model;
  StyleRule r;
  r.kind = "my kind";
  EXPECT_FALSE(apply_operation(model, op::AddStyleRule{r, std::nullopt}).ok());
  r.kind = "relationship";
  EXPECT_FALSE(apply_operation(model, op::AddStyleRule{r, std::nullopt}).ok());
  r.kind.reset();
  r.style = Style{{"fill color", "red"}};
  EXPECT_FALSE(apply_operation(model, op::AddStyleRule{r, std::nullopt}).ok());
  r.style = Style{};
  r.target = StyleTarget::Relationships;
  r.kind = "container";
  EXPECT_FALSE(apply_operation(model, op::AddStyleRule{r, std::nullopt}).ok());
  EXPECT_FALSE(apply_operation(model, op::RemoveStyleRule{7}).ok());
}

TEST(Themes, PickingAThemeRewritesTheStatement) {
  auto u = update(op::SetTheme{std::nullopt, "mono"});
  ASSERT_EQ(u.edits.size(), 1u);
  EXPECT_EQ(u.edits[0].insert, "theme mono");
  u = update(op::SetTheme{std::nullopt, std::nullopt});
  EXPECT_NE(u.source.find("}\n\nstyle container {"), std::string::npos) << u.source;
  EXPECT_EQ(u.source.find("theme brand\n"), std::string::npos);
}

TEST(Themes, TheFirstThemeStatementGoesAboveTheModel) {
  auto u = update(op::SetTheme{std::nullopt, "dark"}, kPlain);
  EXPECT_EQ(u.source.substr(0, 20), "theme dark\n\n// Shop\n");
}

TEST(Themes, AViewPicksItsOwnTheme) {
  auto u = update(op::SetTheme{"checkout", "default"});
  EXPECT_NE(u.source.find("view checkout of shop {\n  include *\n  theme default\n}"), std::string::npos) << u.source;
  auto back = update(op::SetTheme{"checkout", std::nullopt}, u.source);
  EXPECT_EQ(back.source, kSource);
}

TEST(Themes, UpdatingADeclarationTouchesOnlyWhatChanged) {
  ThemeDef t = theme(kSource, 0);
  t.base = "mono";
  t.values.set("edge", "#cccccc");
  t.values.set("fontFamily", "IBM Plex Sans, sans-serif");
  t.nodes[0].second.set("text", "#ffffff");
  auto u = update(op::UpdateTheme{t});
  EXPECT_NE(u.source.find(R"(theme brand extends mono {
  background "#101418"
  edge "#cccccc"   // lines
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
  fontFamily "IBM Plex Sans, sans-serif"
})"),
            std::string::npos)
      << u.source;
}

TEST(Themes, NodeBlocksComeAndGo) {
  ThemeDef t = theme(kSource, 0);
  t.nodes.clear();
  t.nodes.emplace_back("container", Style{{"fill", "#1168bd"}});
  t.base.reset();
  auto u = update(op::UpdateTheme{t});
  EXPECT_NE(u.source.find(R"(theme brand {
  background "#101418"
  edge "#9aa5b1"   // lines
  node container {
    fill "#1168bd"
  }
})"),
            std::string::npos)
      << u.source;
}

TEST(Themes, DeclaringAndRemovingAThemeMovesWholeStatements) {
  ThemeDef t;
  t.name = "print";
  t.base = "mono";
  t.values.set("background", "#ffffff");
  auto u = update(op::Batch{{op::AddTheme{t, std::nullopt}, op::SetTheme{std::nullopt, "print"}}}, kPlain);
  EXPECT_EQ(u.source.substr(0, u.source.find("model")),
            "theme print extends mono {\n  background \"#ffffff\"\n}\n\ntheme print\n\n// Shop\n");
  auto removed = update(op::RemoveTheme{"brand"});
  EXPECT_EQ(removed.source.substr(0, 40), "// Shop, container level\n\ntheme brand\n\ns");
}

TEST(Themes, RefusesBadNamesAndDuplicates) {
  Model model = parse(kSource).model;
  ThemeDef t;
  t.name = "brand";
  EXPECT_FALSE(apply_operation(model, op::AddTheme{t, std::nullopt}).ok());
  t.name = "two words";
  EXPECT_FALSE(apply_operation(model, op::AddTheme{t, std::nullopt}).ok());
  t.name = "nope";
  EXPECT_FALSE(apply_operation(model, op::UpdateTheme{t}).ok());
  EXPECT_FALSE(apply_operation(model, op::SetTheme{"missing", "dark"}).ok());
  EXPECT_FALSE(apply_operation(model, op::SetTheme{std::nullopt, "not a name"}).ok());
}

TEST(Themes, OperationsRoundTripThroughJson) {
  ThemeDef t = theme(kSource, 0);
  StyleRule r = rule(kSource, 1);
  for (const Operation& op : std::vector<Operation>{
           op::AddStyleRule{r, 2}, op::RemoveStyleRule{1}, op::UpdateStyleRule{0, r},
           op::SetTheme{"checkout", "dark"}, op::SetTheme{std::nullopt, std::nullopt}, op::AddTheme{t, 0},
           op::RemoveTheme{"brand"}, op::UpdateTheme{t}}) {
    auto back = operation_from_json(Json::parse(to_json(op).dump()).value());
    ASSERT_TRUE(back.ok()) << to_json(op).dump();
    EXPECT_EQ(*back, op) << to_json(op).dump();
  }
}

// ── Random edits over the sample documents ───────────────────────────────────────────────────

namespace {

std::vector<std::string> corpus() {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(DIADRAW_CORPUS_DIR))
    if (entry.path().extension() == ".draw") files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> out;
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    out.push_back(text.str());
  }
  return out;
}

/// A random style or theme edit, as the inspector makes them.
std::optional<Operation> random_style_operation(const Model& model, std::mt19937& rng, int n) {
  auto pick = [&](std::size_t size) { return static_cast<std::size_t>(rng() % std::max<std::size_t>(size, 1)); };
  auto chance = [&](int percent) { return static_cast<int>(rng() % 100) < percent; };
  std::string tag = std::to_string(n);
  static const std::vector<std::string> kinds{"container", "person", "database", "process"};
  auto random_style = [&](Style style) {
    for (const auto& [k, v] : Style(style))
      if (chance(30)) style.set(k, "#" + tag + "a0a0a");
    if (chance(50)) style.set(chance(50) ? "fill" : "fontColor", "#0" + tag + "0000");
    if (chance(20)) style.set("dashed", "1");
    Style kept;
    for (const auto& [k, v] : style)
      if (!chance(20)) kept.set(k, v);
    return kept;
  };
  switch (rng() % 7) {
    case 0: {
      StyleRule r;
      if (chance(30)) r.target = StyleTarget::Relationships;
      else if (chance(70)) r.kind = kinds[pick(kinds.size())];
      if (chance(40) || (r.target == StyleTarget::Elements && !r.kind)) r.tag = "t" + tag;
      r.style = random_style({});
      return op::AddStyleRule{r, chance(50) ? std::optional(pick(model.styles.size() + 1)) : std::nullopt};
    }
    case 1:
      if (model.styles.empty()) return std::nullopt;
      return op::RemoveStyleRule{pick(model.styles.size())};
    case 2: {
      if (model.styles.empty()) return std::nullopt;
      std::size_t i = pick(model.styles.size());
      StyleRule r = model.styles[i];
      r.style = random_style(r.style);
      if (chance(15) && r.target == StyleTarget::Elements) r.kind = kinds[pick(kinds.size())];
      return op::UpdateStyleRule{i, r};
    }
    case 3: {
      static const std::vector<std::string> themes{"dark", "mono", "default"};
      std::optional<std::string> theme;
      if (!chance(25)) theme = themes[pick(themes.size())];
      std::optional<ViewId> view;
      if (chance(40) && !model.views.keys().empty()) view = model.views.keys()[pick(model.views.size())];
      return op::SetTheme{view, theme};
    }
    case 4: {
      ThemeDef t;
      t.name = "theme" + tag + "x" + std::to_string(model.themes.size());
      if (chance(60)) t.base = chance(50) ? "dark" : "mono";
      if (chance(70)) t.values.set("background", "#10" + tag + "418");
      if (chance(50)) t.nodes.emplace_back(kinds[pick(kinds.size())], Style{{"fill", "#1168bd"}});
      return op::AddTheme{t, chance(50) ? std::optional(pick(model.themes.size() + 1)) : std::nullopt};
    }
    case 5:
      if (model.themes.empty()) return std::nullopt;
      return op::RemoveTheme{model.themes[pick(model.themes.size())].name};
    default: {
      if (model.themes.empty()) return std::nullopt;
      ThemeDef t = model.themes[pick(model.themes.size())];
      if (chance(30)) t.base = chance(30) ? std::nullopt : std::optional<std::string>("mono");
      t.values = random_style(t.values);
      for (auto& [kind, look] : t.nodes) look = random_style(look);
      if (chance(30) && !t.nodes.empty()) t.nodes.erase(t.nodes.begin() + static_cast<std::ptrdiff_t>(pick(t.nodes.size())));
      if (chance(40)) {
        std::string kind = "k" + tag;
        if (std::none_of(t.nodes.begin(), t.nodes.end(), [&](const auto& p) { return p.first == kind; }))
          t.nodes.emplace_back(kind, Style{{"stroke", "#222222"}});
      }
      return op::UpdateTheme{t};
    }
  }
}

}  // namespace

TEST(StyleEditsProperty, RandomEditsParseBackToTheExpectedModel) {
  const auto files = corpus();
  ASSERT_FALSE(files.empty());
  std::mt19937 rng(7);
  int total = 0, minimal = 0;
  for (int run = 0; run < 200; ++run) {
    std::string source = files[rng() % files.size()];
    for (int step = 0; step < 6; ++step) {
      auto parsed = parse(source);
      auto op = random_style_operation(parsed.model, rng, step);
      if (!op) continue;
      auto result = update_source(source, *op, &parsed);
      if (!result) continue;
      std::size_t errors_before = 0, errors_after = 0;
      for (const auto& d : parsed.diagnostics) errors_before += d.severity == Severity::Error;
      for (const auto& d : result->result.diagnostics) errors_after += d.severity == Severity::Error;
      ASSERT_LE(errors_after, errors_before) << "run " << run << " step " << step << "\n" << result->source;
      ASSERT_EQ(print(result->result.model), print(result->model)) << "run " << run << " step " << step;
      ++total;
      if (result->minimal) ++minimal;
      else ADD_FAILURE() << "reprinted: run " << run << " step " << step << " " << to_json(*op).dump() << "\n" << source;
      source = result->source;
    }
  }
  EXPECT_GT(total, 600);
  EXPECT_GT(static_cast<double>(minimal) / total, 0.95);
}
