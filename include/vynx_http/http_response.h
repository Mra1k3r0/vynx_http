#pragma once

#include <cstdint>
#include <string>

#include "byte_buffer.h"
#include "http_headers.h"

namespace vynx_http {

/// HTTP status codes commonly encountered by clients.
enum class http_status : uint16_t {
    ok = 200,
    created = 201,
    no_content = 204,
    moved_permanently = 301,
    found = 302,
    not_modified = 304,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    request_timeout = 408,
    too_many_requests = 429,
    internal_server_error = 500,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504
};

/// Human-readable reason phrase for common status codes.
std::string_view reason_phrase(http_status s);

/// An HTTP/1.1 response.
struct http_response {
    uint16_t status_code = 0;
    std::string reason;
    http_headers headers;
    byte_buffer body;

    [[nodiscard]] http_status status() const noexcept {
        return static_cast<http_status>(status_code);
    }

    /// `true` for 2xx.
    [[nodiscard]] bool is_success() const noexcept {
        return status_code >= 200 && status_code < 300;
    }

    /// `true` for 3xx.
    [[nodiscard]] bool is_redirect() const noexcept {
        return status_code >= 300 && status_code < 400;
    }

    [[nodiscard]] bool is_chunked() const { return headers.is_chunked(); }

    [[nodiscard]] std::optional<std::size_t> content_length() const {
        return headers.content_length();
    }
};

}  // namespace vynx_http
