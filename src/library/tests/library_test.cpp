// Shape libraries: semver ranges, manifests, bundles and the library set.

#include <gtest/gtest.h>

#include "diadraw/library/library.hpp"

using namespace dd;
using namespace dd::library;

TEST(Semver, RangesLikeNpm) {
  EXPECT_TRUE(satisfies("1.4.2", "^1.2"));
  EXPECT_FALSE(satisfies("2.0.0", "^1.2"));
  EXPECT_TRUE(satisfies("0.2.5", "^0.2.1"));
  EXPECT_FALSE(satisfies("0.3.0", "^0.2.1"));
  EXPECT_TRUE(satisfies("1.2.9", "~1.2"));
  EXPECT_TRUE(satisfies("1.5.0", ">=1.2 <2 || 3.x"));
  EXPECT_TRUE(satisfies("3.1.0", ">=1.2 <2 || 3.x"));
  EXPECT_TRUE(satisfies("2.3.4", "1.2 - 2.3"));
  EXPECT_FALSE(satisfies("2.4.0", "1.2 - 2.3"));
  EXPECT_FALSE(satisfies("1.3.0-beta.1", "^1.2"));
  EXPECT_TRUE(satisfies("1.3.0-beta.2", "^1.3.0-beta.1"));
  EXPECT_TRUE(satisfies("7.0.0", "*"));
  EXPECT_FALSE(is_valid_range("^banana"));
  EXPECT_EQ(max_satisfying({"1.0.0", "1.10.0", "1.9.3", "2.0.0"}, "^1"), std::optional<std::string>("1.10.0"));
  EXPECT_LT(compare_versions(*parse_version("1.0.0-alpha.2"), *parse_version("1.0.0-alpha.10")), 0);
  EXPECT_LT(compare_versions(*parse_version("1.0.0-rc.1"), *parse_version("1.0.0")), 0);
}

TEST(Manifest, ValidationAndLicences) {
  auto m = Json::parse(R"({"name":"Bad Name","version":"1","license":"",
    "shapes":{"ok":{"base":"rect"},"9bad":{"svg":"x.svg"},"empty":{}},
    "composites":{"ok":{"width":10,"height":10,"parts":[{"shape":"missing","x":0,"y":0,"width":1,"height":1}]}},
    "dependencies":{"aws-icons":"^banana"}})");
  auto issues = validate_manifest(*m);
  std::vector<std::string> messages;
  for (const auto& i : issues) messages.push_back((i.error ? "E " : "W ") + i.message);
  EXPECT_EQ(messages.size(), 8u);
  EXPECT_EQ(check_license(*Json::parse(R"({"license":"MIT OR GPL-3.0"})")).status, LicenseStatus::Allowed);
  EXPECT_EQ(check_license(*Json::parse(R"({"license":"GPL-3.0"})")).status, LicenseStatus::Disallowed);
  EXPECT_EQ(check_license(*Json::parse(R"({"license":"Apache-2.0","licenseNotes":"No Atlassian"})")).status,
            LicenseStatus::Restricted);
  EXPECT_EQ(check_license(*Json::parse(R"({"license":"UNLICENSED","source":{"kind":"original"}})")).status,
            LicenseStatus::Allowed);
}

TEST(Bundle, SanitizesAndBuilds) {
  auto art = sanitize_svg(
      R"svg(<?xml version="1.0"?><svg width="24px" height="24" onload="x()"><defs><linearGradient id="g"/></defs>)svg"
      R"svg(<script>alert(1)</script><rect fill="url(#g)" href="http://evil" onclick="y()"/><use href="#g"/></svg>)svg",
      "lib-a-");
  ASSERT_TRUE(art);
  EXPECT_EQ(art->view_box, "0 0 24 24");
  EXPECT_EQ(art->body,
            R"svg(<defs><linearGradient id="lib-a-g"/></defs><rect fill="url(#lib-a-g)"/><use href="#lib-a-g"/>)svg");
  auto manifest = Json::parse(R"({"name":"demo","version":"1.0.0","license":"MIT",
    "shapes":{"box":{"svg":"box.svg","label":"Box"}},
    "fragments":{"pair":{"params":[{"name":"a","default":"x"}],"text":"${a} = component shape ${alias}.box\n"}}})");
  auto built = build_bundle(*manifest, [](const std::string& path) -> std::optional<std::string> {
    if (path == "box.svg") return std::string(R"(<svg viewBox="0 0 10 10"><rect/></svg>)");
    return std::nullopt;
  });
  ASSERT_TRUE(built.bundle) << (built.issues.empty() ? "" : built.issues[0].message);
  std::string text = serialize_bundle(*built.bundle);
  EXPECT_EQ(text.substr(0, 51), "{\n \"format\": \"diadraw-library\",\n \"formatVersion\": 1");
  ASSERT_TRUE(parse_bundle(text));
}

TEST(LibrarySet, ResolvesBuiltinsAndDrawsArt) {
  auto r = resolve_uses({{"bpmn", "^1", "bpmn"}, {"nope", "^1", "x"}}, {builtin_source()});
  ASSERT_EQ(r.unresolved.size(), 1u);
  EXPECT_EQ(r.unresolved[0].message, "Library nope is not installed; run `diadraw lib add nope@^1`");
  auto art = r.set.art("bpmn.user_task");
  ASSERT_TRUE(art);
  EXPECT_TRUE(art->shape.body.has_value());
  EXPECT_FALSE(r.set.art("bpmn.not_a_shape"));
  auto palette = r.set.palette();
  EXPECT_GT(palette.size(), 100u);
  EXPECT_EQ(palette.front().ref.substr(0, 5), "bpmn.");
}
