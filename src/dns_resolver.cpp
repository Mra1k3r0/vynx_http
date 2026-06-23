#include "vynx_http/dns_resolver.h"

#include <algorithm>
#include <mutex>

#include "vynx_http/logger.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef __GNUC__
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vynx_http {

// DNS resolver implementation
struct dns_resolver::impl {
    dns_config config_;
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, dns_cache_entry> cache_;
    std::size_t cache_hits_ = 0;
    std::size_t cache_misses_ = 0;
    std::size_t cache_expired_ = 0;

    impl(dns_config cfg) : config_(std::move(cfg)) {}

    ~impl() = default;

    bool is_cache_valid(const dns_cache_entry& entry) const {
        return std::chrono::steady_clock::now() < entry.inserted + entry.ttl;
    }

    void evict_expired() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now >= it->second.inserted + it->second.ttl) {
                it = cache_.erase(it);
                cache_expired_++;
            } else {
                ++it;
            }
        }
    }

    void evict_oldest_if_needed() {
        if (cache_.size() <= config_.max_cache_size) {
            return;
        }

        // Find and remove oldest entry
        auto oldest =
            std::min_element(cache_.begin(), cache_.end(), [](const auto& a, const auto& b) {
                return a.second.inserted < b.second.inserted;
            });

        if (oldest != cache_.end()) {
            cache_.erase(oldest);
        }
    }

    std::vector<socket_address> resolve_addresses(const std::string& host) {
        struct addrinfo hints;
        struct addrinfo* result_ptr = nullptr;

        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string port_str = std::to_string(config_.port);

        int status = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result_ptr);
        if (status != 0) {
            return {};
        }

        std::vector<socket_address> addresses;
        for (auto* rp = result_ptr; rp != nullptr; rp = rp->ai_next) {
            if (rp->ai_family == AF_INET) {
                auto& addr4 = *reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
                ipv4_address addr;
                std::memcpy(addr.bytes, &addr4.sin_addr.s_addr, 4);
                addr.port = ntohs(addr4.sin_port);
                addresses.push_back(addr);
            } else if (rp->ai_family == AF_INET6 && config_.enable_ipv6) {
                auto& addr6 = *reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
                ipv6_address addr;
                std::memcpy(addr.bytes, addr6.sin6_addr.s6_addr, 16);
                addr.port = ntohs(addr6.sin6_port);
                addr.scope_id = addr6.sin6_scope_id;
                addresses.push_back(addr);
            }
        }

        freeaddrinfo(result_ptr);
        return addresses;
    }
};

// dns_resolver implementation
dns_resolver::dns_resolver() : impl_(nullptr) {}

dns_resolver::~dns_resolver() = default;

dns_resolver::dns_resolver(dns_resolver&&) noexcept = default;
dns_resolver& dns_resolver::operator=(dns_resolver&&) noexcept = default;

result<std::unique_ptr<dns_resolver>> dns_resolver::create(dns_config config) {
    auto resolver = std::unique_ptr<dns_resolver>(new dns_resolver());
    resolver->impl_ = std::make_unique<impl>(config);
    return make_result(std::move(resolver));
}

