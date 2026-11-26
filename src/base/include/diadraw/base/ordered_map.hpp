#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dd {

/// Hash and equality that let `unordered_map<std::string, …>` be searched with a `string_view`.
struct StringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

/// A string-keyed map that remembers insertion order, like a JavaScript object.
///
/// Values are held through `shared_ptr<const V>`, so copying a map copies pointers, not values.
/// That makes model snapshots cheap: an operation copies the map it changes and shares every
/// value it does not touch. Replacing a key keeps its position; erasing and re-adding moves it to
/// the end, exactly as `delete obj[k]; obj[k] = v` does in JavaScript.
template <class V>
class OrderedMap {
 public:
  using Ptr = std::shared_ptr<const V>;

  class const_iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = V;
    using difference_type = std::ptrdiff_t;
    using pointer = const V*;
    using reference = const V&;

    const_iterator() = default;
    explicit const_iterator(typename std::vector<std::pair<std::string, Ptr>>::const_iterator it)
        : it_(it) {}
    reference operator*() const { return *it_->second; }
    pointer operator->() const { return it_->second.get(); }
    const_iterator& operator++() {
      ++it_;
      return *this;
    }
    const_iterator operator++(int) {
      auto copy = *this;
      ++it_;
      return copy;
    }
    bool operator==(const const_iterator&) const = default;

   private:
    typename std::vector<std::pair<std::string, Ptr>>::const_iterator it_;
  };

  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
  [[nodiscard]] const_iterator begin() const { return const_iterator(items_.begin()); }
  [[nodiscard]] const_iterator end() const { return const_iterator(items_.end()); }

  [[nodiscard]] const V* find(std::string_view key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : items_[it->second].second.get();
  }
  [[nodiscard]] bool contains(std::string_view key) const { return index_.find(key) != index_.end(); }

  /// The shared pointer for a key, for callers that want to keep a value alive cheaply.
  [[nodiscard]] Ptr get(std::string_view key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : items_[it->second].second;
  }

  /// Inserts or replaces; a replaced key keeps its position.
  void set(std::string key, V value) { set(std::move(key), std::make_shared<const V>(std::move(value))); }
  void set(std::string key, Ptr value) {
    auto it = index_.find(key);
    if (it != index_.end()) {
      items_[it->second].second = std::move(value);
      return;
    }
    index_.emplace(key, items_.size());
    items_.emplace_back(std::move(key), std::move(value));
  }

  bool erase(std::string_view key) {
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    std::size_t pos = it->second;
    index_.erase(it);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(pos));
    for (std::size_t i = pos; i < items_.size(); ++i) index_[items_[i].first] = i;
    return true;
  }

  /// Gives the value at `from` the key `to`, keeping its position. False when `from` is missing or
  /// `to` is taken by another value.
  bool rename(std::string_view from, std::string to) {
    auto it = index_.find(from);
    if (it == index_.end()) return false;
    if (from == to) return true;
    if (index_.find(to) != index_.end()) return false;
    std::size_t pos = it->second;
    index_.erase(it);
    items_[pos].first = to;
    index_.emplace(std::move(to), pos);
    return true;
  }

  /// Renames several keys at once, each value keeping its position, so a chain (`a` to `b` while
  /// `b` becomes `c`) works. A missing `from` is skipped; a value whose `to` is kept by a value
  /// that is not renamed, or was given to an earlier rename, is dropped.
  void rename_all(const std::vector<std::pair<std::string, std::string>>& renames) {
    std::vector<std::pair<std::size_t, const std::string*>> moving;
    for (const auto& [from, to] : renames) {
      auto it = index_.find(from);
      if (it == index_.end()) continue;
      moving.emplace_back(it->second, &to);
      index_.erase(it);
    }
    std::vector<bool> dropped;
    for (const auto& [pos, to] : moving) {
      if (index_.find(*to) != index_.end()) {
        if (dropped.empty()) dropped.resize(items_.size());
        dropped[pos] = true;
        continue;
      }
      items_[pos].first = *to;
      index_.emplace(*to, pos);
    }
    if (dropped.empty()) return;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
      if (dropped[i]) continue;
      if (kept != i) items_[kept] = std::move(items_[i]);  // never onto itself: that empties the key
      ++kept;
    }
    items_.resize(kept);
    index_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i) index_.emplace(items_[i].first, i);
  }

  /// Keys in insertion order.
  [[nodiscard]] std::vector<std::string> keys() const {
    std::vector<std::string> out;
    out.reserve(items_.size());
    for (const auto& [k, _] : items_) out.push_back(k);
    return out;
  }

  /// Same keys with equal values, in any order (JavaScript `toEqual` semantics).
  friend bool operator==(const OrderedMap& a, const OrderedMap& b) {
    if (a.items_.size() != b.items_.size()) return false;
    for (const auto& [k, v] : a.items_) {
      const V* other = b.find(k);
      if (!other || (v.get() != other && !(*v == *other))) return false;
    }
    return true;
  }

 private:
  std::vector<std::pair<std::string, Ptr>> items_;
  std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> index_;
};

}  // namespace dd
