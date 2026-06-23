#include <gtest/gtest.h>
#include <vynx_http/result.h>

using namespace vynx_http;

TEST(ResultTest, DefaultConstruction) {
    auto r = make_result();
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.has_error());
}

TEST(ResultTest, ValueConstruction) {
    auto r = make_result(42);
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.has_error());
}

TEST(ResultTest, ErrorConstruction) {
    auto r = make_error(error_code::connection_refused);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.has_error());
}

TEST(ResultTest, ValueAccess) {
    auto r = make_result(42);
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrorAccess) {
    auto r = make_error(error_code::connection_refused);
    EXPECT_EQ(r.error(), error_code::connection_refused);
}

TEST(ResultTest, HasError) {
    auto ok_result = make_result(1);
    EXPECT_TRUE(ok_result.ok());
    EXPECT_FALSE(ok_result.has_error());

    auto err_result = make_error<int>(error_code::buffer_overflow);
    EXPECT_FALSE(err_result.ok());
    EXPECT_TRUE(err_result.has_error());
}

TEST(ResultTest, VoidSuccess) {
    result<void> r;
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.has_error());
}

TEST(ResultTest, VoidError) {
    result<void> r(error_code::operation_cancelled);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.has_error());
    EXPECT_EQ(r.error(), error_code::operation_cancelled);
}

TEST(ResultTest, MoveSemantics) {
    auto r1 = make_result(std::string("hello"));
    EXPECT_TRUE(r1.ok());
    EXPECT_EQ(r1.value(), "hello");

    auto r2 = std::move(r1);
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(r2.value(), "hello");
}

TEST(ResultTest, ValueOrDefault) {
    auto ok_result = make_result(42);
    EXPECT_EQ(ok_result.value_or(0), 42);

    auto err_result = make_error<int>(error_code::connection_refused);
    EXPECT_EQ(err_result.value_or(99), 99);
}

TEST(ResultTest, MapTransform) {
    auto r = make_result(21);
    auto mapped = r.map([](int v) { return v * 2; });
    EXPECT_TRUE(mapped.ok());
    EXPECT_EQ(mapped.value(), 42);
}

TEST(ResultTest, MapErrorPropagates) {
    auto r = make_error<int>(error_code::connection_refused);
    auto mapped = r.map([](int v) { return v * 2; });
    EXPECT_TRUE(mapped.has_error());
    EXPECT_EQ(mapped.error(), error_code::connection_refused);
}
