#include "vynx_http/http_client.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <memory>

#include "vynx_http/http_parser.h"
#include "vynx_http/logger.h"
#include "vynx_http/tls_context.h"

namespace vynx_http {

// ── cookie_jar ────────────────────────────────────────────────────────

void cookie_jar::set_cookie(const cookie_entry& cookie) {
    // Replace existing cookie with the same (name, domain, path).
    for (auto& entry : cookies_) {
        if (entry.name == cookie.name && entry.domain == cookie.domain &&
            entry.path == cookie.path) {
            entry = cookie;
            return;
        }
    }
    cookies_.push_back(cookie);
}

void cookie_jar::set_cookie(std::string_view header_line, std::string_view request_domain) {
    cookie_entry entry;

    // Split on '; ' to get individual directives.
    std::string_view remaining = header_line;
    bool first = true;

    while (!remaining.empty()) {
        auto semi = remaining.find(';');
        std::string_view token;
        if (semi == std::string_view::npos) {
            token = remaining;
            remaining = {};
        } else {
            token = remaining.substr(0, semi);
            remaining = remaining.substr(semi + 1);
            // Skip leading whitespace.
            while (!remaining.empty() && remaining[0] == ' ') {
                remaining.remove_prefix(1);
            }
        }

        // Trim whitespace.
        while (!token.empty() && token[0] == ' ')
            token.remove_prefix(1);
        while (!token.empty() && token.back() == ' ')
            token.remove_suffix(1);

        if (first) {
            // "name=value" pair.
            auto eq = token.find('=');
            if (eq != std::string_view::npos) {
                entry.name = std::string(token.substr(0, eq));
                entry.value = std::string(token.substr(eq + 1));
            } else {
                entry.name = std::string(token);
            }
            first = false;
            continue;
        }

        // Directive (case-insensitive key).
        auto eq = token.find('=');
        std::string_view key = token;
        std::string_view val;
        if (eq != std::string_view::npos) {
            key = token.substr(0, eq);
            val = token.substr(eq + 1);
        }

        // Case-insensitive compare helper.
        auto iequal = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        };

        if (iequal(key, "domain")) {
            // Strip leading '.' if present.
            if (!val.empty() && val[0] == '.')
                val.remove_prefix(1);
            entry.domain = std::string(val);
        } else if (iequal(key, "path")) {
            entry.path = std::string(val);
        } else if (iequal(key, "secure")) {
            entry.secure = true;
        } else if (iequal(key, "expires")) {
            // Minimal expiry parsing — store the raw string conceptually.
            // A full implementation would parse RFC 6265 date format.
            // For now, mark it as having an expiry and use a default epoch.
            entry.has_expiry = true;
            entry.expires = {};
        }
    }

    // Default domain to request_domain if not set.
    if (entry.domain.empty()) {
        entry.domain = std::string(request_domain);
    }
    // Default path to "/" if not set.
    if (entry.path.empty()) {
        entry.path = "/";
    }

    set_cookie(entry);
}

std::string cookie_jar::build_cookie_header(std::string_view domain, std::string_view path) const {
    std::string header;
    bool first = true;

    for (const auto& cookie : cookies_) {
        // Match domain (cookie domain must be a suffix of the request domain).
        if (!cookie.domain.empty()) {
            // Exact match or suffix match.
            bool match = false;
            if (cookie.domain == domain) {
                match = true;
            } else if (domain.size() > cookie.domain.size()) {
                // Check if domain ends with ".cookie.domain" or ".cookie_domain"
                // but also handle bare "example.com" vs "sub.example.com"
                std::string_view tail = domain.substr(domain.size() - cookie.domain.size());
                if (tail == cookie.domain &&
                    domain[domain.size() - cookie.domain.size() - 1] == '.') {
                    match = true;
                }
            }
            if (!match)
                continue;
        }

        // Match path (cookie path must be a prefix of the request path).
        if (!cookie.path.empty()) {
            if (path.size() < cookie.path.size())
                continue;
            if (path.substr(0, cookie.path.size()) != cookie.path)
                continue;
            // Path prefix match also requires exact or '/' boundary.
            if (path.size() > cookie.path.size() && cookie.path.back() != '/' &&
                path[cookie.path.size()] != '/') {
                continue;
            }
        }

        if (!first) {
            header.append("; ");
        }
        header.append(cookie.name);
        header.push_back('=');
        header.append(cookie.value);
        first = false;
    }

    return header;
}

