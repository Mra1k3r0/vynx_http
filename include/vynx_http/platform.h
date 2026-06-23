#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "error_code.h"
#include "result.h"

namespace vynx_http {

// Platform types
using native_handle = int;
constexpr native_handle invalid_handle = -1;

// Event types
enum class event_type : uint32_t {
    none = 0,
    read = 1,
    write = 2,
    accept = 4,
    connect = 8,
    error = 16,
    hangup = 32
};

// Event flags operator overloads
constexpr event_type operator|(event_type a, event_type b) noexcept {
    return static_cast<event_type>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr event_type operator&(event_type a, event_type b) noexcept {
    return static_cast<event_type>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool has_flag(event_type events, event_type flag) noexcept {
    return (static_cast<uint32_t>(events) & static_cast<uint32_t>(flag)) != 0;
}

// Event structure
struct event_data {
    native_handle handle;
    event_type events;
    void* user_data;
};

// Poller interface
class poller {
   public:
    virtual ~poller() = default;

    // Create a new poller
    static result<std::unique_ptr<poller>> create();

    // Add a handle to the poller
    virtual result<void> add(native_handle handle, event_type events, void* user_data) = 0;

    // Modify events for a handle
    virtual result<void> modify(native_handle handle, event_type events, void* user_data) = 0;

    // Remove a handle from the poller
    virtual result<void> remove(native_handle handle) = 0;

    // Wait for events
    virtual result<std::size_t> wait(event_data* events,
                                     std::size_t max_events,
                                     int timeout_ms) = 0;
};

// Platform-specific poller implementations
namespace platform {

// Create epoll-based poller (Linux)
result<std::unique_ptr<poller>> create_epoll_poller();

// Create kqueue-based poller (macOS/BSD)
result<std::unique_ptr<poller>> create_kqueue_poller();

// Create IOCP-based poller (Windows)
result<std::unique_ptr<poller>> create_iocp_poller();

// Create platform-appropriate poller
result<std::unique_ptr<poller>> create_poller();

}  // namespace platform

// Timer types
using timer_id = uint64_t;
constexpr timer_id invalid_timer_id = 0;

// Timer callback
using timer_callback = std::function<void(timer_id)>;

}  // namespace vynx_http
