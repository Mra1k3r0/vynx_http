#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dns_resolver.h"
#include "event_loop.h"
#include "http_request.h"
#include "http_response.h"
#include "result.h"
#include "tcp_socket.h"
#include "timeout_manager.h"
#include "tls_config.h"
#include "tls_context.h"

namespace vynx_http {

/// Configuration for HTTP client behavior.
struct http_client_config {
    std::chrono::milliseconds connect_timeout{10'000};
    std::chrono::milliseconds read_timeout{30'000};
    std::chrono::milliseconds write_timeout{10'000};
    std::chrono::milliseconds idle_timeout{60'000};
    uint32_t max_redirects = 10;
    bool follow_redirects = true;
    bool keep_alive = true;
    std::string default_host;
    uint16_t default_port = 80;
    tls_config tls;  ///< TLS configuration for HTTPS connections.
};

/// Stored cookie from a Set-Cookie header.
struct cookie_entry {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
    std::chrono::system_clock::time_point expires{};
    bool has_expiry = false;
};

/// Simple cookie jar: stores and sends cookies per domain.
class cookie_jar {
   public:
    void set_cookie(const cookie_entry& cookie);
    void set_cookie(std::string_view header_line, std::string_view request_domain);

    /// Build the `Cookie` header value for `domain`.
    [[nodiscard]] std::string build_cookie_header(std::string_view domain,
                                                  std::string_view path = "/") const;

    void clear();
    [[nodiscard]] std::size_t size() const noexcept;

   private:
    std::vector<cookie_entry> cookies_;
};

/// Callback type for observing redirects during `execute()`.
using redirect_callback =
    std::function<void(std::string_view from, std::string_view to, uint16_t status)>;

/// High-level HTTP/1.1 client.
///
/// Supports: GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS, TRACE, CONNECT.
/// Features: keep-alive, redirects, cookies, chunked and content-length bodies.
class http_client {
   public:
    explicit http_client(event_loop& loop, http_client_config config = {});
    ~http_client();

    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;

    /// Execute a request synchronously (blocks the calling thread).
    result<http_response> execute(http_request req);

    /// Execute with a custom request builder lambda.
    result<http_response> execute(http_method method,
                                  std::string_view url,
                                  const http_headers& headers = {},
                                  byte_buffer body = byte_buffer());

    /// Execute asynchronously (invokes `callback` on completion).
    void execute_async(http_request req, std::function<void(result<http_response>)> callback);

    // ── Convenience methods ──────────────────────────────────────────

    result<http_response> get(std::string_view url, const http_headers& headers = {});

    result<http_response> post(std::string_view url,
                               byte_buffer body,
                               std::string_view content_type = "application/octet-stream",
                               const http_headers& headers = {});

    result<http_response> put(std::string_view url,
                              byte_buffer body,
                              std::string_view content_type = "application/octet-stream",
                              const http_headers& headers = {});

    result<http_response> patch(std::string_view url,
                                byte_buffer body,
                                std::string_view content_type = "application/octet-stream",
                                const http_headers& headers = {});

    result<http_response> del(std::string_view url, const http_headers& headers = {});

    result<http_response> head(std::string_view url, const http_headers& headers = {});

    result<http_response> options(std::string_view url, const http_headers& headers = {});

    /// Access the cookie jar (for manual inspection / clearing).
    [[nodiscard]] cookie_jar& cookies() noexcept { return cookies_; }
    [[nodiscard]] const cookie_jar& cookies() const noexcept { return cookies_; }

    /// Set a callback invoked on each redirect.
    void on_redirect(redirect_callback cb);

    /// Access config (for runtime tuning).
    [[nodiscard]] http_client_config& config() noexcept { return config_; }

   private:
    struct parsed_url {
        std::string scheme;  ///< "http" or "https"
        std::string host;
        uint16_t port = 80;
        std::string path;  ///< Includes query string.
    };

    /// Connection pool entry with TLS support and idle tracking.
    struct pool_entry {
        explicit pool_entry(tcp_socket sock) : socket(std::move(sock)) {}

        tcp_socket socket;
        std::optional<tls_context> tls;  ///< Valid only for HTTPS connections.
        bool is_tls = false;
        bool tls_handshake_done = false;
        std::chrono::steady_clock::time_point last_active;
    };

    [[nodiscard]] result<parsed_url> parse_url(std::string_view url) const;

    result<http_response> do_execute(http_request req);
    result<http_response> do_execute(http_request req, uint32_t remaining_redirects);

    result<http_response> send_and_receive(pool_entry& entry, const http_request& req);

    result<void> send_request(pool_entry& entry, const http_request& req);

    result<http_response> receive_response(pool_entry& entry);

    void apply_defaults(http_request& req) const;

    void cleanup_idle();
    [[nodiscard]] bool is_idle(const pool_entry& entry) const;
    result<void> ensure_tls_context();

    event_loop& loop_;
    http_client_config config_;
    cookie_jar cookies_;
    redirect_callback redirect_cb_;
    std::unordered_map<std::string, pool_entry>
        connections_;                           ///< Keep-alive pool keyed by scheme://host:port.
    std::unique_ptr<ssl_ctx_wrapper> tls_ctx_;  ///< Lazily created on first HTTPS request.
};

}  // namespace vynx_http
