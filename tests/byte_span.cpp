#include <gtest/gtest.h>
#include <vynx_http/byte_span.h>

#include <array>
#include <vector>

using namespace vynx_http;

TEST(ByteSpanTest, DefaultConstruction) {
    byte_span span;
    EXPECT_TRUE(span.empty());
    EXPECT_EQ(span.size(), 0);
    EXPECT_EQ(span.data(), nullptr);
}

TEST(ByteSpanTest, ConstructFromPointerAndSize) {
    std::array<std::byte, 5> data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    byte_span span(data.data(), data.size());
    EXPECT_EQ(span.size(), 5);
    EXPECT_EQ(span.data(), data.data());
}

TEST(ByteSpanTest, ConstructFromArray) {
    std::array<std::byte, 3> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    byte_span span(data);
    EXPECT_EQ(span.size(), 3);
}

TEST(ByteSpanTest, ConstructFromVector) {
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    byte_span span(data);
    EXPECT_EQ(span.size(), 3);
}

TEST(ByteSpanTest, ConstructFromStringView) {
    std::string_view sv = "hello";
    byte_span span(sv);
    EXPECT_EQ(span.size(), 5);
}

TEST(ByteSpanTest, CopyConstruct) {
    std::array<std::byte, 2> data = {std::byte{0x01}, std::byte{0x02}};
    byte_span original(data);
    byte_span copy(original);

    EXPECT_EQ(copy.size(), original.size());
    EXPECT_EQ(copy.data(), original.data());
}

TEST(ByteSpanTest, Subspan) {
    std::array<std::byte, 5> data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    byte_span span(data);
    byte_span sub = span.subspan(1, 3);

    EXPECT_EQ(sub.size(), 3);
    EXPECT_EQ(sub.data(), data.data() + 1);
}

TEST(ByteSpanTest, SubspanToEnd) {
    std::array<std::byte, 5> data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    byte_span span(data);
    byte_span sub = span.subspan(2);

    EXPECT_EQ(sub.size(), 3);
    EXPECT_EQ(sub.data(), data.data() + 2);
}

TEST(ByteSpanTest, ToStringView) {
    std::string_view sv = "hello";
    byte_span span(sv);

    std::string_view result = span.to_string_view();
    EXPECT_EQ(result, sv);
}

TEST(ByteSpanTest, ToVector) {
    std::array<std::byte, 3> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    byte_span span(data);
    std::vector<std::byte> vec = span.to_vector();

    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], std::byte{0x01});
    EXPECT_EQ(vec[1], std::byte{0x02});
    EXPECT_EQ(vec[2], std::byte{0x03});
}

TEST(ByteSpanTest, Equality) {
    std::array<std::byte, 2> data1 = {std::byte{0x01}, std::byte{0x02}};
    std::array<std::byte, 2> data2 = {std::byte{0x01}, std::byte{0x02}};
    std::array<std::byte, 2> data3 = {std::byte{0x01}, std::byte{0x03}};

    byte_span span1(data1);
    byte_span span2(data2);
    byte_span span3(data3);

    EXPECT_EQ(span1, span2);
    EXPECT_NE(span1, span3);
}

TEST(ByteSpanTest, FindByte) {
    std::array<std::byte, 5> data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    byte_span span(data);

    EXPECT_EQ(span.find(std::byte{0x03}), 2);
    EXPECT_EQ(span.find(std::byte{0x06}), byte_span::npos);
}

TEST(ByteSpanTest, FindSubstring) {
    std::array<std::byte, 5> data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    byte_span span(data);
    std::array<std::byte, 2> pattern = {std::byte{0x02}, std::byte{0x03}};

    EXPECT_EQ(span.find(pattern), 1);
}

TEST(ByteSpanTest, IteratorSupport) {
    std::array<std::byte, 3> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    byte_span span(data);
    std::size_t count = 0;
    for (auto it = span.begin(); it != span.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 3);
}
