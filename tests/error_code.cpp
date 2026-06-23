#include <gtest/gtest.h>
#include <vynx_http/error_code.h>

#include <system_error>

using namespace vynx_http;

TEST(ErrorCodeTest, DefaultConstruction) {
    error_code ec = error_code::ok;
    EXPECT_EQ(ec, error_code::ok);
}

TEST(ErrorCodeTest, CategoryName) {
    const auto& cat = get_error_category();
    EXPECT_STREQ(cat.name(), "vynx_http");
}

TEST(ErrorCodeTest, Messages) {
    const auto& cat = get_error_category();

    EXPECT_EQ(cat.message(static_cast<int>(error_code::ok)), "success");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::connection_refused)), "connection refused");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::connection_timeout)), "connection timeout");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::tls_handshake_failed)),
              "tls handshake failed");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::http_parse_error)), "http parse error");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::buffer_overflow)), "buffer overflow");
    EXPECT_EQ(cat.message(static_cast<int>(error_code::operation_cancelled)),
              "operation cancelled");
}

TEST(ErrorCodeTest, MakeErrorCode) {
    auto ec = make_error_code(error_code::ok);
    EXPECT_EQ(ec.value(), static_cast<int>(error_code::ok));
    EXPECT_EQ(&ec.category(), &get_error_category());
}

TEST(ErrorCodeTest, ErrorCondition) {
    auto cond = make_error_condition(error_code::connection_refused);
    EXPECT_EQ(cond.value(), static_cast<int>(error_code::connection_refused));
    EXPECT_EQ(&cond.category(), &get_error_category());
}

TEST(ErrorCodeTest, ImplicitConversion) {
    std::error_code ec = error_code::ok;
    EXPECT_EQ(ec.value(), static_cast<int>(error_code::ok));
}

TEST(ErrorCodeTest, Comparison) {
    EXPECT_EQ(error_code::ok, error_code::ok);
    EXPECT_NE(error_code::ok, error_code::connection_refused);
    EXPECT_TRUE(error_code::ok < error_code::connection_refused);
}