result<std::vector<socket_address>> dns_resolver::resolve(std::string_view host) {
    std::string host_str(host);

    // Check cache first
    {
        std::lock_guard lock(impl_->cache_mutex_);
        auto it = impl_->cache_.find(host_str);
        if (it != impl_->cache_.end()) {
            if (impl_->is_cache_valid(it->second)) {
                impl_->cache_hits_++;
                std::vector<socket_address> addresses;
                for (const auto& record : it->second.result.records) {
                    if (record.type == dns_record_type::a && record.data.size() == 4) {
                        ipv4_address addr;
                        std::memcpy(addr.bytes, record.data.data(), 4);
                        addr.port = impl_->config_.port;
                        addresses.push_back(addr);
                    } else if (record.type == dns_record_type::aaaa && record.data.size() == 16) {
                        ipv6_address addr;
                        std::memcpy(addr.bytes, record.data.data(), 16);
                        addr.port = impl_->config_.port;
                        addr.scope_id = 0;
                        addresses.push_back(addr);
                    }
                }
                return make_result(std::move(addresses));
            } else {
                impl_->cache_.erase(it);
                impl_->cache_expired_++;
            }
        }
    }

    impl_->cache_misses_++;

    // Perform resolution
    auto addresses = impl_->resolve_addresses(host_str);
    if (addresses.empty()) {
        return result<std::vector<socket_address>>(error_code::dns_resolution_failed);
    }

    // Cache the result
    {
        std::lock_guard lock(impl_->cache_mutex_);
        impl_->evict_oldest_if_needed();

        dns_cache_entry entry;
        entry.inserted = std::chrono::steady_clock::now();
        entry.ttl = impl_->config_.cache_ttl;
        entry.result.query_time = entry.inserted;
        entry.result.from_cache = false;

        for (const auto& addr : addresses) {
            dns_record record;
            record.name = host_str;
            record.ttl = static_cast<uint32_t>(impl_->config_.cache_ttl.count());
            record.expiry = entry.inserted + entry.ttl;
            record.priority = 0;
            record.port = impl_->config_.port;

            if (std::holds_alternative<ipv4_address>(addr)) {
                const auto& ipv4 = std::get<ipv4_address>(addr);
                record.type = dns_record_type::a;
                record.data.assign(reinterpret_cast<const char*>(ipv4.bytes), 4);
            } else if (std::holds_alternative<ipv6_address>(addr)) {
                const auto& ipv6 = std::get<ipv6_address>(addr);
                record.type = dns_record_type::aaaa;
                record.data.assign(reinterpret_cast<const char*>(ipv6.bytes), 16);
            }

            entry.result.records.push_back(std::move(record));
        }

        impl_->cache_[host_str] = std::move(entry);
    }

    return make_result(std::move(addresses));
}

void dns_resolver::resolve_async(std::string_view host, resolve_callback callback) {
    // Simplified async implementation: resolve synchronously and post callback
    auto result = resolve(host);
    if (result.has_error()) {
        callback(result.error(), {});
    } else {
        callback(error_code::ok, std::move(result.value()));
    }
}

result<std::vector<dns_record>> dns_resolver::resolve_record(std::string_view host,
                                                             dns_record_type type) {
    std::string host_str(host);

    // Check cache first
    {
        std::lock_guard lock(impl_->cache_mutex_);
        auto it = impl_->cache_.find(host_str);
        if (it != impl_->cache_.end() && impl_->is_cache_valid(it->second)) {
            std::vector<dns_record> filtered;
            for (const auto& record : it->second.result.records) {
                if (record.type == type) {
                    filtered.push_back(record);
                }
            }
            return make_result(std::move(filtered));
        }
    }

    // Perform resolution
    auto addresses = impl_->resolve_addresses(host_str);
    std::vector<dns_record> records;

    for (const auto& addr : addresses) {
        dns_record record;
        record.name = host_str;
        record.ttl = static_cast<uint32_t>(impl_->config_.cache_ttl.count());
        record.expiry = std::chrono::steady_clock::now() + impl_->config_.cache_ttl;
        record.priority = 0;
        record.port = impl_->config_.port;

        if (std::holds_alternative<ipv4_address>(addr)) {
            const auto& ipv4 = std::get<ipv4_address>(addr);
            record.type = dns_record_type::a;
            record.data.assign(reinterpret_cast<const char*>(ipv4.bytes), 4);
        } else if (std::holds_alternative<ipv6_address>(addr)) {
            const auto& ipv6 = std::get<ipv6_address>(addr);
            record.type = dns_record_type::aaaa;
            record.data.assign(reinterpret_cast<const char*>(ipv6.bytes), 16);
        }

        if (record.type == type) {
            records.push_back(std::move(record));
        }
    }

    if (records.empty() && addresses.empty()) {
        return result<std::vector<dns_record>>(error_code::dns_resolution_failed);
    }

    return make_result(std::move(records));
}

