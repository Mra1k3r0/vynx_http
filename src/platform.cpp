#include "vynx_http/platform.h"

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <errno.h>
#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstring>
#include <stdexcept>

namespace vynx_http {

#ifdef __linux__

// epoll-based poller implementation
class epoll_poller : public poller {
   public:
    epoll_poller() : epoll_fd_(epoll_create1(EPOLL_CLOEXEC)) {
        if (epoll_fd_ == -1) {
            throw std::runtime_error("Failed to create epoll instance");
        }
    }

    ~epoll_poller() override {
        if (epoll_fd_ != -1) {
            close(epoll_fd_);
        }
    }

    result<void> add(native_handle handle, event_type events, void* user_data) override {
        struct epoll_event ev;
        ev.events = convert_events(events);
        ev.data.ptr = user_data;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, handle, &ev) == -1) {
            return make_error(error_code::socket_error);
        }

        return make_result();
    }

    result<void> modify(native_handle handle, event_type events, void* user_data) override {
        struct epoll_event ev;
        ev.events = convert_events(events);
        ev.data.ptr = user_data;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, handle, &ev) == -1) {
            return make_error(error_code::socket_error);
        }

        return make_result();
    }

    result<void> remove(native_handle handle) override {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, handle, nullptr) == -1) {
            return make_error(error_code::socket_error);
        }

        return make_result();
    }

    result<std::size_t> wait(event_data* events, std::size_t max_events, int timeout_ms) override {
        std::vector<struct epoll_event> epoll_events(max_events);

        int n =
            epoll_wait(epoll_fd_, epoll_events.data(), static_cast<int>(max_events), timeout_ms);

        if (n == -1) {
            if (errno == EINTR) {
                return make_result(std::size_t(0));
            }
            return result<std::size_t>(error_code::socket_error);
        }

        for (int i = 0; i < n; ++i) {
            events[i].handle = -1;  // epoll doesn't provide the handle in events
            events[i].events = convert_back_events(epoll_events[i].events);
            events[i].user_data = epoll_events[i].data.ptr;
        }

        return make_result(static_cast<std::size_t>(n));
    }

   private:
    uint32_t convert_events(event_type events) const {
        uint32_t result = 0;
        if (has_flag(events, event_type::read))
            result |= EPOLLIN;
        if (has_flag(events, event_type::write))
            result |= EPOLLOUT;
        if (has_flag(events, event_type::error))
            result |= EPOLLERR;
        if (has_flag(events, event_type::hangup))
            result |= EPOLLHUP;
        result |= EPOLLRDHUP;
        return result;
    }

    event_type convert_back_events(uint32_t events) const {
        event_type result = event_type::none;
        if (events & EPOLLIN)
            result = result | event_type::read;
        if (events & EPOLLOUT)
            result = result | event_type::write;
        if (events & EPOLLERR)
            result = result | event_type::error;
        if (events & (EPOLLHUP | EPOLLRDHUP))
            result = result | event_type::hangup;
        return result;
    }

    int epoll_fd_;
};

result<std::unique_ptr<poller>> platform::create_epoll_poller() {
    try {
        return make_result<std::unique_ptr<poller>>(std::make_unique<epoll_poller>());
    } catch (...) {
        return result<std::unique_ptr<poller>>(error_code::allocation_failed);
    }
}

#endif  // __linux__

#ifdef __APPLE__

// kqueue-based poller implementation
class kqueue_poller : public poller {
   public:
    kqueue_poller() : kqueue_fd_(kqueue()) {
        if (kqueue_fd_ == -1) {
            throw std::runtime_error("Failed to create kqueue instance");
        }
    }

    ~kqueue_poller() override {
        if (kqueue_fd_ != -1) {
            close(kqueue_fd_);
        }
    }

    result<void> add(native_handle handle, event_type events, void* user_data) override {
        struct kevent change;
        unsigned short filter = convert_filter(events);
        unsigned short flags = EV_ADD | EV_ENABLE;

        EV_SET(&change, handle, filter, flags, 0, 0, user_data);

        if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
            return make_error(error_code::socket_error);
        }

