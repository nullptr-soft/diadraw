#include "diadraw/model/history.hpp"

namespace dd {

std::vector<Operation> History::operations() const {
  std::vector<Operation> out;
  out.reserve(done_.size());
  for (const auto& e : done_) out.push_back(e.operation);
  return out;
}

Result<const Model*> History::apply(const Operation& operation) {
  auto inverse = apply_in_place(model_, operation);
  if (!inverse) return inverse.error();
  done_.push_back({operation, std::move(*inverse)});
  undone_.clear();
  notify(operation);
  return &model_;
}

const Model& History::undo() {
  if (done_.empty()) return model_;
  Entry entry = std::move(done_.back());
  done_.pop_back();
  // An inverse always applies to the model its operation produced.
  auto redo_op = apply_in_place(model_, entry.inverse);
  (void)redo_op;
  undone_.push_back(std::move(entry));
  notify(undone_.back().inverse);
  return model_;
}

const Model& History::redo() {
  if (undone_.empty()) return model_;
  Entry entry = std::move(undone_.back());
  undone_.pop_back();
  auto inverse = apply_in_place(model_, entry.operation);
  (void)inverse;
  done_.push_back(std::move(entry));
  notify(done_.back().operation);
  return model_;
}

History::Subscription History::subscribe(Listener listener) {
  auto id = next_id_++;
  listeners_.emplace(id, std::move(listener));
  return id;
}

void History::unsubscribe(Subscription id) { listeners_.erase(id); }

void History::notify(const Operation& operation) {
  // Copy, so a listener may unsubscribe itself.
  auto listeners = listeners_;
  for (const auto& [_, listener] : listeners) listener(model_, operation);
}

}  // namespace dd
