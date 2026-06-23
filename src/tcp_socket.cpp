#include "vynx_http/tcp_socket.h"

#include "vynx_http/platform.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <cstring>
#include <stdexcept>

namespace vynx_http {

// Platform-specific socket implementation
#ifdef _WIN32

struct tcp_socket::impl {
    SOCKET fd = INVALID_SOCKET;
    socket_state state = socket_state::disconnected;
    socket_options options;
    socket_stats stats;
    event_driver* driver = nullptr;
    bool non_blocking = false;

    impl() {
        static bool wsa_initialized = false;
        if (!wsa_initialized) {
            WSADATA wsa_data;
            WSAStartup(MAKEWORD(2, 2), &wsa_data);
            wsa_initialized = true;
        }
    }

    ~impl() {
        if (fd != INVALID_SOCKET) {
            closesocket(fd);
        }
    }

    result<void> set_non_blocking(bool enable) {
        u_long mode = enable ? 1 : 0;
        if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
            return make_error(error_code::socket_error);
        }
        non_blocking = enable;
        return make_result();
    }

    result<void> set_socket_options(const socket_options& opts) {
        BOOL keep_alive = opts.keep_alive ? TRUE : FALSE;
        if (setsockopt(fd,
                       SOL_SOCKET,
                       SO_KEEPALIVE,
                       reinterpret_cast<const char*>(&keep_alive),
                       sizeof(keep_alive)) != 0) {
            return make_error(error_code::socket_error);
        }

        BOOL no_delay = opts.no_delay ? TRUE : FALSE;
        if (setsockopt(fd,
                       IPPROTO_TCP,
                       TCP_NODELAY,
                       reinterpret_cast<const char*>(&no_delay),
                       sizeof(no_delay)) != 0) {
            return make_error(error_code::socket_error);
        }

        options = opts;
        return make_result();
    }
};

#else

struct tcp_socket::impl {
    int fd = -1;
    socket_state state = socket_state::disconnected;
    socket_options options;
    socket_stats stats;
    event_driver* driver = nullptr;
    bool non_blocking = false;

    ~impl() {
        if (fd != -1) {
            ::close(fd);
        }
    }

    result<void> set_non_blocking(bool enable) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return make_error(error_code::socket_error);
        }

        if (enable) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }

        if (fcntl(fd, F_SETFL, flags) == -1) {
            return make_error(error_code::socket_error);
        }

        non_blocking = enable;
        return make_result();
    }

    result<void> set_socket_options(const socket_options& opts) {
        int keep_alive = opts.keep_alive ? 1 : 0;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) != 0) {
            return make_error(error_code::socket_error);
        }

        int no_delay = opts.no_delay ? 1 : 0;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0) {
            return make_error(error_code::socket_error);
        }

        options = opts;
        return make_result();
    }
};

#endif

// Helper to convert socket_address to sockaddr for connect()
static void address_to_sockaddr(const socket_address& addr,
                                struct sockaddr_in& addr4,
                                struct sockaddr_in6& addr6,
                                struct sockaddr*& out_ptr,
                                socklen_t& out_len) {
    if (std::holds_alternative<ipv4_address>(addr)) {
        const auto& a = std::get<ipv4_address>(addr);
        std::memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(a.port);
        std::memcpy(&addr4.sin_addr.s_addr, a.bytes, 4);
        out_ptr = reinterpret_cast<struct sockaddr*>(&addr4);
        out_len = sizeof(addr4);
    } else {
        const auto& a = std::get<ipv6_address>(addr);
        std::memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(a.port);
        std::memcpy(addr6.sin6_addr.s6_addr, a.bytes, 16);
        addr6.sin6_scope_id = a.scope_id;
        out_ptr = reinterpret_cast<struct sockaddr*>(&addr6);
        out_len = sizeof(addr6);
    }
}

// tcp_socket implementation
tcp_socket::tcp_socket() : impl_(std::make_unique<impl>()) {}

tcp_socket::~tcp_socket() = default;

tcp_socket::tcp_socket(tcp_socket&&) noexcept = default;
tcp_socket& tcp_socket::operator=(tcp_socket&&) noexcept = default;

tcp_socket tcp_socket::invalid() noexcept {
    return tcp_socket();
}

result<tcp_socket> tcp_socket::create() {
    tcp_socket s;

#ifdef _WIN32
    s.impl_->fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.impl_->fd == INVALID_SOCKET) {
        return result<tcp_socket>(error_code::socket_error);
    }
#else
    s.impl_->fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.impl_->fd == -1) {
        return result<tcp_socket>(error_code::socket_error);
    }
#endif

    s.impl_->state = socket_state::disconnected;
    return make_result(std::move(s));
}

result<tcp_socket> tcp_socket::from_fd(int fd) {
    tcp_socket s;
    s.impl_->fd = fd;
    s.impl_->state = socket_state::connected;
    s.impl_->stats.connected_time = std::chrono::steady_clock::now();
    s.impl_->stats.last_active_time = s.impl_->stats.connected_time;
    return make_result(std::move(s));
}

