#pragma once

#include <functional>
#include <memory>

#include "result.h"

namespace vynx_http {

// Forward declaration
class event_loop;

// Signal type enum
enum class signal_type {
    sigint,   // Ctrl+C
    sigterm,  // Termination request
    sighup,   // Hangup
    sigpipe,  // Broken pipe (ignored by default)
    unknown
};

// Signal callback
using signal_callback = std::function<void(signal_type)>;

// Signal handler - cross-platform signal handling
class signal_handler {
   public:
    // Create a signal handler
    static result<std::unique_ptr<signal_handler>> create();

    // Destructor
    ~signal_handler();

    // Move only
    signal_handler(signal_handler&&) noexcept;
    signal_handler& operator=(signal_handler&&) noexcept;

    // Non-copyable
    signal_handler(const signal_handler&) = delete;
    signal_handler& operator=(const signal_handler&) = delete;

    // Register a signal callback
    result<void> register_callback(signal_type signal, signal_callback callback);

    // Unregister a signal callback
    result<void> unregister_callback(signal_type signal);

    // Ignore a signal
    result<void> ignore(signal_type signal);

    // Restore default handler for a signal
    result<void> restore_default(signal_type signal);

    // Get number of signals received since creation
    [[nodiscard]] std::size_t signal_count() const noexcept;

    // Check if a signal has been received
    [[nodiscard]] bool has_received(signal_type signal) const noexcept;

    // Get last received signal
    [[nodiscard]] signal_type last_signal() const noexcept;

   private:
    signal_handler();

    struct impl;
    std::unique_ptr<impl> impl_;
};

// Convenience: register event loop to stop on SIGINT/SIGTERM
result<void> register_loop_shutdown(event_loop& loop, signal_handler& handler);

}  // namespace vynx_http
