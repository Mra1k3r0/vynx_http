#include "vynx_http/signal_handler.h"

#include <atomic>
#include <mutex>

#include "vynx_http/event_loop.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cstring>
#endif

namespace vynx_http {

namespace {

// File-scope signal state (not nested in signal_handler to avoid private access issues)
struct signal_state {
    std::atomic<bool> received_sigint{false};
    std::atomic<bool> received_sigterm{false};
    std::atomic<bool> received_sighup{false};
    std::atomic<bool> received_sigpipe{false};
    std::atomic<std::size_t> total_count{0};
    signal_type last_sig{signal_type::unknown};
    std::mutex callback_mutex;
    signal_callback callbacks[5];
};

// Global pointer for signal handler callback (signal-safe storage)
std::atomic<signal_state*> g_signal_state{nullptr};

#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
    signal_state* state = g_signal_state.load(std::memory_order_acquire);
    if (!state) {
        return FALSE;
    }

    switch (type) {
    case CTRL_C_EVENT: {
        state->received_sigint.store(true, std::memory_order_release);
        state->last_sig = signal_type::sigint;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        if (state->callbacks[static_cast<int>(signal_type::sigint)]) {
            state->callbacks[static_cast<int>(signal_type::sigint)](signal_type::sigint);
        }
        return TRUE;
    }

    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT: {
        state->received_sigterm.store(true, std::memory_order_release);
        state->last_sig = signal_type::sigterm;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        if (state->callbacks[static_cast<int>(signal_type::sigterm)]) {
            state->callbacks[static_cast<int>(signal_type::sigterm)](signal_type::sigterm);
        }
        return TRUE;
    }

    default:
        return FALSE;
    }
}
#else
void posix_handler(int sig) {
    signal_state* state = g_signal_state.load(std::memory_order_acquire);
    if (!state) {
        return;
    }

    switch (sig) {
    case SIGINT:
        state->received_sigint.store(true, std::memory_order_release);
        state->last_sig = signal_type::sigint;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        if (state->callbacks[static_cast<int>(signal_type::sigint)]) {
            state->callbacks[static_cast<int>(signal_type::sigint)](signal_type::sigint);
        }
        break;

    case SIGTERM:
        state->received_sigterm.store(true, std::memory_order_release);
        state->last_sig = signal_type::sigterm;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        if (state->callbacks[static_cast<int>(signal_type::sigterm)]) {
            state->callbacks[static_cast<int>(signal_type::sigterm)](signal_type::sigterm);
        }
        break;

    case SIGHUP:
        state->received_sighup.store(true, std::memory_order_release);
        state->last_sig = signal_type::sighup;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        if (state->callbacks[static_cast<int>(signal_type::sighup)]) {
            state->callbacks[static_cast<int>(signal_type::sighup)](signal_type::sighup);
        }
        break;

    case SIGPIPE:
        state->received_sigpipe.store(true, std::memory_order_release);
        state->last_sig = signal_type::sigpipe;
        state->total_count.fetch_add(1, std::memory_order_relaxed);
        if (state->callbacks[static_cast<int>(signal_type::sigpipe)]) {
            state->callbacks[static_cast<int>(signal_type::sigpipe)](signal_type::sigpipe);
        }
        break;

    default:
        break;
    }
}
#endif

}  // anonymous namespace

struct signal_handler::impl {
    signal_state state;
};

// Factory
result<std::unique_ptr<signal_handler>> signal_handler::create() {
    try {
        auto handler = std::unique_ptr<signal_handler>(new signal_handler());
        handler->impl_ = std::make_unique<impl>();

        // Register global signal state
        g_signal_state.store(&handler->impl_->state, std::memory_order_release);

#ifdef _WIN32
        SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = posix_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
        sigaction(SIGPIPE, &sa, nullptr);
#endif

        return make_result(std::move(handler));
    } catch (...) {
        return result<std::unique_ptr<signal_handler>>(error_code::allocation_failed);
    }
}

// Private constructor
signal_handler::signal_handler() : impl_(nullptr) {}

