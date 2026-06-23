#include "vynx_http/timeout_manager.h"

#include <mutex>
#include <unordered_map>
#include <utility>

#include "vynx_http/event_loop.h"

namespace vynx_http {

namespace {

std::size_t count_active_in_map(const std::unordered_map<uint64_t, timeout_entry>& entries) {
    std::size_t count = 0;
    for (const auto& [key, entry] : entries) {
        if (entry.active) {
            count++;
        }
    }
    return count;
}

}  // anonymous namespace

struct timeout_manager::impl {
    event_loop* loop = nullptr;
    std::unordered_map<uint64_t, timeout_entry> entries;
    std::mutex mutex;
    stats stats_;

    uint64_t make_key(native_handle h, timeout_type t) const {
        return static_cast<uint64_t>(h) * 10 + static_cast<uint32_t>(t);
    }
};

// Factory
result<std::unique_ptr<timeout_manager>> timeout_manager::create() {
    try {
        auto mgr = std::unique_ptr<timeout_manager>(new timeout_manager());
        mgr->impl_ = std::make_unique<impl>();
        return make_result(std::move(mgr));
    } catch (...) {
        return result<std::unique_ptr<timeout_manager>>(error_code::allocation_failed);
    }
}

// Private constructor
timeout_manager::timeout_manager() : impl_(nullptr) {}

// Destructor
timeout_manager::~timeout_manager() = default;

// Move operations
timeout_manager::timeout_manager(timeout_manager&&) noexcept = default;
timeout_manager& timeout_manager::operator=(timeout_manager&&) noexcept = default;

// Start a timeout
result<void> timeout_manager::start_timeout(native_handle handle,
                                            timeout_type type,
                                            std::chrono::milliseconds duration,
                                            timeout_callback callback) {
    if (!impl_) {
        return make_error(error_code::invalid_state);
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);

    auto key = impl_->make_key(handle, type);

    // If there's already an active entry for this handle+type, cancel the old timer
    auto it = impl_->entries.find(key);
    if (it != impl_->entries.end() && it->second.active) {
        if (impl_->loop && it->second.timer != invalid_timer_id) {
            impl_->loop->cancel_timer(it->second.timer);
        }
        impl_->stats_.timeouts_cancelled++;
    }

    // Copy callback for the timer lambda (before moving into entry)
    timeout_callback timer_cb = callback;

    // Schedule timer via event loop
    timer_id tid = invalid_timer_id;
    if (impl_->loop) {
        event_loop* loop_ptr = impl_->loop;

        tid = loop_ptr->schedule_timer(
            duration, [this, handle, type, timer_cb](timer_id /*tid*/) mutable {
                std::lock_guard<std::mutex> l(impl_->mutex);
                auto k = impl_->make_key(handle, type);
                auto entry_it = impl_->entries.find(k);
                if (entry_it != impl_->entries.end() && entry_it->second.active) {
                    entry_it->second.active = false;
                    impl_->stats_.timeouts_fired++;
                    if (timer_cb) {
                        timer_cb(handle, type);
                    }
                }
                impl_->stats_.active_timeouts = count_active_in_map(impl_->entries);
            });
    }

    // Store entry
    timeout_entry entry;
    entry.timer = tid;
    entry.handle = handle;
    entry.type = type;
    entry.callback = std::move(callback);
    entry.active = true;

    impl_->entries[key] = std::move(entry);
    impl_->stats_.active_timeouts = count_active_in_map(impl_->entries);

    return make_result();
}

// Cancel a specific timeout
result<void> timeout_manager::cancel_timeout(native_handle handle, timeout_type type) {
    if (!impl_) {
        return make_error(error_code::invalid_state);
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);

    auto key = impl_->make_key(handle, type);
    auto it = impl_->entries.find(key);
    if (it == impl_->entries.end() || !it->second.active) {
        return make_error(error_code::invalid_argument);
    }

    // Cancel the event loop timer
    if (impl_->loop && it->second.timer != invalid_timer_id) {
        impl_->loop->cancel_timer(it->second.timer);
    }

    it->second.active = false;
    impl_->stats_.timeouts_cancelled++;
    impl_->stats_.active_timeouts = count_active_in_map(impl_->entries);

    return make_result();
}

// Cancel all timeouts for a handle
result<void> timeout_manager::cancel_all(native_handle handle) {
    if (!impl_) {
        return make_error(error_code::invalid_state);
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);

    for (auto& [key, entry] : impl_->entries) {
        if (entry.handle == handle && entry.active) {
            if (impl_->loop && entry.timer != invalid_timer_id) {
                impl_->loop->cancel_timer(entry.timer);
            }
            entry.active = false;
            impl_->stats_.timeouts_cancelled++;
        }
    }

    impl_->stats_.active_timeouts = count_active_in_map(impl_->entries);

    return make_result();
}

// Reset idle timeout
result<void> timeout_manager::reset_idle(native_handle handle, std::chrono::milliseconds duration) {
    // Cancel the existing idle timeout if any (ok if it doesn't exist)
    auto cancel_result = cancel_timeout(handle, timeout_type::idle);
    if (cancel_result.has_error() && cancel_result.error() != error_code::invalid_argument) {
        return cancel_result;
    }

    // Start a new idle timeout with a no-op callback
    return start_timeout(
        handle, timeout_type::idle, duration, [](native_handle /*h*/, timeout_type /*t*/) {});
}

// Check if a timeout is active
bool timeout_manager::is_active(native_handle handle, timeout_type type) const {
    if (!impl_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto key = impl_->make_key(handle, type);
    auto it = impl_->entries.find(key);
    return it != impl_->entries.end() && it->second.active;
}

// Get active timeout count
std::size_t timeout_manager::active_count() const {
    if (!impl_) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    return count_active_in_map(impl_->entries);
}

// Get timeout statistics
const timeout_manager::stats& timeout_manager::get_stats() const noexcept {
    static stats empty_stats;
    return impl_ ? impl_->stats_ : empty_stats;
}

// Set event loop
void timeout_manager::set_event_loop(event_loop* loop) {
    if (impl_) {
        impl_->loop = loop;
    }
}

}  // namespace vynx_http
