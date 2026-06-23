#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "byte_span.h"
#include "error_code.h"
#include "result.h"

namespace vynx_http {

// Socket state enum
enum class socket_state { disconnected, connecting, connected, closing, error };

// Socket options
struct socket_options {
    bool keep_alive = true;
    bool no_delay = true;
    std::chrono::milliseconds keep_alive_idle{60000};
    std::chrono::milliseconds keep_alive_interval{30000};
    int keep_alive_count = 3;
    int receive_buffer_size = 0;  // 0 = system default
    int send_buffer_size = 0;     // 0 = system default
    // Timeout configuration
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds read_timeout{60000};
    std::chrono::milliseconds write_timeout{60000};
};

// Address types
struct ipv4_address {
    uint8_t bytes[4];
    uint16_t port;
};

struct ipv6_address {
    uint8_t bytes[16];
    uint16_t port;
    uint32_t scope_id;
};

using socket_address = std::variant<ipv4_address, ipv6_address>;

// Socket statistics
struct socket_stats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    std::chrono::steady_clock::time_point connected_time;
    std::chrono::steady_clock::time_point last_active_time;
};

// Forward declaration
class event_driver;

// TCP Socket abstraction
class tcp_socket {
   public:
    // Callback types
    using connect_callback = std::function<void(error_code)>;
    using read_callback = std::function<void(error_code, std::size_t)>;
    using write_callback = std::function<void(error_code, std::size_t)>;
    using close_callback = std::function<void(error_code)>;

    // Create a disconnected socket
    static result<tcp_socket> create();

    // Create an invalid/empty socket (for error callbacks)
    static tcp_socket invalid() noexcept;

    // Create from existing file descriptor (takes ownership)
    static result<tcp_socket> from_fd(int fd);

    // Move only
    tcp_socket(tcp_socket&& other) noexcept;
    tcp_socket& operator=(tcp_socket&& other) noexcept;

    // Non-copyable
    tcp_socket(const tcp_socket&) = delete;
    tcp_socket& operator=(const tcp_socket&) = delete;

    // Destructor
    ~tcp_socket();

    // Connect to remote address
    result<void> connect(std::string_view host, uint16_t port);

    // Non-blocking connect (returns immediately)
    result<void> connect_async(std::string_view host, uint16_t port, connect_callback callback);

    // Send data
    result<std::size_t> send(byte_span data);

    // Non-blocking send
    result<void> send_async(byte_span data, write_callback callback);

    // Receive data
    result<std::size_t> receive(byte_span buffer);

    // Non-blocking receive
    result<void> receive_async(byte_span buffer, read_callback callback);

    // Close the socket
    result<void> close();

    // Shutdown the socket
    result<void> shutdown(bool send = true, bool receive = true);

    // Get socket state
    [[nodiscard]] socket_state state() const noexcept;

    // Get local address
    [[nodiscard]] result<socket_address> local_address() const;

    // Get remote address
    [[nodiscard]] result<socket_address> remote_address() const;

    // Get socket options
    [[nodiscard]] const socket_options& options() const noexcept;

    // Set socket options
    result<void> set_options(const socket_options& opts);

    // Get socket statistics
    [[nodiscard]] const socket_stats& stats() const noexcept;

    // Get underlying file descriptor
    [[nodiscard]] int fd() const noexcept;

    // Check if socket is valid
    [[nodiscard]] bool valid() const noexcept;

    // Set non-blocking mode
    result<void> set_non_blocking(bool non_blocking);

    // Set event driver for async operations
    void set_event_driver(event_driver* driver);

   private:
    tcp_socket();

    struct impl;
    std::unique_ptr<impl> impl_;
};

// Address resolution
result<socket_address> resolve_address(std::string_view host, uint16_t port);

// Format address to string
std::string to_string(const socket_address& addr);

}  // namespace vynx_http
