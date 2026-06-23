#pragma once

#include <memory>
#include <string_view>

#include "byte_span.h"
#include "result.h"
#include "tls_config.h"

namespace vynx_http {

/// RAII wrapper for SSL_CTX* (shared across connections).
///
/// Holds the OpenSSL context with CA store and protocol configuration.
/// Created once per unique tls_config, reused across connections.
class ssl_ctx_wrapper {
    friend class tls_context;

   public:
    /// Create a new SSL context with the given configuration.
    static result<ssl_ctx_wrapper> create(const tls_config& config);

    ~ssl_ctx_wrapper();

    ssl_ctx_wrapper(ssl_ctx_wrapper&& other) noexcept;
    ssl_ctx_wrapper& operator=(ssl_ctx_wrapper&& other) noexcept;

    ssl_ctx_wrapper(const ssl_ctx_wrapper&) = delete;
    ssl_ctx_wrapper& operator=(const ssl_ctx_wrapper&) = delete;

   private:
    ssl_ctx_wrapper();

    /// Internal: raw handle for TLS implementation. Not for public use.
    [[nodiscard]] void* native_handle() const noexcept;

    struct impl;
    std::unique_ptr<impl> impl_;
};

/// Per-connection TLS session (wraps SSL*).
///
/// Handles handshake, encrypted read/write, and shutdown for a single TLS connection.
class tls_context {
   public:
    /// Create a new TLS session over an existing socket.
    static result<tls_context> create(ssl_ctx_wrapper& ctx,
                                      int socket_fd,
                                      std::string_view hostname);

    ~tls_context();

    tls_context(tls_context&& other) noexcept;
    tls_context& operator=(tls_context&& other) noexcept;

    tls_context(const tls_context&) = delete;
    tls_context& operator=(const tls_context&) = delete;

    /// Perform the TLS handshake.
    result<void> handshake();

    /// Read decrypted data from the TLS connection.
    result<std::size_t> read(byte_span buffer);

    /// Write data through the TLS connection.
    result<std::size_t> write(byte_span data);

    /// Shut down the TLS connection.
    result<void> shutdown();

   private:
    tls_context();
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vynx_http
