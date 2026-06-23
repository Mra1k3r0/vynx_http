#pragma once

#include <string>

#include "byte_buffer.h"
#include "http_headers.h"
#include "http_method.h"

namespace vynx_http {

/// An HTTP/1.1 request.
struct http_request {
    http_method method = http_method::get;
    std::string uri;     ///< Path + query (e.g. "/search?q=1").
    std::string host;    ///< Target host (used for `Host` header).
    std::string scheme;  ///< "http" or "https". Set by parse_url, used for TLS dispatch.
    uint16_t port = 80;
    http_headers headers;
    byte_buffer body;

    /// Serialize the request line + headers + body into raw bytes.
    [[nodiscard]] byte_buffer serialize() const;

    /// Build the request line (e.g. "GET /path HTTP/1.1\r\n").
    [[nodiscard]] std::string request_line() const;
};

}  // namespace vynx_http
