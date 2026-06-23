#pragma once

#include <string>
#include <string_view>
#include <system_error>

namespace vynx_http {

// Error codes specific to HTTP client operations
enum class error_code {
    // Success
    ok = 0,

    // Network errors
    connection_refused,
    connection_timeout,
    connection_reset,
    dns_resolution_failed,
    socket_error,
    address_unavailable,

    // TLS errors
    tls_handshake_failed,
    tls_certificate_error,
    tls_hostname_mismatch,
    tls_protocol_error,

    // HTTP errors
    http_parse_error,
    http_invalid_method,
    http_invalid_version,
    http_invalid_header,
    http_invalid_body,
    http_chunked_encoding_error,
    http_content_length_mismatch,

    // HTTP/2 errors
    h2_frame_error,
    h2_compression_error,
    h2_stream_error,
    h2_flow_control_error,

    // Resource errors
    buffer_overflow,
    buffer_underflow,
    allocation_failed,
    resource_exhausted,

    // Operation errors
    operation_cancelled,
    operation_timeout,
    invalid_state,
    invalid_argument,

    // Compression errors
    decompression_error,
    compression_error,
    invalid_content_encoding
};

// Error category for vynx_http errors
class error_category : public std::error_category {
   public:
    [[nodiscard]] const char* name() const noexcept override { return "vynx_http"; }

    [[nodiscard]] std::string message(int ev) const override {
        switch (static_cast<error_code>(ev)) {
        case error_code::ok:
            return "success";
        case error_code::connection_refused:
            return "connection refused";
        case error_code::connection_timeout:
            return "connection timeout";
        case error_code::connection_reset:
            return "connection reset";
        case error_code::dns_resolution_failed:
            return "dns resolution failed";
        case error_code::socket_error:
            return "socket error";
        case error_code::address_unavailable:
            return "address unavailable";
        case error_code::tls_handshake_failed:
            return "tls handshake failed";
        case error_code::tls_certificate_error:
            return "tls certificate error";
        case error_code::tls_hostname_mismatch:
            return "tls hostname mismatch";
        case error_code::tls_protocol_error:
            return "tls protocol error";
        case error_code::http_parse_error:
            return "http parse error";
        case error_code::http_invalid_method:
            return "http invalid method";
        case error_code::http_invalid_version:
            return "http invalid version";
        case error_code::http_invalid_header:
            return "http invalid header";
        case error_code::http_invalid_body:
            return "http invalid body";
        case error_code::http_chunked_encoding_error:
            return "http chunked encoding error";
        case error_code::http_content_length_mismatch:
            return "http content length mismatch";
        case error_code::h2_frame_error:
            return "h2 frame error";
        case error_code::h2_compression_error:
            return "h2 compression error";
        case error_code::h2_stream_error:
            return "h2 stream error";
        case error_code::h2_flow_control_error:
            return "h2 flow control error";
        case error_code::buffer_overflow:
            return "buffer overflow";
        case error_code::buffer_underflow:
            return "buffer underflow";
        case error_code::allocation_failed:
            return "allocation failed";
        case error_code::resource_exhausted:
            return "resource exhausted";
        case error_code::operation_cancelled:
            return "operation cancelled";
        case error_code::operation_timeout:
            return "operation timeout";
        case error_code::invalid_state:
            return "invalid state";
        case error_code::invalid_argument:
            return "invalid argument";
        case error_code::decompression_error:
            return "decompression error";
        case error_code::compression_error:
            return "compression error";
        case error_code::invalid_content_encoding:
            return "invalid content encoding";
        default:
            return "unknown error";
        }
    }
};

// Get the global error category instance
const error_category& get_error_category() noexcept;

// Make error code from enum
std::error_code make_error_code(error_code ec) noexcept;

// Make error condition from error code
std::error_condition make_error_condition(error_code ec) noexcept;

}  // namespace vynx_http

// Enable implicit conversion to std::error_code
template <>
struct std::is_error_code_enum<vynx_http::error_code> : std::true_type {};
