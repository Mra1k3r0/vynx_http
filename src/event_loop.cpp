#include "vynx_http/event_loop.h"

#include <algorithm>
#include <stdexcept>

#include "vynx_http/logger.h"

namespace vynx_http {

// Event loop implementation
struct event_loop::impl {
    event_loop_config config;
    std::unique_ptr<poller> poller_;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{false};

    // Event handlers
    std::unordered_map<native_handle, event_handler*> handlers;

    // Timers
    std::vector<timer_entry> timers;
    timer_id next_timer_id = 1;

    // Task queue
    std::vector<std::function<void()>> task_queue;
    std::mutex task_mutex;

    // Statistics
    stats stats_;

    impl(event_loop_config cfg) : config(cfg) {
        auto poller_result = poller::create();
        if (poller_result.has_error()) {
            throw std::runtime_error("Failed to create poller");
        }
        poller_ = std::move(poller_result.value());
        stats_.start_time = std::chrono::steady_clock::now();
    }

    ~impl() = default;

    void process_timers() {
        auto now = std::chrono::steady_clock::now();

        for (auto& timer : timers) {
            if (timer.cancelled)
                continue;

            if (now >= timer.expiry) {
                timer.callback(timer.id);
                stats_.timers_fired++;

                if (timer.repeating) {
                    timer.expiry = now + timer.interval;
                } else {
                    timer.cancelled = true;
                }
            }
        }

        // Remove cancelled timers
        timers.erase(
            std::remove_if(
                timers.begin(), timers.end(), [](const timer_entry& t) { return t.cancelled; }),
            timers.end());
    }

    void process_tasks() {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(task_mutex);
            tasks.swap(task_queue);
        }

        for (auto& task : tasks) {
            task();
            stats_.tasks_executed++;
        }
    }

    int calculate_timeout() {
        if (timers.empty()) {
            return config.default_timeout_ms;
        }

        auto now = std::chrono::steady_clock::now();
        auto min_expiry = std::chrono::steady_clock::time_point::max();

        for (const auto& timer : timers) {
            if (!timer.cancelled && timer.expiry < min_expiry) {
                min_expiry = timer.expiry;
            }
        }

        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(min_expiry - now).count();

        return std::max(0, static_cast<int>(duration));
    }
};

// event_loop implementation
event_loop::event_loop() : impl_(nullptr) {}

event_loop::~event_loop() = default;

event_loop::event_loop(event_loop&&) noexcept = default;
event_loop& event_loop::operator=(event_loop&&) noexcept = default;

result<std::unique_ptr<event_loop>> event_loop::create(event_loop_config config) {
    try {
        auto loop = std::unique_ptr<event_loop>(new event_loop());
        loop->impl_ = std::make_unique<impl>(config);
        return make_result(std::move(loop));
    } catch (...) {
        return result<std::unique_ptr<event_loop>>(error_code::allocation_failed);
    }
}

result<void> event_loop::run() {
    if (impl_->running) {
        return make_error(error_code::invalid_state);
    }

    impl_->running = true;
    impl_->stopped = false;

    while (!impl_->stopped) {
        auto result = run_once(impl_->calculate_timeout());
        if (result.has_error()) {
            impl_->running = false;
            return result;
        }
    }

    impl_->running = false;
    return make_result();
}

result<void> event_loop::run_once(int timeout_ms) {
    // Process pending tasks
    impl_->process_tasks();

    // Wait for events
    constexpr std::size_t max_events = 1024;
    std::vector<event_data> events(max_events);

    auto wait_result = impl_->poller_->wait(events.data(), max_events, timeout_ms);
    if (wait_result.has_error()) {
        return make_error(wait_result.error());
    }

    // Process events
    std::size_t num_events = wait_result.value();
    for (std::size_t i = 0; i < num_events; ++i) {
        auto it = impl_->handlers.find(events[i].handle);
        if (it != impl_->handlers.end()) {
            it->second->on_event(events[i].handle, events[i].events);
            impl_->stats_.events_processed++;
        }
    }

    // Process timers
    impl_->process_timers();

    return make_result();
}

void event_loop::stop() {
    if (impl_) {
        impl_->stopped = true;
    }
}

bool event_loop::is_running() const noexcept {
    return impl_ ? static_cast<bool>(impl_->running) : false;
}

void event_loop::post(std::function<void()> task) {
    if (impl_) {
        std::lock_guard lock(impl_->task_mutex);
        impl_->task_queue.push_back(std::move(task));
    }
}

timer_id event_loop::schedule_timer(std::chrono::milliseconds delay,
                                    timer_callback callback,
                                    bool repeating) {
    timer_entry entry;
    entry.id = impl_->next_timer_id++;
    entry.expiry = std::chrono::steady_clock::now() + delay;
    entry.interval = delay;
    entry.callback = std::move(callback);
    entry.repeating = repeating;
    entry.cancelled = false;

    impl_->timers.push_back(entry);
    return entry.id;
}

timer_id event_loop::schedule_timer(std::chrono::milliseconds delay,
                                    std::chrono::milliseconds interval,
                                    timer_callback callback) {
    timer_entry entry;
    entry.id = impl_->next_timer_id++;
    entry.expiry = std::chrono::steady_clock::now() + delay;
    entry.interval = interval;
    entry.callback = std::move(callback);
    entry.repeating = true;
    entry.cancelled = false;

    impl_->timers.push_back(entry);
    return entry.id;
}

result<void> event_loop::cancel_timer(timer_id id) {
    for (auto& timer : impl_->timers) {
        if (timer.id == id) {
            timer.cancelled = true;
            return make_result();
        }
    }
    return make_error(error_code::invalid_argument);
}

result<void> event_loop::register_socket(tcp_socket& socket,
                                         event_type events,
                                         event_handler* handler) {
    native_handle handle = socket.fd();
    if (handle == invalid_handle) {
        return make_error(error_code::invalid_state);
    }

    auto result = impl_->poller_->add(handle, events, handler);
    if (result.has_error()) {
        return result;
    }

    impl_->handlers[handle] = handler;
    return make_result();
}

result<void> event_loop::modify_socket(tcp_socket& socket, event_type events) {
    native_handle handle = socket.fd();
    if (handle == invalid_handle) {
        return make_error(error_code::invalid_state);
    }

    auto it = impl_->handlers.find(handle);
    if (it == impl_->handlers.end()) {
        return make_error(error_code::invalid_argument);
    }

    return impl_->poller_->modify(handle, events, it->second);
}

result<void> event_loop::unregister_socket(tcp_socket& socket) {
    native_handle handle = socket.fd();
    if (handle == invalid_handle) {
        return make_error(error_code::invalid_state);
    }

    auto result = impl_->poller_->remove(handle);
    if (result.has_error()) {
        return result;
    }

    impl_->handlers.erase(handle);
    return make_result();
}

poller* event_loop::get_poller() noexcept {
    return impl_ ? impl_->poller_.get() : nullptr;
}

const event_loop::stats& event_loop::get_stats() const noexcept {
    static stats empty_stats;
    return impl_ ? impl_->stats_ : empty_stats;
}

// event_loop_scope implementation
event_loop_scope::event_loop_scope(event_loop& loop) : loop_(loop) {}

event_loop_scope::~event_loop_scope() {
    loop_.stop();
}

}  // namespace vynx_http
