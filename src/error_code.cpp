#include "vynx_http/error_code.h"

namespace vynx_http {

const error_category& get_error_category() noexcept {
    static error_category instance;
    return instance;
}

std::error_code make_error_code(error_code ec) noexcept {
    return std::error_code(static_cast<int>(ec), get_error_category());
}

std::error_condition make_error_condition(error_code ec) noexcept {
    return std::error_condition(static_cast<int>(ec), get_error_category());
}

}  // namespace vynx_http
