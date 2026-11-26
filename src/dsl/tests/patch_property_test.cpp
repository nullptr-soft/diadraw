// Random edits through update_source over the sample documents (the property test of the
// TypeScript implementation, ported): every operation sequence yields text that parses without
// errors back to the model the operation produces, local patches keep the comments they did not
// touch, and nearly every edit is a local patch rather than a reprint.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/editor/editor.hpp"
#include "diadraw/model/json.hpp"

using namespace dd;

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

/// Uniform numbers in [0, 1) from a fixed seed, the same on every platform.
class Picker {
 public:
  explicit Picker(std::uint32_t seed) : rng_(seed) {}
  double operator()() { return static_cast<double>(rng_()) / 4294967296.0; }
  template <typename T>
  const T* choose(const std::vector<T>& xs) {
    return xs.empty() ? nullptr : &xs[static_cast<std::size_t>((*this)() * static_cast<double>(xs.size()))];
  }

 private:
  std::mt19937 rng_;
};

template <typename V>
std::vector<V> values(const OrderedMap<V>& map) {
  std::vector<V> out;
  for (const auto& key : map.keys()) out.push_back(*map.find(key));
  return out;
}

/// A random valid operation for a model, or nothing when the model has nothing to pick.
std::optional<Operation> random_operation(const Model& model, Picker& pick, int n) {
  auto elements = values(model.elements);
  auto relationships = values(model.relationships);
  auto views = values(model.views);
  std::string tag = std::to_string(n);
  switch (static_cast<int>(pick() * 11)) {
    case 0: {
      const Element* parent = pick() < 0.5 ? pick.choose(elements) : nullptr;
      static const std::vector<std::string> kinds{"process", "container", "decision", "database"};
      std::string local = "fresh" + tag;
      return op::AddElement{make_element(parent ? parent->id + "." + local : local, *pick.choose(kinds),
                                         "Fresh " + tag, parent ? std::optional(parent->id) : std::nullopt)};
    }
    case 1: {
      const Element* e = pick.choose(elements);
      if (!e) return std::nullopt;
      return op::RemoveElement{e->id};
    }
    case 2: {
      const Element* e = pick.choose(elements);
      if (!e) return std::nullopt;
      return op::RenameElement{e->id, "Renamed \"" + tag + "\""};
    }
    case 3: {
      const Element* a = pick.choose(elements);
      const Element* b = pick.choose(elements);
      if (!a || !b) return std::nullopt;
      Endpoint from{a->id, std::nullopt}, to{b->id, std::nullopt};
      int index = 1;
      while (model.relationships.contains(dsl::relationship_id(from, to, index))) ++index;
      return op::AddRelationship{make_relationship(dsl::relationship_id(from, to, index), from, to,
                                                   pick() < 0.5 ? std::optional("link " + tag) : std::nullopt)};
    }
    case 4: {
      const Relationship* r = pick.choose(relationships);
      if (!r) return std::nullopt;
      return op::RemoveRelationship{r->id};
    }
    case 5: {
      const Relationship* r = pick.choose(relationships);
      if (!r) return std::nullopt;
      return op::SetRelationshipLabel{r->id, pick() < 0.3 ? std::nullopt : std::optional("label " + tag)};
    }
    case 6: {
      const View* v = pick.choose(views);
      const Element* a = pick.choose(elements);
      const Element* b = pick.choose(elements);
      if (!v || !a || !b) return std::nullopt;
      static const std::vector<Placement> placements{Placement::Below, Placement::Above, Placement::LeftOf,
                                                     Placement::RightOf};
      op::AddHint hint{v->id, PinHint{a->id, *pick.choose(placements), b->id}, std::nullopt};
      if (pick() < 0.5) hint.index = static_cast<std::size_t>(pick() * static_cast<double>(v->hints.size() + 1));
      return hint;
    }
    case 7: {
      std::vector<View> hinted;
      for (const auto& v : views)
        if (!v.hints.empty()) hinted.push_back(v);
      const View* v = pick.choose(hinted);
      if (!v) return std::nullopt;
      return op::RemoveHint{v->id, static_cast<std::size_t>(pick() * static_cast<double>(v->hints.size()))};
    }
    case 8: {
      // An inspector edit: a few of the element's own properties change at once.
      const Element* e = pick.choose(elements);
      if (!e) return std::nullopt;
      Element u = *e;
      static const std::vector<std::string> kinds{"process", "container", "database", "aws.lambda"};
      if (pick() < 0.3) u.kind = *pick.choose(kinds);
      if (pick() < 0.3) u.name = "Named " + tag;
      if (pick() < 0.4) u.technology = pick() < 0.3 ? std::nullopt : std::optional("Tech \"" + tag + "\"");
      if (pick() < 0.3) u.shape = pick() < 0.3 ? std::nullopt : std::optional<std::string>("cylinder");
      if (pick() < 0.3) u.description = pick() < 0.3 ? std::nullopt : std::optional("About " + tag);
      if (pick() < 0.4) {
        if (!u.tags.empty() && pick() < 0.5) u.tags.erase(u.tags.begin());
        if (pick() < 0.6) u.tags.push_back("t" + tag);
      }
      if (pick() < 0.3) u.style = pick() < 0.3 ? std::nullopt : std::optional(Style{{"fill", "#" + tag + "0f0f0"}});
      if (pick() < 0.4) {
        Properties props;
        for (const auto& [k, v] : u.properties)
          if (pick() < 0.6) props.set(k, pick() < 0.5 ? v : v + tag);
        if (pick() < 0.6) props.set("key" + tag, "value " + tag);
        u.properties = std::move(props);
      }
      return op::UpdateElement{std::move(u)};
    }
    case 9: {
      // An inspector edit of a relationship: its own properties, and sometimes its name.
      const Relationship* r = pick.choose(relationships);
      if (!r) return std::nullopt;
      Relationship u = *r;
      if (pick() < 0.3) u.label = pick() < 0.3 ? std::nullopt : std::optional("Says \"" + tag + "\"");
      if (pick() < 0.3) u.technology = pick() < 0.3 ? std::nullopt : std::optional("Tech " + tag);
      if (pick() < 0.3) {
        double arrow = pick();
        u.line = arrow < 0.4 ? LineStyle::Dashed : LineStyle::Solid;
        u.bidirectional = arrow > 0.7;
      }
      if (pick() < 0.3) {
        if (!u.tags.empty() && pick() < 0.5) u.tags.erase(u.tags.begin());
        if (pick() < 0.6) u.tags.push_back("t" + tag);
      }
      if (pick() < 0.2) u.style = pick() < 0.3 ? std::nullopt : std::optional(Style{{"stroke", "#" + tag + "0f0f0"}});
      if (pick() < 0.3) u.properties.set("key" + tag, "value " + tag);
      std::optional<std::string> name = dsl::is_identifier(r->id) ? std::optional(r->id) : std::nullopt;
      if (pick() < 0.4) {
        std::string fresh = "line" + tag + "_" + std::to_string(model.relationships.size());
        name = pick() < 0.3 ? std::nullopt : std::optional(fresh);
      }
      auto op = editor::update_relationship_operation(model, u, name);
      if (!op || !*op) return std::nullopt;
      return std::move(**op);
    }
    default: {
      const View* v = pick.choose(views);
      if (!v) return std::nullopt;
      static const std::vector<Direction> directions{Direction::Right, Direction::Down, Direction::Left,
                                                     Direction::Up};
      LayoutOptions layout;
      layout.direction = *pick.choose(directions);
      return op::SetLayout{v->id, layout};
    }
  }
}

