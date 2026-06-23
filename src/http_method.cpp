#include "vynx_http/http_method.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "vynx_http/error_code.h"

namespace vynx_http {

result<http_method> parse_http_method(std::string_view token) {
    // Uppercase the token for comparison.
    std::string upper(token);
    for (auto& ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    if (upper == "GET")
        return make_result(http_method::get);
    if (upper == "POST")
        return make_result(http_method::post);
    if (upper == "PUT")
        return make_result(http_method::put);
    if (upper == "PATCH")
        return make_result(http_method::patch);
    if (upper == "DELETE")
        return make_result(http_method::del);
    if (upper == "HEAD")
        return make_result(http_method::head);
    if (upper == "OPTIONS")
        return make_result(http_method::options);
    if (upper == "TRACE")
        return make_result(http_method::trace);
    if (upper == "CONNECT")
        return make_result(http_method::connect);

    return make_error<http_method>(error_code::http_invalid_method);
}

}  // namespace vynx_http
