#include "vynx_http/http_headers.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>

namespace vynx_http {

void http_headers::add(std::string name, std::string value) {
    fields_.push_back({std::move(name), std::move(value)});
}

void http_headers::set(std::string name, std::string value) {
    for (auto& field : fields_) {
        if (field.name.size() != name.size())
            continue;
        bool match = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match) {
            field.value = std::move(value);
            return;
        }
    }
    fields_.push_back({std::move(name), std::move(value)});
}

std::optional<std::string_view> http_headers::get(std::string_view name) const {
    for (const auto& field : fields_) {
        if (field.name.size() != name.size())
            continue;
        bool match = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return field.value;
        }
    }
    return std::nullopt;
}

bool http_headers::contains(std::string_view name) const {
    for (const auto& field : fields_) {
        if (field.name.size() != name.size())
            continue;
        bool match = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

void http_headers::remove(std::string_view name) {
    fields_.erase(
        std::remove_if(fields_.begin(),
                       fields_.end(),
                       [&name](const header_field& field) {
                           if (field.name.size() != name.size())
                               return false;
                           for (std::size_t i = 0; i < name.size(); ++i) {
                               if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                                   std::tolower(static_cast<unsigned char>(name[i]))) {
                                   return false;
                               }
                           }
                           return true;
                       }),
        fields_.end());
}

void http_headers::clear() noexcept {
    fields_.clear();
}

std::size_t http_headers::size() const noexcept {
    return fields_.size();
}

bool http_headers::empty() const noexcept {
    return fields_.empty();
}

const std::vector<header_field>& http_headers::fields() const noexcept {
    return fields_;
}

std::string_view http_headers::host(std::string_view default_host) const {
    auto val = get("Host");
    return val ? *val : default_host;
}

std::optional<std::size_t> http_headers::content_length() const {
    auto val = get("Content-Length");
    if (!val)
        return std::nullopt;

    std::size_t length = 0;
    auto [ptr, ec] = std::from_chars(val->data(), val->data() + val->size(), length);
    if (ec != std::errc{})
        return std::nullopt;
    return length;
}

bool http_headers::is_chunked() const {
    auto val = get("Transfer-Encoding");
    if (!val)
        return false;

    // Split on ',' and match whole tokens (case-insensitive).
    std::string_view encoding = *val;
    constexpr std::string_view needle = "chunked";

    std::size_t pos = 0;
    while (pos < encoding.size()) {
        // Skip leading whitespace/comma
        while (pos < encoding.size() &&
               (encoding[pos] == ' ' || encoding[pos] == '\t' || encoding[pos] == ',')) {
            ++pos;
        }
        if (pos >= encoding.size())
            break;

        // Find end of token
        std::size_t start = pos;
        while (pos < encoding.size() && encoding[pos] != ',' && encoding[pos] != ' ' &&
               encoding[pos] != '\t') {
            ++pos;
        }
        std::size_t token_len = pos - start;

        // Compare token with "chunked"
        if (token_len == needle.size()) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                if (std::tolower(static_cast<unsigned char>(encoding[start + j])) !=
                    std::tolower(static_cast<unsigned char>(needle[j]))) {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
    }
    return false;
}

bool http_headers::connection_keep_alive() const {
    auto val = get("Connection");
    if (!val)
        return true;  // HTTP/1.1 defaults to keep-alive.

    // Split on ',' and match whole tokens (case-insensitive).
    std::string_view conn = *val;

    std::size_t pos = 0;
    while (pos < conn.size()) {
        // Skip leading whitespace/comma
        while (pos < conn.size() && (conn[pos] == ' ' || conn[pos] == '\t' || conn[pos] == ',')) {
            ++pos;
        }
        if (pos >= conn.size())
            break;

        // Find end of token
        std::size_t start = pos;
        while (pos < conn.size() && conn[pos] != ',' && conn[pos] != ' ' && conn[pos] != '\t') {
            ++pos;
        }
        std::size_t token_len = pos - start;

        // Check if this token is "close" (case-insensitive)
        constexpr std::string_view close_needle = "close";
        if (token_len == close_needle.size()) {
            bool match = true;
            for (std::size_t j = 0; j < close_needle.size(); ++j) {
                if (std::tolower(static_cast<unsigned char>(conn[start + j])) !=
                    std::tolower(static_cast<unsigned char>(close_needle[j]))) {
                    match = false;
                    break;
                }
            }
            if (match)
                return false;  // "close" found → not keep-alive
        }
    }
    return true;  // No "close" token found → keep-alive
}

}  // namespace vynx_http
