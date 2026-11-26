#pragma once

#include <string>
#include <utility>
#include <variant>

namespace dd {

/// An error with a human-readable message. The core never throws across its public API.
struct Error {
  std::string message;
};

/// A value or an error, a small stand-in for C++23 `std::expected`.
template <class T>
class [[nodiscard]] Result {
 public:
  Result(T value) : data_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
  Result(Error error) : data_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool ok() const noexcept { return data_.index() == 0; }
  explicit operator bool() const noexcept { return ok(); }

  T& value() & { return std::get<0>(data_); }
  const T& value() const& { return std::get<0>(data_); }
  T&& value() && { return std::get<0>(std::move(data_)); }
  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  [[nodiscard]] const Error& error() const { return std::get<1>(data_); }

 private:
  std::variant<T, Error> data_;
};

/// Builds an `Error` result from string pieces: `fail("Element \"", id, "\" not found")`.
template <class... Parts>
Error fail(Parts&&... parts) {
  std::string message;
  (message.append(std::forward<Parts>(parts)), ...);
  return Error{std::move(message)};
}

}  // namespace dd
