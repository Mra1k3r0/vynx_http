#include "vynx_http/http_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string_view>

namespace vynx_http {

/// Maximum allowed chunk size (64 MB). Prevents memory exhaustion from malicious servers.
static constexpr unsigned long max_chunk_size = 64 * 1024 * 1024;

/// Maximum bytes allowed in the parser buffer before headers are complete (8 KB).
static constexpr std::size_t max_header_buffer = 8192;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

http_parser::http_parser() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

result<void> http_parser::feed(byte_span data) {
    if (state_ == parser_state::error || state_ == parser_state::complete) {
        return make_result();
    }

    buffer_.append(reinterpret_cast<const char*>(data.data()), data.size());

    bool progress = true;
    while (progress) {
        progress = false;

        switch (state_) {
        case parser_state::status_line: {
            auto pos = buffer_.find("\r\n");
            if (pos == std::string::npos) {
                // Check header buffer limit before more data arrives.
                if (buffer_.size() > max_header_buffer) {
                    error_ = "header section exceeds maximum size";
                    state_ = parser_state::error;
                    return make_error(error_code::http_parse_error);
                }
                break;
            }

            std::string_view line(buffer_.data(), pos);
            auto line_result = parse_status_line(line);
            if (line_result.has_error()) {
                state_ = parser_state::error;
                return line_result;
            }
            buffer_.erase(0, pos + 2);
            state_ = parser_state::headers;
            progress = true;
            break;
        }

        case parser_state::headers: {
            auto pos = buffer_.find("\r\n");
            if (pos == std::string::npos) {
                if (buffer_.size() > max_header_buffer) {
                    error_ = "header section exceeds maximum size";
                    state_ = parser_state::error;
                    return make_error(error_code::http_parse_error);
                }
                break;
            }

            if (pos == 0) {
                // Empty line = end of headers
                buffer_.erase(0, 2);
                if (response_.is_chunked()) {
                    state_ = parser_state::body_chunked;
                } else if (response_.content_length().has_value()) {
                    body_remaining_ = response_.content_length().value();
                    content_length_set_ = true;
                    state_ = parser_state::body_raw;
                } else {
                    state_ = parser_state::body_until_eof;
                }
                progress = true;
            } else {
                std::string_view line(buffer_.data(), pos);
                auto header_result = parse_header_line(line);
                if (header_result.has_error()) {
                    state_ = parser_state::error;
                    return header_result;
                }
                buffer_.erase(0, pos + 2);
                progress = true;
            }
            break;
        }

        case parser_state::body_raw:
        case parser_state::body_chunked:
        case parser_state::body_until_eof: {
            auto body_result = process_body();
            if (body_result.has_error()) {
                state_ = parser_state::error;
                return body_result;
            }
            break;
        }

        default:
            break;
        }
    }

    return make_result();
}

parser_state http_parser::state() const noexcept {
    return state_;
}

const http_response& http_parser::response() const noexcept {
    return response_;
}

std::string_view http_parser::error_message() const noexcept {
    return error_;
}

void http_parser::reset() {
    state_ = parser_state::status_line;
    response_.status_code = 0;
    response_.reason.clear();
    response_.headers.clear();
    response_.body.reset();
    error_.clear();
    buffer_.clear();
    body_remaining_ = 0;
    content_length_set_ = false;
}

// ---------------------------------------------------------------------------
// Status line parsing
// ---------------------------------------------------------------------------

result<void> http_parser::parse_status_line(std::string_view line) {
    // Find first space – separates version from rest
    auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) {
        error_ = "missing status code in status line";
        return make_error(error_code::http_parse_error);
    }

    std::string_view version = line.substr(0, sp1);

    // Validate HTTP/1.x version
    if (version.size() < 8 || version.substr(0, 7) != "HTTP/1.") {
        error_ = "invalid HTTP version";
        return make_error(error_code::http_invalid_version);
    }

    // Find second space – separates status code from reason phrase
    auto sp2 = line.find(' ', sp1 + 1);
    std::string_view code_str;
    std::string_view reason;
    if (sp2 == std::string_view::npos) {
        code_str = line.substr(sp1 + 1);
        reason = "";
    } else {
        code_str = line.substr(sp1 + 1, sp2 - sp1 - 1);
        reason = line.substr(sp2 + 1);
    }

    // Parse numeric status code
    if (code_str.empty()) {
        error_ = "empty status code";
        return make_error(error_code::http_parse_error);
    }

