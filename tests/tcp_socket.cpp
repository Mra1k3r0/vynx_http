#include <gtest/gtest.h>
#include <vynx_http/tcp_socket.h>

using namespace vynx_http;

TEST(TcpSocketTest, Create) {
    auto result = tcp_socket::create();
    EXPECT_TRUE(result.ok());
    auto sock = std::move(result).value();
    EXPECT_TRUE(sock.valid());
}

TEST(TcpSocketTest, InvalidSocket) {
    auto sock = tcp_socket::invalid();
    EXPECT_FALSE(sock.valid());
}

TEST(TcpSocketTest, DefaultState) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();
    EXPECT_EQ(sock.state(), socket_state::disconnected);
}

TEST(TcpSocketTest, Close) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();

    auto close_result = sock.close();
    // Already disconnected but should not crash
    EXPECT_TRUE(close_result.ok());
}

TEST(TcpSocketTest, Shutdown) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();

    auto shutdown_result = sock.shutdown();
    // Already disconnected but should not crash
    EXPECT_TRUE(shutdown_result.ok());
}

TEST(TcpSocketTest, Options) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();

    const auto& opts = sock.options();
    EXPECT_TRUE(opts.keep_alive);
    EXPECT_TRUE(opts.no_delay);
}

TEST(TcpSocketTest, Stats) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();

    const auto& st = sock.stats();
    EXPECT_EQ(st.bytes_sent, 0u);
    EXPECT_EQ(st.bytes_received, 0u);
    EXPECT_EQ(st.packets_sent, 0u);
    EXPECT_EQ(st.packets_received, 0u);
}

TEST(TcpSocketTest, SetNonBlocking) {
    auto result = tcp_socket::create();
    ASSERT_TRUE(result.ok());
    auto sock = std::move(result).value();

    auto nb_result = sock.set_non_blocking(true);
    // Should succeed or fail gracefully (platform dependent)
    (void)nb_result;
    EXPECT_TRUE(sock.valid());
}

TEST(TcpSocketTest, ResolveAddress) {
    auto result = resolve_address("localhost", 80);
    // Should succeed on most systems
    if (result.ok()) {
        auto addr = result.value();
        auto str = to_string(addr);
        EXPECT_FALSE(str.empty());
    }
    // If resolution fails (e.g. no network), it should fail gracefully
}

TEST(TcpSocketTest, ResolveAddressToString) {
    auto result = resolve_address("127.0.0.1", 80);
    ASSERT_TRUE(result.ok());
    auto addr = result.value();
    auto str = to_string(addr);
    EXPECT_FALSE(str.empty());
    // Should contain the IP address
    EXPECT_NE(str.find("127.0.0.1"), std::string::npos);
}