void cookie_jar::clear() {
    cookies_.clear();
}

std::size_t cookie_jar::size() const noexcept {
    return cookies_.size();
}

// ── http_client ───────────────────────────────────────────────────────

http_client::http_client(event_loop& loop, http_client_config config) :
    loop_(loop), config_(std::move(config)) {}

http_client::~http_client() {
    for (auto& [key, entry] : connections_) {
        (void)key;
        if (entry.socket.valid()) {
            entry.socket.close();
        }
    }
}

// ── URL parsing ───────────────────────────────────────────────────────

result<http_client::parsed_url> http_client::parse_url(std::string_view url) const {
    parsed_url result_url;

    // Must start with "http://" or "https://".
    constexpr std::string_view http_prefix = "http://";
    constexpr std::string_view https_prefix = "https://";

    if (url.size() >= https_prefix.size() && url.substr(0, https_prefix.size()) == https_prefix) {
        url.remove_prefix(https_prefix.size());
        result_url.scheme = "https";
        result_url.port = 443;
    } else if (url.size() >= http_prefix.size() &&
               url.substr(0, http_prefix.size()) == http_prefix) {
        url.remove_prefix(http_prefix.size());
        result_url.scheme = "http";
        result_url.port = 80;
    } else {
        return make_error<parsed_url>(error_code::invalid_argument);
    }

    // Find the end of the host[:port] section (first '/' or end of string).
    auto path_pos = url.find('/');
    std::string_view host_port;
    if (path_pos == std::string_view::npos) {
        host_port = url;
        result_url.path = "/";
    } else {
        host_port = url.substr(0, path_pos);
        result_url.path = std::string(url.substr(path_pos));
    }

    if (host_port.empty()) {
        return make_error<parsed_url>(error_code::invalid_argument);
    }

    // Check for IPv6 address [...].
    if (host_port[0] == '[') {
        auto close_bracket = host_port.find(']');
        if (close_bracket == std::string_view::npos) {
            return make_error<parsed_url>(error_code::invalid_argument);
        }
        result_url.host = std::string(host_port.substr(1, close_bracket - 1));
        // After ']' we might have ':port'.
        if (close_bracket + 1 < host_port.size()) {
            if (host_port[close_bracket + 1] != ':') {
                return make_error<parsed_url>(error_code::invalid_argument);
            }
            auto port_str = host_port.substr(close_bracket + 2);
            if (port_str.empty()) {
                return make_error<parsed_url>(error_code::invalid_argument);
            }
            uint16_t port_val = 0;
            auto [ptr, ec] =
                std::from_chars(port_str.data(), port_str.data() + port_str.size(), port_val);
            if (ec != std::errc{} || port_val == 0) {
                return make_error<parsed_url>(error_code::invalid_argument);
            }
            result_url.port = port_val;
        }
    } else {
        // Regular host or host:port.
        auto colon_pos = host_port.find(':');
        if (colon_pos == std::string_view::npos) {
            result_url.host = std::string(host_port);
            // Keep the default port set above (80 for http, 443 for https).
        } else {
            result_url.host = std::string(host_port.substr(0, colon_pos));
            auto port_str = host_port.substr(colon_pos + 1);
            if (port_str.empty()) {
                return make_error<parsed_url>(error_code::invalid_argument);
            }
            uint16_t port_val = 0;
            auto [ptr, ec] =
                std::from_chars(port_str.data(), port_str.data() + port_str.size(), port_val);
            if (ec != std::errc{} || port_val == 0) {
                return make_error<parsed_url>(error_code::invalid_argument);
            }
            result_url.port = port_val;
        }
    }

    if (result_url.host.empty()) {
        return make_error<parsed_url>(error_code::invalid_argument);
    }

    return make_result(std::move(result_url));
}