result<void> tcp_socket::connect(std::string_view host, uint16_t port) {
    if (impl_->state != socket_state::disconnected) {
        return make_error(error_code::invalid_state);
    }

    // Resolve address
    auto addr_result = resolve_address(host, port);
    if (addr_result.has_error()) {
        return result<void>(addr_result.error());
    }

    auto& addr = addr_result.value();
    impl_->state = socket_state::connecting;

    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr* sockaddr_ptr = nullptr;
    socklen_t sockaddr_len = 0;
    address_to_sockaddr(addr, addr4, addr6, sockaddr_ptr, sockaddr_len);

    int rc = ::connect(impl_->fd, sockaddr_ptr, sockaddr_len);

    if (rc == 0) {
        impl_->state = socket_state::connected;
        impl_->stats.connected_time = std::chrono::steady_clock::now();
        impl_->stats.last_active_time = impl_->stats.connected_time;
        return make_result();
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
        return make_result();
    }
#else
    if (errno == EINPROGRESS || errno == EALREADY) {
        return make_result();
    }
#endif

    impl_->state = socket_state::error;
    return make_error(error_code::connection_refused);
}

result<std::size_t> tcp_socket::send(byte_span data) {
    if (impl_->state != socket_state::connected) {
        return result<std::size_t>(error_code::invalid_state);
    }

    ssize_t bytes_sent;

#ifdef _WIN32
    bytes_sent = ::send(
        impl_->fd, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#else
    bytes_sent = ::send(impl_->fd, data.data(), data.size(), 0);
#endif

    if (bytes_sent >= 0) {
        impl_->stats.bytes_sent += bytes_sent;
        impl_->stats.packets_sent++;
        impl_->stats.last_active_time = std::chrono::steady_clock::now();
        return make_result(static_cast<std::size_t>(bytes_sent));
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
        return make_result(std::size_t(0));
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return make_result(std::size_t(0));
    }
#endif

    return result<std::size_t>(error_code::socket_error);
}

result<std::size_t> tcp_socket::receive(byte_span buffer) {
    if (impl_->state != socket_state::connected) {
        return result<std::size_t>(error_code::invalid_state);
    }

    ssize_t bytes_received;

#ifdef _WIN32
    bytes_received = ::recv(impl_->fd,
                            reinterpret_cast<char*>(const_cast<std::byte*>(buffer.data())),
                            static_cast<int>(buffer.size()),
                            0);
#else
    bytes_received = ::recv(impl_->fd,
                            const_cast<void*>(static_cast<const void*>(buffer.data())),
                            buffer.size(),
                            0);
#endif

    if (bytes_received > 0) {
        impl_->stats.bytes_received += bytes_received;
        impl_->stats.packets_received++;
        impl_->stats.last_active_time = std::chrono::steady_clock::now();
        return make_result(static_cast<std::size_t>(bytes_received));
    }

    if (bytes_received == 0) {
        impl_->state = socket_state::disconnected;
        return result<std::size_t>(error_code::connection_reset);
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
        return make_result(std::size_t(0));
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return make_result(std::size_t(0));
    }
#endif

    return result<std::size_t>(error_code::socket_error);
}

result<void> tcp_socket::close() {
#ifdef _WIN32
    if (impl_->fd == INVALID_SOCKET) {
#else
    if (impl_->fd == -1) {
#endif
        return make_result();
    }

    impl_->state = socket_state::closing;

#ifdef _WIN32
    if (closesocket(impl_->fd) != 0) {
        impl_->state = socket_state::error;
        return make_error(error_code::socket_error);
    }
    impl_->fd = INVALID_SOCKET;
#else
    if (::close(impl_->fd) != 0) {
        impl_->state = socket_state::error;
        return make_error(error_code::socket_error);
    }
    impl_->fd = -1;
#endif

    impl_->state = socket_state::disconnected;
    return make_result();
}

result<void> tcp_socket::shutdown(bool send, bool receive) {
#ifdef _WIN32
    if (impl_->fd == INVALID_SOCKET) {
#else
    if (impl_->fd == -1) {
#endif
        return make_result();
    }

    int how;
    if (send && receive) {
#ifdef _WIN32
        how = SD_BOTH;
#else
        how = SHUT_RDWR;
#endif
    } else if (send) {
#ifdef _WIN32
        how = SD_SEND;
#else
        how = SHUT_WR;
#endif
    } else if (receive) {
#ifdef _WIN32
        how = SD_RECEIVE;
#else
        how = SHUT_RD;
#endif
    } else {
        return make_result();
    }

    if (::shutdown(impl_->fd, how) != 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAENOTCONN) {
            return make_error(error_code::socket_error);
        }
#else
        if (errno != ENOTCONN) {
            return make_error(error_code::socket_error);
        }
#endif
    }

    return make_result();
}

socket_state tcp_socket::state() const noexcept {
    return impl_->state;
}

result<socket_address> tcp_socket::local_address() const {
#ifdef _WIN32
    if (impl_->fd == INVALID_SOCKET) {
#else
    if (impl_->fd == -1) {
#endif
        return result<socket_address>(error_code::invalid_state);
    }

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getsockname(impl_->fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) != 0) {
        return result<socket_address>(error_code::socket_error);
    }

    if (addr.ss_family == AF_INET) {
        auto& a = reinterpret_cast<struct sockaddr_in&>(addr);
        ipv4_address r;
        std::memcpy(r.bytes, &a.sin_addr.s_addr, 4);
        r.port = ntohs(a.sin_port);
        return make_result<socket_address>(r);
    } else if (addr.ss_family == AF_INET6) {
        auto& a = reinterpret_cast<struct sockaddr_in6&>(addr);
        ipv6_address r;
        std::memcpy(r.bytes, a.sin6_addr.s6_addr, 16);
        r.port = ntohs(a.sin6_port);
        r.scope_id = a.sin6_scope_id;
        return make_result<socket_address>(r);
    }

    return result<socket_address>(error_code::invalid_argument);
}

result<socket_address> tcp_socket::remote_address() const {
#ifdef _WIN32
    if (impl_->fd == INVALID_SOCKET) {
#else
    if (impl_->fd == -1) {
#endif
        return result<socket_address>(error_code::invalid_state);
    }

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getpeername(impl_->fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) != 0) {
        return result<socket_address>(error_code::socket_error);
    }

    if (addr.ss_family == AF_INET) {
        auto& a = reinterpret_cast<struct sockaddr_in&>(addr);
        ipv4_address r;
        std::memcpy(r.bytes, &a.sin_addr.s_addr, 4);
        r.port = ntohs(a.sin_port);
        return make_result<socket_address>(r);
    } else if (addr.ss_family == AF_INET6) {
        auto& a = reinterpret_cast<struct sockaddr_in6&>(addr);
        ipv6_address r;
        std::memcpy(r.bytes, a.sin6_addr.s6_addr, 16);
        r.port = ntohs(a.sin6_port);
        r.scope_id = a.sin6_scope_id;
        return make_result<socket_address>(r);
    }

    return result<socket_address>(error_code::invalid_argument);
}

const socket_options& tcp_socket::options() const noexcept {
    return impl_->options;
}

result<void> tcp_socket::set_options(const socket_options& opts) {
    return impl_->set_socket_options(opts);
}

const socket_stats& tcp_socket::stats() const noexcept {
    return impl_->stats;
}

int tcp_socket::fd() const noexcept {
    return impl_->fd;
}

bool tcp_socket::valid() const noexcept {
#ifdef _WIN32
    return impl_->fd != INVALID_SOCKET;
#else
    return impl_->fd != -1;
#endif
}

result<void> tcp_socket::set_non_blocking(bool non_blocking) {
    return impl_->set_non_blocking(non_blocking);
}

void tcp_socket::set_event_driver(event_driver* driver) {
    impl_->driver = driver;
}

// Address resolution
result<socket_address> resolve_address(std::string_view host, uint16_t port) {
#ifdef _WIN32
    // Ensure Winsock is initialized before getaddrinfo
    static bool wsa_initialized = false;
    if (!wsa_initialized) {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        wsa_initialized = true;
    }
#endif

    struct addrinfo hints;
    struct addrinfo* result_ptr = nullptr;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string host_str(host);
    std::string port_str = std::to_string(port);

    int status = getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result_ptr);
    if (status != 0) {
        return result<socket_address>(error_code::dns_resolution_failed);
    }

    // Try to get IPv4 first, then IPv6
    for (auto* rp = result_ptr; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            auto* addr4 = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
            ipv4_address r;
            std::memcpy(r.bytes, &addr4->sin_addr.s_addr, 4);
            r.port = ntohs(addr4->sin_port);
            freeaddrinfo(result_ptr);
            return make_result<socket_address>(r);
        }
    }

    for (auto* rp = result_ptr; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET6) {
            auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
            ipv6_address r;
            std::memcpy(r.bytes, addr6->sin6_addr.s6_addr, 16);
            r.port = ntohs(addr6->sin6_port);
            r.scope_id = addr6->sin6_scope_id;
            freeaddrinfo(result_ptr);
            return make_result<socket_address>(r);
        }
    }

    freeaddrinfo(result_ptr);
    return result<socket_address>(error_code::dns_resolution_failed);
}

std::string to_string(const socket_address& addr) {
    if (std::holds_alternative<ipv4_address>(addr)) {
        const auto& ipv4 = std::get<ipv4_address>(addr);
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, ipv4.bytes, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ipv4.port);
    } else if (std::holds_alternative<ipv6_address>(addr)) {
        const auto& ipv6 = std::get<ipv6_address>(addr);
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, ipv6.bytes, buf, sizeof(buf));
        return "[" + std::string(buf) + "]:" + std::to_string(ipv6.port);
    }
    return "unknown";
}

}  // namespace vynx_http
