#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "result.h"

namespace vynx_http {

/// Case-insensitive string comparator for header names.
struct header_name_less {
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) {
                return std::tolower(static_cast<unsigned char>(ca)) <
                       std::tolower(static_cast<unsigned char>(cb));
            });
    }
};

/// A single header field (name, value).
struct header_field {
    std::string name;
    std::string value;
};

/// Ordered collection of header fields with case-insensitive lookup.
///
/// Headers are stored in insertion order.  Lookups are O(n) but
/// HTTP header counts are typically small (< 30).
class http_headers {
   public:
    http_headers() = default;

    /// Append a header.  Duplicates are allowed per RFC 7230 §3.2.2.
    void add(std::string name, std::string value);

    /// Set a header, replacing any existing value with the same name.
    void set(std::string name, std::string value);

    /// Return the first value for `name`, or `std::nullopt`.
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const;

    /// Return `true` if a header with `name` exists.
    [[nodiscard]] bool contains(std::string_view name) const;

    /// Remove all values for `name`.
    void remove(std::string_view name);

    /// Remove all headers.
    void clear() noexcept;

    /// Number of header fields.
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept;

    /// Direct access to underlying storage (iteration, serialization).
    [[nodiscard]] const std::vector<header_field>& fields() const noexcept;

    /// Resolve the `Host` header, falling back to the provided default.
    [[nodiscard]] std::string_view host(std::string_view default_host = "") const;

    /// Resolve `Content-Length` if present.
    [[nodiscard]] std::optional<std::size_t> content_length() const;

    /// Resolve `Transfer-Encoding` value (case-insensitive check for "chunked").
    bool is_chunked() const;

    /// Resolve `Connection: keep-alive` or `Connection: close`.
    bool connection_keep_alive() const;

   private:
    std::vector<header_field> fields_;
};

}  // namespace vynx_http