// ── defaults ──────────────────────────────────────────────────────────

void http_client::apply_defaults(http_request& req) const {
    // Fill in host/port from config if not set.
    if (req.host.empty() && !config_.default_host.empty()) {
        req.host = config_.default_host;
    }
    if (req.port == 0) {
        req.port = config_.default_port;
    }

    // Host header.
    if (!req.headers.contains("Host")) {
        if (req.port != 80 && req.port != 443) {
            req.headers.set("Host", req.host + ":" + std::to_string(req.port));
        } else {
            req.headers.set("Host", req.host);
        }
    }

    // Connection header.
    if (!req.headers.contains("Connection")) {
        req.headers.set("Connection", config_.keep_alive ? "keep-alive" : "close");
    }

    // Content-Length for non-empty body.
    if (req.body.size() > 0 && !req.headers.contains("Content-Length")) {
        req.headers.set("Content-Length", std::to_string(req.body.size()));
    }

    // User-Agent.
    if (!req.headers.contains("User-Agent")) {
        req.headers.set("User-Agent", "vynx_http/0.1");
    }

    // Accept.
    if (!req.headers.contains("Accept")) {
        req.headers.set("Accept", "*/*");
    }

    // Attach cookies for this host.
    std::string cookie_header = cookies_.build_cookie_header(req.host, req.uri);
    if (!cookie_header.empty()) {
        req.headers.set("Cookie", std::move(cookie_header));
    }
}

// ── execute ───────────────────────────────────────────────────────────

result<http_response> http_client::execute(http_request req) {
    return do_execute(std::move(req));
}

result<http_response> http_client::execute(http_method method,
                                           std::string_view url,
                                           const http_headers& headers,
                                           byte_buffer body) {
    auto parsed = parse_url(url);
    if (parsed.has_error()) {
        return result<http_response>(parsed.error());
    }

    http_request req;
    req.method = method;
    req.uri = std::move(parsed.value().path);
    req.host = std::move(parsed.value().host);
    req.scheme = std::move(parsed.value().scheme);
    req.port = parsed.value().port;
    req.headers = headers;
    req.body = std::move(body);

    return do_execute(std::move(req));
}

void http_client::execute_async(http_request req,
                                std::function<void(result<http_response>)> callback) {
    // Wrap move-only captures in a shared_ptr so the lambda passed to
    // post() is copy-constructible (required by std::function).
    struct async_payload {
        http_client* self;
        http_request request;
        std::function<void(result<http_response>)> cb;
        void run() { cb(self->do_execute(std::move(request))); }
    };
    auto payload =
        std::make_shared<async_payload>(async_payload{this, std::move(req), std::move(callback)});
    loop_.post([payload]() { payload->run(); });
}

// ── TLS helpers ───────────────────────────────────────────────────────

result<void> http_client::ensure_tls_context() {
    if (tls_ctx_)
        return make_result();
    auto ctx_result = ssl_ctx_wrapper::create(config_.tls);
    if (ctx_result.has_error()) {
        return result<void>(ctx_result.error());
    }
    tls_ctx_ = std::make_unique<ssl_ctx_wrapper>(std::move(ctx_result.value()));
    return make_result();
}

// ── idle connection cleanup ───────────────────────────────────────────