std::size_t count_comments(const std::string& text) {
  static const std::regex kComment(R"(//[^\n]*|/\*[\s\S]*?\*/)");
  return static_cast<std::size_t>(std::distance(std::sregex_iterator(text.begin(), text.end(), kComment),
                                                std::sregex_iterator()));
}

}  // namespace

TEST(UpdateSourceProperty, RandomEditsParseBackToTheExpectedModel) {
  const auto files = corpus();
  ASSERT_FALSE(files.empty());
  Picker pick(42);
  int total = 0, minimal = 0, relationship_edits = 0;
  for (int run = 0; run < 300; ++run) {
    std::string source = *pick.choose(files);
    for (int step = 0; step < 5; ++step) {
      auto parsed = dsl::parse(source);
      auto op = random_operation(parsed.model, pick, step);
      if (!op) continue;
      auto result = dsl::update_source(source, *op, &parsed);
      if (!result) continue;  // a refused operation, e.g. a relationship to itself
      for (const auto& d : result->result.diagnostics)
        ASSERT_NE(d.severity, dsl::Severity::Error) << "run " << run << " step " << step << ": " << d.message;
      ASSERT_EQ(dsl::print(result->result.model), dsl::print(result->model)) << "run " << run << " step " << step;
      ++total;
      // Inspector edits of relationships (alone, or a rename then an update) always patch in place.
      const auto& variant = static_cast<const Operation::variant&>(*op);
      const auto* batch = std::get_if<op::Batch>(&variant);
      const auto& first = static_cast<const Operation::variant&>(batch ? batch->operations.front() : *op);
      if (std::holds_alternative<op::UpdateRelationship>(first) ||
          std::holds_alternative<op::RenameRelationships>(first)) {
        ++relationship_edits;
        EXPECT_TRUE(result->minimal) << "run " << run << " step " << step << ": " << to_json(*op).dump();
      }
      if (result->minimal) {
        ++minimal;
        // A local patch never loses a comment outside the statements it touched.
        std::size_t removed = 0;
        for (const auto& e : result->edits) removed += count_comments(source.substr(e.from, e.to - e.from));
        EXPECT_GE(count_comments(result->source) + removed, count_comments(source)) << "run " << run;
      }
      source = result->source;
    }
  }
  EXPECT_GT(total, 1000);
  EXPECT_GT(relationship_edits, 50);
  EXPECT_GT(static_cast<double>(minimal) / total, 0.9);
}
