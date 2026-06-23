#pragma once

#include <cstdint>
#include <string_view>

#include "result.h"

namespace vynx_http {

/// HTTP method as defined in RFC 7231 and extensions.
enum class http_method : uint8_t {
    get,
    post,
    put,
    patch,
    del,  ///< `delete` is a C++ keyword.
    head,
    options,
    trace,
    connect
};

/// Return the token representation of `m` (e.g. `"GET"`).
constexpr std::string_view to_string(http_method m) noexcept {
    switch (m) {
    case http_method::get:
        return "GET";
    case http_method::post:
        return "POST";
    case http_method::put:
        return "PUT";
    case http_method::patch:
        return "PATCH";
    case http_method::del:
        return "DELETE";
    case http_method::head:
        return "HEAD";
    case http_method::options:
        return "OPTIONS";
    case http_method::trace:
        return "TRACE";
    case http_method::connect:
        return "CONNECT";
    }
    return "UNKNOWN";
}

/// Parse a method token into an `http_method`.  Returns `std::nullopt` on failure.
result<http_method> parse_http_method(std::string_view token);

}  // namespace vynx_http