// Destructor
signal_handler::~signal_handler() {
    if (impl_) {
        g_signal_state.store(nullptr, std::memory_order_release);

#ifdef _WIN32
        SetConsoleCtrlHandler(ctrl_handler, FALSE);
#else
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
        sigaction(SIGPIPE, &sa, nullptr);
#endif
    }
}

// Move operations
signal_handler::signal_handler(signal_handler&&) noexcept = default;
signal_handler& signal_handler::operator=(signal_handler&&) noexcept = default;

// Register a signal callback
result<void> signal_handler::register_callback(signal_type signal, signal_callback callback) {
    if (!impl_) {
        return make_error(error_code::invalid_state);
    }

    auto idx = static_cast<int>(signal);
    if (idx < 0 || idx >= 5) {
        return make_error(error_code::invalid_argument);
    }

    std::lock_guard<std::mutex> lock(impl_->state.callback_mutex);
    impl_->state.callbacks[idx] = std::move(callback);
    return make_result();
}

// Unregister a signal callback
result<void> signal_handler::unregister_callback(signal_type signal) {
    if (!impl_) {
        return make_error(error_code::invalid_state);
    }

    auto idx = static_cast<int>(signal);
    if (idx < 0 || idx >= 5) {
        return make_error(error_code::invalid_argument);
    }

    std::lock_guard<std::mutex> lock(impl_->state.callback_mutex);
    impl_->state.callbacks[idx] = nullptr;
    return make_result();
}

// Ignore a signal
result<void> signal_handler::ignore(signal_type signal) {
#ifdef _WIN32
    (void)signal;
    return make_result();
#else
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);

    int sig = 0;
    switch (signal) {
    case signal_type::sigint:
        sig = SIGINT;
        break;
    case signal_type::sigterm:
        sig = SIGTERM;
        break;
    case signal_type::sighup:
        sig = SIGHUP;
        break;
    case signal_type::sigpipe:
        sig = SIGPIPE;
        break;
    default:
        return make_error(error_code::invalid_argument);
    }

    if (sigaction(sig, &sa, nullptr) != 0) {
        return make_error(error_code::socket_error);
    }

    return make_result();
#endif
}

// Restore default handler for a signal
result<void> signal_handler::restore_default(signal_type signal) {
#ifdef _WIN32
    (void)signal;
    return make_result();
#else
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    int sig = 0;
    switch (signal) {
    case signal_type::sigint:
        sig = SIGINT;
        break;
    case signal_type::sigterm:
        sig = SIGTERM;
        break;
    case signal_type::sighup:
        sig = SIGHUP;
        break;
    case signal_type::sigpipe:
        sig = SIGPIPE;
        break;
    default:
        return make_error(error_code::invalid_argument);
    }

    if (sigaction(sig, &sa, nullptr) != 0) {
        return make_error(error_code::socket_error);
    }

    return make_result();
#endif
}

// Get number of signals received since creation
std::size_t signal_handler::signal_count() const noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->state.total_count.load(std::memory_order_relaxed);
}

// Check if a signal has been received
bool signal_handler::has_received(signal_type signal) const noexcept {
    if (!impl_) {
        return false;
    }

    switch (signal) {
    case signal_type::sigint:
        return impl_->state.received_sigint.load(std::memory_order_acquire);
    case signal_type::sigterm:
        return impl_->state.received_sigterm.load(std::memory_order_acquire);
    case signal_type::sighup:
        return impl_->state.received_sighup.load(std::memory_order_acquire);
    case signal_type::sigpipe:
        return impl_->state.received_sigpipe.load(std::memory_order_acquire);
    default:
        return false;
    }
}

// Get last received signal
signal_type signal_handler::last_signal() const noexcept {
    if (!impl_) {
        return signal_type::unknown;
    }
    return impl_->state.last_sig;
}

// Convenience: register event loop to stop on SIGINT/SIGTERM
result<void> register_loop_shutdown(event_loop& loop, signal_handler& handler) {
    loop.schedule_timer(std::chrono::milliseconds(0),
                        std::chrono::milliseconds(100),
                        [&loop, &handler](timer_id /*tid*/) {
                            if (handler.has_received(signal_type::sigint) ||
                                handler.has_received(signal_type::sigterm)) {
                                loop.stop();
                            }
                        });

    return make_result();
}

}  // namespace vynx_http
