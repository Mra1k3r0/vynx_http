#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "platform.h"
#include "result.h"

namespace vynx_http {

// Forward declaration
class event_loop;

// Timeout configuration
struct timeout_config {
    std::chrono::milliseconds connect_timeout{30000};  // 30s default
    std::chrono::milliseconds read_timeout{60000};     // 60s default
    std::chrono::milliseconds write_timeout{60000};    // 60s default
    std::chrono::milliseconds idle_timeout{300000};    // 5min default
    bool enable_connect_timeout = true;
    bool enable_read_timeout = true;
    bool enable_write_timeout = true;
    bool enable_idle_timeout = true;
};

// Timeout type enum
enum class timeout_type { connect, read, write, idle };

// Timeout callback
using timeout_callback = std::function<void(native_handle handle, timeout_type type)>;

// Timeout entry
struct timeout_entry {
    timer_id timer;
    native_handle handle;
    timeout_type type;
    timeout_callback callback;
    bool active;
};

// Timeout manager - manages per-socket timeouts using event loop timers
class timeout_manager {
   public:
    // Create a timeout manager
    static result<std::unique_ptr<timeout_manager>> create();

    // Destructor
    ~timeout_manager();

    // Move only
    timeout_manager(timeout_manager&&) noexcept;
    timeout_manager& operator=(timeout_manager&&) noexcept;

    // Non-copyable
    timeout_manager(const timeout_manager&) = delete;
    timeout_manager& operator=(const timeout_manager&) = delete;

    // Start a timeout for a socket
    result<void> start_timeout(native_handle handle,
                               timeout_type type,
                               std::chrono::milliseconds duration,
                               timeout_callback callback);

    // Cancel a specific timeout
    result<void> cancel_timeout(native_handle handle, timeout_type type);

    // Cancel all timeouts for a socket
    result<void> cancel_all(native_handle handle);

    // Reset idle timeout (call on activity)
    result<void> reset_idle(native_handle handle, std::chrono::milliseconds duration);

    // Check if a timeout is active
    [[nodiscard]] bool is_active(native_handle handle, timeout_type type) const;

    // Get active timeout count
    [[nodiscard]] std::size_t active_count() const;

    // Get timeout statistics
    struct stats {
        std::size_t timeouts_fired = 0;
        std::size_t timeouts_cancelled = 0;
        std::size_t active_timeouts = 0;
    };

    [[nodiscard]] const stats& get_stats() const noexcept;

    // Set the event loop for timer scheduling
    void set_event_loop(event_loop* loop);

   private:
    timeout_manager();

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vynx_http
