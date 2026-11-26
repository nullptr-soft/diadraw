// OrderedMap's renames: one key, and several at once where one takes the key another gives up.

#include <gtest/gtest.h>

#include "diadraw/base/ordered_map.hpp"

using dd::OrderedMap;

namespace {

OrderedMap<int> numbers() {
  OrderedMap<int> m;
  m.set("a", 1);
  m.set("b", 2);
  m.set("c", 3);
  return m;
}

}  // namespace

TEST(OrderedMap, RenameKeepsThePositionAndRefusesATakenKey) {
  auto m = numbers();
  EXPECT_TRUE(m.rename("b", "x"));
  EXPECT_EQ(m.keys(), (std::vector<std::string>{"a", "x", "c"}));
  EXPECT_FALSE(m.rename("a", "c"));
  EXPECT_FALSE(m.rename("nope", "y"));
}

TEST(OrderedMap, RenameAllFollowsAChain) {
  auto m = numbers();
  // `a` takes `b`'s key while `b` takes `c`'s and `c` a new one: one after another, each would
  // meet a key still taken.
  m.rename_all({{"a", "b"}, {"b", "c"}, {"c", "d"}, {"missing", "e"}});
  EXPECT_EQ(m.keys(), (std::vector<std::string>{"b", "c", "d"}));
  EXPECT_EQ(*m.find("b"), 1);
  EXPECT_EQ(*m.find("c"), 2);
  EXPECT_EQ(*m.find("d"), 3);
}

TEST(OrderedMap, RenameAllDropsAValueWhoseKeyStaysTaken) {
  auto m = numbers();
  m.rename_all({{"a", "c"}, {"b", "x"}});
  EXPECT_EQ(m.keys(), (std::vector<std::string>{"x", "c"}));
  EXPECT_EQ(*m.find("c"), 3);
  EXPECT_EQ(*m.find("x"), 2);
  // Two renames to one key: the first keeps it.
  auto two = numbers();
  two.rename_all({{"a", "x"}, {"b", "x"}});
  EXPECT_EQ(two.keys(), (std::vector<std::string>{"x", "c"}));
  EXPECT_EQ(*two.find("x"), 1);
}