        return make_result();
    }

    result<void> modify(native_handle handle, event_type events, void* user_data) override {
        // Remove old and add new
        auto remove_result = remove(handle);
        if (remove_result.has_error()) {
            return remove_result;
        }
        return add(handle, events, user_data);
    }

    result<void> remove(native_handle handle) override {
        struct kevent change;
        EV_SET(&change, handle, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr);

        EV_SET(&change, handle, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr);

        return make_result();
    }

    result<std::size_t> wait(event_data* events, std::size_t max_events, int timeout_ms) override {
        std::vector<struct kevent> kevents(max_events);

        struct timespec timeout;
        if (timeout_ms >= 0) {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_nsec = (timeout_ms % 1000) * 1000000;
        }

        int n = kevent(kqueue_fd_,
                       nullptr,
                       0,
                       kevents.data(),
                       static_cast<int>(max_events),
                       timeout_ms >= 0 ? &timeout : nullptr);

        if (n == -1) {
            if (errno == EINTR) {
                return make_result(std::size_t(0));
            }
            return result<std::size_t>(error_code::socket_error);
        }

        for (int i = 0; i < n; ++i) {
            events[i].handle = static_cast<native_handle>(kevents[i].ident);
            events[i].events = convert_back_events(kevents[i]);
            events[i].user_data = kevents[i].udata;
        }

        return make_result(static_cast<std::size_t>(n));
    }

   private:
    unsigned short convert_filter(event_type events) const {
        if (has_flag(events, event_type::read)) {
            return EVFILT_READ;
        }
        if (has_flag(events, event_type::write)) {
            return EVFILT_WRITE;
        }
        return EVFILT_READ;
    }

    event_type convert_back_events(const struct kevent& kev) const {
        event_type result = event_type::none;

        if (kev.filter == EVFILT_READ) {
            result = result | event_type::read;
        }
        if (kev.filter == EVFILT_WRITE) {
            result = result | event_type::write;
        }
        if (kev.flags & EV_ERROR) {
            result = result | event_type::error;
        }
        if (kev.flags & EV_EOF) {
            result = result | event_type::hangup;
        }

        return result;
    }

    int kqueue_fd_;
};

result<std::unique_ptr<poller>> platform::create_kqueue_poller() {
    try {
        return make_result<std::unique_ptr<poller>>(std::make_unique<kqueue_poller>());
    } catch (...) {
        return result<std::unique_ptr<poller>>(error_code::allocation_failed);
    }
}

#endif  // __APPLE__

#ifdef _WIN32

// IOCP-based poller implementation
class iocp_poller : public poller {
   public:
    iocp_poller() : iocp_handle_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0)) {
        if (iocp_handle_ == nullptr) {
            throw std::runtime_error("Failed to create IOCP");
        }
    }

    ~iocp_poller() override {
        if (iocp_handle_ != nullptr) {
            CloseHandle(iocp_handle_);
        }
    }

    result<void> add(native_handle handle, event_type /*events*/, void* user_data) override {
        // Associate file handle with IOCP
        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(handle),
                                   iocp_handle_,
                                   reinterpret_cast<ULONG_PTR>(user_data),
                                   0) == nullptr) {
            return make_error(error_code::socket_error);
        }

        return make_result();
    }

    result<void> modify(native_handle handle, event_type events, void* user_data) override {
        // IOCP doesn't support modification, just add again
        return add(handle, events, user_data);
    }

    result<void> remove(native_handle /*handle*/) override {
        // IOCP doesn't support removal, handles are removed when closed
        return make_result();
    }

    result<std::size_t> wait(event_data* events, std::size_t max_events, int timeout_ms) override {
        std::vector<OVERLAPPED_ENTRY> entries(max_events);
        ULONG num_entries = 0;

        BOOL success =
            GetQueuedCompletionStatusEx(iocp_handle_,
                                        entries.data(),
                                        static_cast<ULONG>(max_events),
                                        &num_entries,
                                        timeout_ms >= 0 ? static_cast<DWORD>(timeout_ms) : INFINITE,
                                        FALSE);

        if (!success) {
            DWORD error = GetLastError();
            if (error == WAIT_TIMEOUT) {
                return make_result(std::size_t(0));
            }
            return result<std::size_t>(error_code::socket_error);
        }

        for (ULONG i = 0; i < num_entries; ++i) {
            events[i].handle = -1;                // IOCP doesn't provide the handle directly
            events[i].events = event_type::read;  // Default to read
            events[i].user_data = reinterpret_cast<void*>(entries[i].lpCompletionKey);
        }

        return make_result(static_cast<std::size_t>(num_entries));
    }

   private:
    HANDLE iocp_handle_;
};

result<std::unique_ptr<poller>> platform::create_iocp_poller() {
    try {
        return make_result<std::unique_ptr<poller>>(std::make_unique<iocp_poller>());
    } catch (...) {
        return result<std::unique_ptr<poller>>(error_code::allocation_failed);
    }
}

#endif  // _WIN32

// Platform-appropriate poller creation
result<std::unique_ptr<poller>> platform::create_poller() {
#ifdef __linux__
    return create_epoll_poller();
#elif defined(__APPLE__)
    return create_kqueue_poller();
#elif defined(_WIN32)
    return create_iocp_poller();
#else
    return result<std::unique_ptr<poller>>(error_code::invalid_state);
#endif
}

// Generic poller implementation
result<std::unique_ptr<poller>> poller::create() {
    return platform::create_poller();
}

}  // namespace vynx_http
