#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "result.h"
#include "tcp_socket.h"

namespace vynx_http {

// DNS record types
enum class dns_record_type { a, aaaa, cname, mx, ns, txt, srv };

// DNS record
struct dns_record {
    dns_record_type type;
    std::string name;
    std::string data;
    uint32_t ttl;
    std::chrono::steady_clock::time_point expiry;
    uint16_t priority;  // For MX records
    uint16_t port;      // For SRV records
};

// DNS query result
struct dns_result {
    std::vector<dns_record> records;
    std::chrono::steady_clock::time_point query_time;
    bool from_cache = false;
};

// DNS resolver configuration
struct dns_config {
    std::string primary_server = "8.8.8.8";
    std::string secondary_server = "8.8.4.4";
    uint16_t port = 53;
    std::chrono::seconds timeout{5};
    std::size_t max_cache_size = 1024;
    std::chrono::seconds cache_ttl{300};
    bool enable_ipv6 = true;
    bool enable_canonical_names = true;
};

// DNS cache entry
struct dns_cache_entry {
    dns_result result;
    std::chrono::steady_clock::time_point inserted;
    std::chrono::seconds ttl;
};

// DNS resolver
class dns_resolver {
   public:
    // Create a DNS resolver
    static result<std::unique_ptr<dns_resolver>> create(dns_config config = {});

    // Destructor
    ~dns_resolver();

    // Move only
    dns_resolver(dns_resolver&&) noexcept;
    dns_resolver& operator=(dns_resolver&&) noexcept;

    // Non-copyable
    dns_resolver(const dns_resolver&) = delete;
    dns_resolver& operator=(const dns_resolver&) = delete;

    // Resolve a hostname
    result<std::vector<socket_address>> resolve(std::string_view host);

    // Resolve a hostname asynchronously
    using resolve_callback = std::function<void(error_code, std::vector<socket_address>)>;
    void resolve_async(std::string_view host, resolve_callback callback);

    // Resolve with specific record type
    result<std::vector<dns_record>> resolve_record(std::string_view host, dns_record_type type);

    // Clear the cache
    void clear_cache();

    // Get cache statistics
    struct cache_stats {
        std::size_t size;
        std::size_t hits;
        std::size_t misses;
        std::size_t expired;
    };

    cache_stats get_cache_stats() const;

    // Update configuration
    void update_config(const dns_config& config);

    // Get configuration
    const dns_config& config() const noexcept;

   private:
    dns_resolver();

    struct impl;
    std::unique_ptr<impl> impl_;
};

// Happy Eyeballs configuration
struct happy_eyeballs_config {
    std::chrono::milliseconds connection_delay{250};
    std::chrono::milliseconds dns_timeout{5000};
    std::chrono::milliseconds connect_timeout{3000};
    bool enable_ipv4 = true;
    bool enable_ipv6 = true;
    bool prefer_ipv6 = false;
};

// Happy Eyeballs implementation
class happy_eyeballs {
   public:
    // Create a Happy Eyeballs resolver
    static result<std::unique_ptr<happy_eyeballs>> create(dns_resolver& resolver,
                                                          happy_eyeballs_config config = {});

    // Destructor
    ~happy_eyeballs();

    // Connect using Happy Eyeballs algorithm
    result<tcp_socket> connect(std::string_view host, uint16_t port);

    // Connect asynchronously
    using connect_callback = std::function<void(error_code, tcp_socket)>;
    void connect_async(std::string_view host, uint16_t port, connect_callback callback);

   private:
    happy_eyeballs();

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vynx_http
