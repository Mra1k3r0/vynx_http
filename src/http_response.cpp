#include "vynx_http/http_response.h"

namespace vynx_http {

std::string_view reason_phrase(http_status s) {
    switch (s) {
    case http_status::ok:
        return "OK";
    case http_status::created:
        return "Created";
    case http_status::no_content:
        return "No Content";
    case http_status::moved_permanently:
        return "Moved Permanently";
    case http_status::found:
        return "Found";
    case http_status::not_modified:
        return "Not Modified";
    case http_status::bad_request:
        return "Bad Request";
    case http_status::unauthorized:
        return "Unauthorized";
    case http_status::forbidden:
        return "Forbidden";
    case http_status::not_found:
        return "Not Found";
    case http_status::method_not_allowed:
        return "Method Not Allowed";
    case http_status::request_timeout:
        return "Request Timeout";
    case http_status::too_many_requests:
        return "Too Many Requests";
    case http_status::internal_server_error:
        return "Internal Server Error";
    case http_status::bad_gateway:
        return "Bad Gateway";
    case http_status::service_unavailable:
        return "Service Unavailable";
    case http_status::gateway_timeout:
        return "Gateway Timeout";
    }
    return "Unknown";
}

}  // namespace vynx_http
