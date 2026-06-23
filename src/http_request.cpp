#include "vynx_http/http_request.h"

#include <string>

namespace vynx_http {

std::string http_request::request_line() const {
    std::string line;
    line.append(to_string(method));
    line.push_back(' ');
    line.append(uri);
    line.append(" HTTP/1.1\r\n");
    return line;
}

byte_buffer http_request::serialize() const {
    // Reject URIs containing CR or LF (header injection prevention).
    if (uri.find('\r') != std::string::npos || uri.find('\n') != std::string::npos) {
        return byte_buffer();
    }

    byte_buffer buf;

    // Request line.
    std::string line = request_line();
    buf.write(reinterpret_cast<const std::byte*>(line.data()), line.size());

    // Headers.
    for (const auto& field : headers.fields()) {
        std::string header_line;
        header_line.append(field.name);
        header_line.append(": ");
        header_line.append(field.value);
        header_line.append("\r\n");
        buf.write(reinterpret_cast<const std::byte*>(header_line.data()), header_line.size());
    }

    // End of headers.
    static constexpr const char* crlf = "\r\n";
    buf.write(reinterpret_cast<const std::byte*>(crlf), 2);

    // Body.
    if (body.size() > 0) {
        buf.write(body);
    }

    return buf;
}

}  // namespace vynx_http
