#pragma once

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "error_code.h"

namespace vynx_http {

// Result type for error handling
// Contains either a value of type T or an error_code
template <typename T>
class result {
   public:
    // Construct with value
    explicit result(T value) : data_(std::move(value)) {}

    // Construct with error
    explicit result(error_code ec) : data_(ec) {}

    // Check if result contains value
    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }

    // Check if result contains error
    [[nodiscard]] bool has_error() const noexcept {
        return std::holds_alternative<error_code>(data_);
    }

    // Get value (throws if error)
    [[nodiscard]] T& value() & {
        if (has_error()) {
            throw std::runtime_error("result contains error");
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] const T& value() const& {
        if (has_error()) {
            throw std::runtime_error("result contains error");
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& value() && {
        if (has_error()) {
            throw std::runtime_error("result contains error");
        }
        return std::get<T>(std::move(data_));
    }

    // Get error code (throws if value)
    [[nodiscard]] error_code error() const {
        if (ok()) {
            throw std::runtime_error("result contains value, not error");
        }
        return std::get<error_code>(data_);
    }

    // Get value or default
    T value_or(T&& default_value) const& {
        if (ok()) {
            return value();
        }
        return std::forward<T>(default_value);
    }

    T value_or(T&& default_value) && {
        if (ok()) {
            return std::move(value());
        }
        return std::forward<T>(default_value);
    }

    // Monadic bind
    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T&>())) {
        if (ok()) {
            return f(value());
        }
        using ReturnType = decltype(f(std::declval<T&>()));
        return ReturnType(error());
    }

    // Transform value
    template <typename F>
    auto map(F&& f) -> result<decltype(f(std::declval<T&>()))> {
        if (ok()) {
            return result<decltype(f(std::declval<T&>()))>(f(value()));
        }
        return result<decltype(f(std::declval<T&>()))>(error());
    }

   private:
    std::variant<T, error_code> data_;
};

// Specialization for void result
template <>
class result<void> {
   public:
    // Construct with success
    result() : error_(error_code::ok) {}

    // Construct with error
    explicit result(error_code ec) : error_(ec) {}

    // Check if result is success
    [[nodiscard]] bool ok() const noexcept { return error_ == error_code::ok; }

    // Check if result contains error
    [[nodiscard]] bool has_error() const noexcept { return error_ != error_code::ok; }

    // Get error code (throws if success)
    [[nodiscard]] error_code error() const {
        if (ok()) {
            throw std::runtime_error("result is success, not error");
        }
        return error_;
    }

   private:
    error_code error_;
};

// Make result with value
template <typename T>
result<T> make_result(T value) {
    return result<T>(std::move(value));
}

// Make result<void> (non-template overload)
inline result<void> make_result() {
    return result<void>();
}

// Make result<void> with error (non-template overload)
inline result<void> make_error(error_code ec) {
    return result<void>(ec);
}

// Make result<T> with error (explicit template: make_error<std::size_t>(ec))
template <typename T>
result<T> make_error(error_code ec) {
    return result<T>(ec);
}

}  // namespace vynx_http