void http_client::cleanup_idle() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = connections_.begin(); it != connections_.end();) {
        if (now - it->second.last_active > config_.idle_timeout) {
            if (it->second.socket.valid()) {
                it->second.socket.close();
            }
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

bool http_client::is_idle(const pool_entry& entry) const {
    return std::chrono::steady_clock::now() - entry.last_active > config_.idle_timeout;
}

// ── do_execute (core request logic) ───────────────────────────────────

result<http_response> http_client::do_execute(http_request req) {
    return do_execute(std::move(req), config_.max_redirects);
}

result<http_response> http_client::do_execute(http_request req, uint32_t remaining_redirects) {
    apply_defaults(req);

    // Clean up idle connections before looking up the pool.
    cleanup_idle();

    // Determine scheme: use req.scheme if set, otherwise infer from port.
    std::string scheme = req.scheme.empty() ? (req.port == 443 ? "https" : "http") : req.scheme;

    bool is_tls = (scheme == "https");

    // Build connection pool key with scheme.
    std::string pool_key = scheme + "://" + req.host + ":" + std::to_string(req.port);

    // Get or create connection.
    auto it = connections_.find(pool_key);
    pool_entry* entry_ptr = nullptr;

    if (it != connections_.end() && it->second.socket.valid() &&
        it->second.socket.state() == socket_state::connected) {
        entry_ptr = &it->second;
    } else {
        // Remove stale entry if present.
        if (it != connections_.end()) {
            it->second.socket.close();
            connections_.erase(it);
        }

        auto create_success = tcp_socket::create();
        if (create_success.has_error()) {
            return result<http_response>(create_success.error());
        }

        auto new_sock = std::move(create_success.value());

        // Apply socket options.
        socket_options opts;
        opts.keep_alive = config_.keep_alive;
        opts.no_delay = true;
        opts.connect_timeout = config_.connect_timeout;
        opts.read_timeout = config_.read_timeout;
        opts.write_timeout = config_.write_timeout;
        new_sock.set_options(opts);

        auto connect_success = new_sock.connect(req.host, req.port);
        if (connect_success.has_error()) {
            return result<http_response>(connect_success.error());
        }

        pool_entry new_entry(std::move(new_sock));
        new_entry.is_tls = is_tls;

        // For HTTPS: create TLS context and perform handshake.
        if (is_tls) {
            auto tls_ctx_result = ensure_tls_context();
            if (tls_ctx_result.has_error()) {
                return result<http_response>(tls_ctx_result.error());
            }

            auto tls_result = tls_context::create(*tls_ctx_, new_entry.socket.fd(), req.host);
            if (tls_result.has_error()) {
                return result<http_response>(tls_result.error());
            }

            new_entry.tls.emplace(std::move(tls_result.value()));

            auto handshake_result = new_entry.tls->handshake();
            if (handshake_result.has_error()) {
                return result<http_response>(handshake_result.error());
            }

            new_entry.tls_handshake_done = true;
        }

        new_entry.last_active = std::chrono::steady_clock::now();

        auto insert_result = connections_.emplace(pool_key, std::move(new_entry));
        entry_ptr = &insert_result.first->second;
    }

    // Send and receive.
    auto response_result = send_and_receive(*entry_ptr, req);
    if (response_result.has_error()) {
        // Remove broken connection from pool.
        connections_.erase(pool_key);
        return response_result;
    }

    auto response = std::move(response_result.value());

    // Update last_active on successful round-trip.
    entry_ptr->last_active = std::chrono::steady_clock::now();

    // Parse Set-Cookie headers.
    for (const auto& field : response.headers.fields()) {
        if (field.name.size() == 10) {  // "Set-Cookie"
            bool match = true;
            static constexpr const char* set_cookie = "Set-Cookie";
            for (std::size_t i = 0; i < 10; ++i) {
                if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                    std::tolower(static_cast<unsigned char>(set_cookie[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                cookies_.set_cookie(field.value, req.host);
            }
        }
    }

    // Handle redirect.
    if (response.is_redirect() && config_.follow_redirects && remaining_redirects > 0) {
        auto location = response.headers.get("Location");
        if (!location) {
            return make_result(std::move(response));
        }

        std::string_view loc = *location;

        // Determine the target URL.
        std::string target_url;
        if (loc.substr(0, 7) == "http://" || loc.substr(0, 8) == "https://") {
            target_url = std::string(loc);
        } else if (loc.substr(0, 1) == "/") {
            // Relative redirect — same host:port with same scheme.
            target_url = scheme + "://" + req.host;
            if (req.port != 80 && req.port != 443) {
                target_url += ":" + std::to_string(req.port);
            }
            target_url += std::string(loc);
        } else {
            // Unknown format — return the redirect response as-is.
            return make_result(std::move(response));
        }

        // Invoke callback.
        if (redirect_cb_) {
            std::string from_url = scheme + "://" + req.host;
            if (req.port != 80 && req.port != 443) {
                from_url += ":" + std::to_string(req.port);
            }
            from_url += req.uri;
            redirect_cb_(from_url, target_url, response.status_code);
        }

        // Parse new URL and recurse with decremented budget.
        auto new_parsed = parse_url(target_url);
        if (new_parsed.has_error()) {
            return result<http_response>(new_parsed.error());
        }

        http_request redirect_req;
        redirect_req.method = (response.status_code == 307 || response.status_code == 308)
                                  ? req.method
                                  : http_method::get;
        redirect_req.uri = std::move(new_parsed.value().path);
        redirect_req.host = std::move(new_parsed.value().host);
        redirect_req.scheme = std::move(new_parsed.value().scheme);
        redirect_req.port = new_parsed.value().port;
        // Redirect requests typically drop the body.
        redirect_req.body = byte_buffer();

        // Determine if cross-origin redirect.
        bool is_cross_origin = (redirect_req.host != req.host);

        // Copy original headers except Host, Content-Length, Cookie — they will be re-applied.
        for (const auto& field : req.headers.fields()) {
            // Skip Host, Content-Length, Cookie — they will be re-applied.
            bool skip = false;
            static constexpr const char* host_hdr = "Host";
            static constexpr const char* cl_hdr = "Content-Length";
            static constexpr const char* cookie_hdr = "Cookie";
            for (const char* skip_name : {host_hdr, cl_hdr, cookie_hdr}) {
                if (field.name.size() == std::strlen(skip_name)) {
                    bool match = true;
                    for (std::size_t i = 0; i < field.name.size(); ++i) {
                        if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                            std::tolower(static_cast<unsigned char>(skip_name[i]))) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        skip = true;
                        break;
                    }
                }
            }

            // Skip auth headers on cross-origin redirects.
            if (is_cross_origin) {
                static constexpr const char* auth_hdr = "Authorization";
                static constexpr const char* proxy_auth_hdr = "Proxy-Authorization";
                for (const char* auth_name : {auth_hdr, proxy_auth_hdr}) {
                    if (field.name.size() == std::strlen(auth_name)) {
                        bool auth_match = true;
                        for (std::size_t i = 0; i < field.name.size(); ++i) {
                            if (std::tolower(static_cast<unsigned char>(field.name[i])) !=
                                std::tolower(static_cast<unsigned char>(auth_name[i]))) {
                                auth_match = false;
                                break;
                            }
                        }
                        if (auth_match) {
                            skip = true;
                            break;
                        }
                    }
                }
            }

            if (!skip) {
                redirect_req.headers.add(field.name, field.value);
            }
        }

        // Decrement remaining redirects.
        remaining_redirects--;

        return do_execute(std::move(redirect_req), remaining_redirects);
    }

    // If server said Connection: close, remove from pool.
    if (!response.headers.connection_keep_alive()) {
        connections_.erase(pool_key);
    }

    return make_result(std::move(response));
}

// ── send_and_receive ──────────────────────────────────────────────────

result<http_response> http_client::send_and_receive(pool_entry& entry, const http_request& req) {
    auto send_status = send_request(entry, req);
    if (send_status.has_error()) {
        return result<http_response>(send_status.error());
    }

    return receive_response(entry);
}

// ── send_request ──────────────────────────────────────────────────────

result<void> http_client::send_request(pool_entry& entry, const http_request& req) {
    byte_buffer serialized = req.serialize();

    std::size_t total_sent = 0;
    std::size_t remaining = serialized.size();

    while (remaining > 0) {
        byte_span chunk(serialized.read_ptr() + total_sent, remaining);

        auto send_status = (entry.is_tls && entry.tls_handshake_done) ? entry.tls->write(chunk)
                                                                      : entry.socket.send(chunk);

        if (send_status.has_error()) {
            return result<void>(send_status.error());
        }

        std::size_t bytes_sent = send_status.value();
        if (bytes_sent == 0) {
            // Socket would block — in a blocking context this is unexpected.
            return make_error(error_code::operation_timeout);
        }

        total_sent += bytes_sent;
        remaining -= bytes_sent;
    }

    return make_result();
}

// ── receive_response ──────────────────────────────────────────────────

result<http_response> http_client::receive_response(pool_entry& entry) {
    http_parser parser;
    constexpr std::size_t recv_buf_size = 8192;
    byte_buffer recv_buf(recv_buf_size);

    while (parser.state() != parser_state::complete && parser.state() != parser_state::error) {
        byte_span chunk(recv_buf.write_ptr(), recv_buf.available());

        auto recv_status = (entry.is_tls && entry.tls_handshake_done) ? entry.tls->read(chunk)
                                                                      : entry.socket.receive(chunk);

        if (recv_status.has_error()) {
            // Connection reset or similar — may still have partial data.
            if (parser.state() == parser_state::complete) {
                break;
            }
            return result<http_response>(recv_status.error());
        }

        std::size_t bytes_received = recv_status.value();
        if (bytes_received == 0) {
            // Connection closed by peer.
            if (parser.state() == parser_state::complete) {
                break;
            }
            return make_error<http_response>(error_code::http_parse_error);
        }

        recv_buf.advance_write(bytes_received);

        auto feed_status = parser.feed(byte_span(recv_buf.read_ptr(), recv_buf.size()));
        if (feed_status.has_error()) {
            return make_error<http_response>(feed_status.error());
        }

        // Reset recv buffer for next read (parser copies data internally).
        recv_buf.clear();
    }

    if (parser.state() == parser_state::error) {
        return make_error<http_response>(error_code::http_parse_error);
    }

    if (parser.state() != parser_state::complete) {
        return make_error<http_response>(error_code::http_parse_error);
    }

    return make_result(parser.release_response());
}

// ── convenience methods ───────────────────────────────────────────────

result<http_response> http_client::get(std::string_view url, const http_headers& headers) {
    return execute(http_method::get, url, headers, byte_buffer());
}

result<http_response> http_client::post(std::string_view url,
                                        byte_buffer body,
                                        std::string_view content_type,
                                        const http_headers& headers) {
    http_headers hdrs = headers;
    if (!hdrs.contains("Content-Type")) {
        hdrs.set("Content-Type", std::string(content_type));
    }
    return execute(http_method::post, url, hdrs, std::move(body));
}

result<http_response> http_client::put(std::string_view url,
                                       byte_buffer body,
                                       std::string_view content_type,
                                       const http_headers& headers) {
    http_headers hdrs = headers;
    if (!hdrs.contains("Content-Type")) {
        hdrs.set("Content-Type", std::string(content_type));
    }
    return execute(http_method::put, url, hdrs, std::move(body));
}

result<http_response> http_client::patch(std::string_view url,
                                         byte_buffer body,
                                         std::string_view content_type,
                                         const http_headers& headers) {
    http_headers hdrs = headers;
    if (!hdrs.contains("Content-Type")) {
        hdrs.set("Content-Type", std::string(content_type));
    }
    return execute(http_method::patch, url, hdrs, std::move(body));
}

result<http_response> http_client::del(std::string_view url, const http_headers& headers) {
    return execute(http_method::del, url, headers, byte_buffer());
}

result<http_response> http_client::head(std::string_view url, const http_headers& headers) {
    return execute(http_method::head, url, headers, byte_buffer());
}

result<http_response> http_client::options(std::string_view url, const http_headers& headers) {
    return execute(http_method::options, url, headers, byte_buffer());
}

void http_client::on_redirect(redirect_callback cb) {
    redirect_cb_ = std::move(cb);
}

}  // namespace vynx_http