    int code = 0;
    for (char c : code_str) {
        if (c < '0' || c > '9') {
            error_ = "non-digit in status code";
            return make_error(error_code::http_parse_error);
        }
        code = code * 10 + (c - '0');
    }

    if (code < 100 || code > 599) {
        error_ = "status code out of range";
        return make_error(error_code::http_parse_error);
    }

    response_.status_code = static_cast<uint16_t>(code);
    response_.reason = std::string(reason);

    return make_result();
}

// ---------------------------------------------------------------------------
// Header line parsing
// ---------------------------------------------------------------------------

result<void> http_parser::parse_header_line(std::string_view line) {
    auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        error_ = "missing colon in header line";
        return make_error(error_code::http_invalid_header);
    }

    std::string_view name = line.substr(0, colon);

    // Skip optional whitespace after colon
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) {
        value.remove_prefix(1);
    }

    // Trim trailing whitespace from value
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }

    if (name.empty()) {
        error_ = "empty header name";
        return make_error(error_code::http_invalid_header);
    }

    response_.headers.add(std::string(name), std::string(value));
    return make_result();
}

// ---------------------------------------------------------------------------
// Body processing
// ---------------------------------------------------------------------------

result<void> http_parser::process_body() {
    switch (state_) {
    case parser_state::body_raw: {
        if (body_remaining_ == 0) {
            state_ = parser_state::complete;
            return make_result();
        }

        if (buffer_.empty())
            return make_result();

        std::size_t to_read = std::min(static_cast<std::size_t>(body_remaining_), buffer_.size());
        response_.body.write(reinterpret_cast<const std::byte*>(buffer_.data()), to_read);
        buffer_.erase(0, to_read);
        body_remaining_ -= to_read;

        if (body_remaining_ == 0) {
            state_ = parser_state::complete;
        }
        return make_result();
    }

    case parser_state::body_chunked: {
        // Look for the chunk size line
        while (true) {
            auto pos = buffer_.find("\r\n");
            if (pos == std::string::npos)
                return make_result();

            std::string_view chunk_line(buffer_.data(), pos);

            // Parse hex chunk size (ignore chunk extensions after ';')
            auto semi = chunk_line.find(';');
            std::string_view hex_str =
                (semi != std::string_view::npos) ? chunk_line.substr(0, semi) : chunk_line;

            // Trim any leading whitespace from hex
            while (!hex_str.empty() && hex_str[0] == ' ') {
                hex_str.remove_prefix(1);
            }

            if (hex_str.empty()) {
                error_ = "empty chunk size";
                return make_error(error_code::http_chunked_encoding_error);
            }

            unsigned long chunk_size = 0;
            auto [ptr, ec] =
                std::from_chars(hex_str.data(), hex_str.data() + hex_str.size(), chunk_size, 16);
            if (ec != std::errc{} || chunk_size > max_chunk_size) {
                error_ = "chunk size overflow or exceeds maximum";
                return make_error(error_code::http_chunked_encoding_error);
            }

            // Consume the size line + CRLF
            buffer_.erase(0, pos + 2);

            // Zero-size chunk = final chunk
            if (chunk_size == 0) {
                // Skip trailing CRLF after final chunk
                if (buffer_.size() >= 2 && buffer_[0] == '\r' && buffer_[1] == '\n') {
                    buffer_.erase(0, 2);
                }
                state_ = parser_state::complete;
                return make_result();
            }

            // Need chunk_size bytes + trailing CRLF
            std::size_t need = chunk_size + 2;
            if (buffer_.size() < need)
                return make_result();

            // Copy chunk data into response body
            response_.body.write(reinterpret_cast<const std::byte*>(buffer_.data()), chunk_size);
            buffer_.erase(0, need);
        }
    }

    case parser_state::body_until_eof:
        // Append everything in the buffer to the body
        if (!buffer_.empty()) {
            response_.body.write(reinterpret_cast<const std::byte*>(buffer_.data()),
                                 buffer_.size());
            buffer_.clear();
        }
        return make_result();

    default:
        return make_result();
    }
}

// ---------------------------------------------------------------------------
// Convenience free function
// ---------------------------------------------------------------------------

result<http_response> parse_response(byte_span data) {
    http_parser parser;
    auto feed_result = parser.feed(data);
    if (feed_result.has_error()) {
        return make_error<http_response>(feed_result.error());
    }

    if (parser.state() == parser_state::error) {
        return make_error<http_response>(error_code::http_parse_error);
    }

    if (parser.state() != parser_state::complete) {
        return make_error<http_response>(error_code::http_parse_error);
    }

    return make_result(parser.release_response());
}

}  // namespace vynx_http