void dns_resolver::clear_cache() {
    std::lock_guard lock(impl_->cache_mutex_);
    impl_->cache_.clear();
}

dns_resolver::cache_stats dns_resolver::get_cache_stats() const {
    std::lock_guard lock(impl_->cache_mutex_);
    return {impl_->cache_.size(), impl_->cache_hits_, impl_->cache_misses_, impl_->cache_expired_};
}

void dns_resolver::update_config(const dns_config& config) {
    impl_->config_ = config;
}

const dns_config& dns_resolver::config() const noexcept {
    return impl_->config_;
}

// Happy Eyeballs implementation
struct happy_eyeballs::impl {
    dns_resolver& resolver_;
    happy_eyeballs_config config_;

    impl(dns_resolver& resolver, happy_eyeballs_config cfg) :
        resolver_(resolver), config_(std::move(cfg)) {}

    ~impl() = default;

    result<tcp_socket> connect_with_addresses(std::string_view host,
                                              uint16_t port,
                                              const std::vector<socket_address>& addresses) {
        if (addresses.empty()) {
            return result<tcp_socket>(error_code::connection_refused);
        }

        // Try each address in order
        for (const auto& address : addresses) {
            auto socket_result = tcp_socket::create();
            if (socket_result.has_error()) {
                continue;
            }

            auto sock = std::move(socket_result.value());
            std::string host_str(host);

            auto connect_result = sock.connect(host_str, port);
            if (connect_result.ok()) {
                return make_result(std::move(sock));
            }
            (void)address;  // TODO: connect to specific address directly
        }

        return result<tcp_socket>(error_code::connection_refused);
    }
};

happy_eyeballs::happy_eyeballs() : impl_(nullptr) {}

happy_eyeballs::~happy_eyeballs() = default;

result<std::unique_ptr<happy_eyeballs>> happy_eyeballs::create(dns_resolver& resolver,
                                                               happy_eyeballs_config config) {
    auto he = std::unique_ptr<happy_eyeballs>(new happy_eyeballs());
    he->impl_ = std::make_unique<impl>(resolver, config);
    return make_result(std::move(he));
}

result<tcp_socket> happy_eyeballs::connect(std::string_view host, uint16_t port) {
    // Resolve addresses
    auto addresses_result = impl_->resolver_.resolve(host);
    if (addresses_result.has_error()) {
        return result<tcp_socket>(error_code::dns_resolution_failed);
    }

    auto& addresses = addresses_result.value();

    // Separate IPv4 and IPv6 addresses
    std::vector<socket_address> ipv4_addrs, ipv6_addrs;
    for (const auto& addr : addresses) {
        if (std::holds_alternative<ipv4_address>(addr)) {
            ipv4_addrs.push_back(addr);
        } else {
            ipv6_addrs.push_back(addr);
        }
    }

    // Interleave addresses per RFC 8305
    std::vector<socket_address> interleaved;
    std::size_t max_len = std::max(ipv4_addrs.size(), ipv6_addrs.size());

    for (std::size_t i = 0; i < max_len; ++i) {
        if (impl_->config_.prefer_ipv6) {
            if (i < ipv6_addrs.size())
                interleaved.push_back(ipv6_addrs[i]);
            if (i < ipv4_addrs.size())
                interleaved.push_back(ipv4_addrs[i]);
        } else {
            if (i < ipv4_addrs.size())
                interleaved.push_back(ipv4_addrs[i]);
            if (i < ipv6_addrs.size())
                interleaved.push_back(ipv6_addrs[i]);
        }
    }

    return impl_->connect_with_addresses(host, port, interleaved);
}

void happy_eyeballs::connect_async(std::string_view host,
                                   uint16_t port,
                                   connect_callback callback) {
    auto result = connect(host, port);
    if (result.has_error()) {
        callback(result.error(), tcp_socket::invalid());
    } else {
        callback(error_code::ok, std::move(result.value()));
    }
}

}  // namespace vynx_http
