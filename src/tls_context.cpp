#include "vynx_http/tls_context.h"

#include <openssl/ssl.h>

#include "vynx_http/error_code.h"
#include "vynx_http/logger.h"

namespace vynx_http {

// ============================================================================
// ssl_ctx_wrapper
// ============================================================================

struct ssl_ctx_wrapper::impl {
    SSL_CTX* ctx = nullptr;

    explicit impl(SSL_CTX* c) noexcept : ctx(c) {}

    ~impl() {
        if (ctx) {
            SSL_CTX_free(ctx);
        }
    }

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;
    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;
};

ssl_ctx_wrapper::ssl_ctx_wrapper() = default;

ssl_ctx_wrapper::~ssl_ctx_wrapper() = default;

ssl_ctx_wrapper::ssl_ctx_wrapper(ssl_ctx_wrapper&& other) noexcept = default;

ssl_ctx_wrapper& ssl_ctx_wrapper::operator=(ssl_ctx_wrapper&& other) noexcept = default;

result<ssl_ctx_wrapper> ssl_ctx_wrapper::create(const tls_config& config) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        VYNX_LOG_ERROR("vynx_http.tls", "SSL_CTX_new failed");
        return make_error<ssl_ctx_wrapper>(error_code::tls_handshake_failed);
    }

    // Load CA bundle or use system defaults
    if (!config.ca_bundle_path.empty()) {
        if (SSL_CTX_load_verify_locations(ctx, config.ca_bundle_path.c_str(), nullptr) != 1) {
            VYNX_LOG_ERROR("vynx_http.tls", "SSL_CTX_load_verify_locations failed");
            SSL_CTX_free(ctx);
            return make_error<ssl_ctx_wrapper>(error_code::tls_certificate_error);
        }
    } else {
        if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            VYNX_LOG_ERROR("vynx_http.tls", "SSL_CTX_set_default_verify_paths failed");
            SSL_CTX_free(ctx);
            return make_error<ssl_ctx_wrapper>(error_code::tls_certificate_error);
        }
    }

    // Configure verification
    if (config.verify_peer || config.verify_hostname) {
        SSL_CTX_set_verify(ctx, SSL_CTX_get_verify_mode(ctx) | SSL_VERIFY_PEER, nullptr);
    }

    ssl_ctx_wrapper wrapper;
    wrapper.impl_ = std::make_unique<impl>(ctx);
    return make_result(std::move(wrapper));
}

void* ssl_ctx_wrapper::native_handle() const noexcept {
    return impl_ ? impl_->ctx : nullptr;
}

// ============================================================================
// tls_context
// ============================================================================

struct tls_context::impl {
    SSL* ssl = nullptr;

    explicit impl(SSL* s) noexcept : ssl(s) {}

    ~impl() {
        if (ssl) {
            SSL_free(ssl);
        }
    }

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;
    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;
};

tls_context::tls_context() = default;

tls_context::~tls_context() = default;

tls_context::tls_context(tls_context&& other) noexcept = default;

tls_context& tls_context::operator=(tls_context&& other) noexcept = default;

result<tls_context> tls_context::create(ssl_ctx_wrapper& ctx,
                                        int socket_fd,
                                        std::string_view hostname) {
    SSL_CTX* raw_ctx = ctx.impl_ ? ctx.impl_->ctx : nullptr;
    if (!raw_ctx) {
        VYNX_LOG_ERROR("vynx_http.tls", "tls_context::create called with null SSL_CTX");
        return make_error<tls_context>(error_code::invalid_argument);
    }

    SSL* ssl = SSL_new(raw_ctx);
    if (!ssl) {
        VYNX_LOG_ERROR("vynx_http.tls", "SSL_new failed");
        return make_error<tls_context>(error_code::tls_handshake_failed);
    }

    if (SSL_set_fd(ssl, socket_fd) != 1) {
        VYNX_LOG_ERROR("vynx_http.tls", "SSL_set_fd failed");
        SSL_free(ssl);
        return make_error<tls_context>(error_code::tls_handshake_failed);
    }

    // Set hostname for verification and SNI.
    // hostname is std::string_view and may not be null-terminated, so
    // construct a std::string to guarantee a trailing '\0'.
    if (!hostname.empty()) {
        std::string host_str(hostname);
#if OPENSSL_VERSION_NUMBER >= 0x40000000L
        SSL_set1_dnsname(ssl, host_str.c_str());
#else
        SSL_set1_host(ssl, host_str.c_str());
#endif
        SSL_set_tlsext_host_name(ssl, host_str.c_str());
    }

    tls_context result_ctx;
    result_ctx.impl_ = std::make_unique<impl>(ssl);
    return make_result(std::move(result_ctx));
}

result<void> tls_context::handshake() {
    int ret = SSL_connect(impl_->ssl);
    if (ret != 1) {
        int ssl_err = SSL_get_error(impl_->ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            return make_error(error_code::operation_timeout);
        }
        if (ssl_err == SSL_ERROR_SSL) {
            VYNX_LOG_ERROR("vynx_http.tls", "SSL_connect SSL error");
            return make_error(error_code::tls_certificate_error);
        }
        VYNX_LOG_ERROR("vynx_http.tls", "SSL_connect failed");
        return make_error(error_code::tls_handshake_failed);
    }
    return make_result();
}

result<std::size_t> tls_context::read(byte_span buffer) {
    // SSL_read requires a non-const buffer; byte_span::data() returns const,
    // but the underlying memory is caller-owned and mutable.
    auto* buf = const_cast<std::byte*>(buffer.data());
    int ret = SSL_read(impl_->ssl, buf, static_cast<int>(buffer.size()));
    if (ret > 0) {
        return make_result(static_cast<std::size_t>(ret));
    }
    if (ret == 0) {
        return make_error<std::size_t>(error_code::connection_reset);
    }
    int ssl_err = SSL_get_error(impl_->ssl, ret);
    if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
        return make_error<std::size_t>(error_code::operation_timeout);
    }
    VYNX_LOG_ERROR("vynx_http.tls", "SSL_read failed");
    return make_error<std::size_t>(error_code::tls_protocol_error);
}

result<std::size_t> tls_context::write(byte_span data) {
    int ret = SSL_write(impl_->ssl, data.data(), static_cast<int>(data.size()));
    if (ret > 0) {
        return make_result(static_cast<std::size_t>(ret));
    }
    if (ret == 0) {
        return make_error<std::size_t>(error_code::connection_reset);
    }
    int ssl_err = SSL_get_error(impl_->ssl, ret);
    if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
        return make_error<std::size_t>(error_code::operation_timeout);
    }
    VYNX_LOG_ERROR("vynx_http.tls", "SSL_write failed");
    return make_error<std::size_t>(error_code::tls_protocol_error);
}

result<void> tls_context::shutdown() {
    int ret = SSL_shutdown(impl_->ssl);
    if (ret < 0) {
        int ssl_err = SSL_get_error(impl_->ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            return make_error(error_code::operation_timeout);
        }
        VYNX_LOG_ERROR("vynx_http.tls", "SSL_shutdown failed");
        return make_error(error_code::tls_protocol_error);
    }
    return make_result();
}

}  // namespace vynx_http
