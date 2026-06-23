#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "platform.h"
#include "result.h"
#include "tcp_socket.h"

namespace vynx_http {

// Event loop configuration
struct event_loop_config {
    std::size_t max_events = 1024;
    int default_timeout_ms = 100;
    bool enable_profiling = false;
};

// Event handler interface
class event_handler {
   public:
    virtual ~event_handler() = default;

    // Called when an event occurs
    virtual void on_event(native_handle handle, event_type events) = 0;

    // Called when an error occurs
    virtual void on_error(native_handle handle, error_code error) = 0;
};

// Timer entry
struct timer_entry {
    timer_id id;
    std::chrono::steady_clock::time_point expiry;
    std::chrono::steady_clock::duration interval;
    timer_callback callback;
    bool repeating;
    bool cancelled;
};

// Event loop
class event_loop {
   public:
    // Create an event loop
    static result<std::unique_ptr<event_loop>> create(event_loop_config config = {});

    // Destructor
    ~event_loop();

    // Move only
    event_loop(event_loop&&) noexcept;
    event_loop& operator=(event_loop&&) noexcept;

    // Non-copyable
    event_loop(const event_loop&) = delete;
    event_loop& operator=(const event_loop&) = delete;

    // Run the event loop (blocks until stop() is called)
    result<void> run();

    // Run one iteration of the event loop
    result<void> run_once(int timeout_ms = -1);

    // Stop the event loop
    void stop();

    // Check if the event loop is running
    [[nodiscard]] bool is_running() const noexcept;

    // Post a task to the event loop
    void post(std::function<void()> task);

    // Schedule a timer
    timer_id schedule_timer(std::chrono::milliseconds delay,
                            timer_callback callback,
                            bool repeating = false);

    // Schedule a timer with interval
    timer_id schedule_timer(std::chrono::milliseconds delay,
                            std::chrono::milliseconds interval,
                            timer_callback callback);

    // Cancel a timer
    result<void> cancel_timer(timer_id id);

    // Register a socket for events
    result<void> register_socket(tcp_socket& socket, event_type events, event_handler* handler);

    // Modify socket events
    result<void> modify_socket(tcp_socket& socket, event_type events);

    // Unregister a socket
    result<void> unregister_socket(tcp_socket& socket);

    // Get the poller
    poller* get_poller() noexcept;

    // Get event loop statistics
    struct stats {
        uint64_t events_processed = 0;
        uint64_t timers_fired = 0;
        uint64_t tasks_executed = 0;
        std::chrono::steady_clock::time_point start_time;
    };

    [[nodiscard]] const stats& get_stats() const noexcept;

   private:
    event_loop();

    struct impl;
    std::unique_ptr<impl> impl_;
};

// Event loop scope guard
class event_loop_scope {
   public:
    event_loop_scope(event_loop& loop);
    ~event_loop_scope();

    // Non-copyable
    event_loop_scope(const event_loop_scope&) = delete;
    event_loop_scope& operator=(const event_loop_scope&) = delete;

   private:
    event_loop& loop_;
};

}  // namespace vynx_http
