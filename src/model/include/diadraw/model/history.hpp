#pragma once

// A linear operation log with undo and redo, a port of packages/core/src/history.ts.
//
// Both editors (text and canvas) push their changes through one log, so undo works the same
// whichever side made the change. Applying a new operation after an undo discards the redo stack.

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "diadraw/model/operations.hpp"

namespace dd {

class History {
 public:
  using Listener = std::function<void(const Model&, const Operation&)>;
  using Subscription = std::uint64_t;

  History() = default;
  explicit History(Model initial) : model_(std::move(initial)) {}

  [[nodiscard]] const Model& model() const noexcept { return model_; }
  [[nodiscard]] bool can_undo() const noexcept { return !done_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !undone_.empty(); }

  /// Operations applied so far, oldest first (excluding undone ones).
  [[nodiscard]] std::vector<Operation> operations() const;

  /// Applies an operation; on error nothing changes, the redo stack included.
  Result<const Model*> apply(const Operation& operation);
  /// Undoes the last operation; a no-op with an empty stack.
  const Model& undo();
  /// Redoes the last undone operation; a no-op with an empty stack.
  const Model& redo();

  Subscription subscribe(Listener listener);
  void unsubscribe(Subscription id);

 private:
  struct Entry {
    Operation operation;
    Operation inverse;
  };

  void notify(const Operation& operation);

  Model model_;
  std::vector<Entry> done_;
  std::vector<Entry> undone_;
  std::map<Subscription, Listener> listeners_;
  Subscription next_id_ = 1;
};

}  // namespace dd
