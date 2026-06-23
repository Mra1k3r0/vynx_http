#pragma once

#include <string_view>
#include <vector>

#include "byte_span.h"
#include "http_request.h"
#include "http_response.h"
#include "result.h"

namespace vynx_http {

/// State of the response parser.
enum class parser_state {
    status_line,     ///< Expecting "HTTP/1.1 200 OK\r\n"
    headers,         ///< Parsing header fields
    body_raw,        ///< Reading Content-Length bytes
    body_chunked,    ///< Reading chunked transfer-encoding
    body_until_eof,  ///< Reading until connection close
    complete,        ///< Response fully parsed
    error
};

/// Incremental HTTP/1.1 response parser.
///
/// Feed data in via `feed()`.  After each call check `state()`:
/// - `complete` → the `response()` is ready.
/// - `error` → `error_message()` explains what went wrong.
/// - anything else → call `feed()` again with more data.
class http_parser {
   public:
    http_parser();

    /// Feed raw bytes into the parser.
    result<void> feed(byte_span data);

    /// Current parser state.
    [[nodiscard]] parser_state state() const noexcept;

    /// The parsed response (valid only when `state() == complete`).
    [[nodiscard]] const http_response& response() const noexcept;

    /// Human-readable error (valid only when `state() == error`).
    [[nodiscard]] std::string_view error_message() const noexcept;

    /// Move the parsed response out. Only valid when state() == complete.
    http_response release_response() { return std::move(response_); }

    /// Reset the parser for reuse.
    void reset();

   private:
    result<void> parse_status_line(std::string_view line);
    result<void> parse_header_line(std::string_view line);
    result<void> process_body();

    parser_state state_ = parser_state::status_line;
    http_response response_;
    std::string error_;
    std::string buffer_;  ///< Accumulates incomplete data across feeds.
    std::size_t body_remaining_ = 0;
    bool content_length_set_ = false;
};

/// Parse a complete response from a buffer.
result<http_response> parse_response(byte_span data);

}  // namespace vynx_http
