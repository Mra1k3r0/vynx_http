#include <gtest/gtest.h>
#include <vynx_http/byte_buffer.h>

#include <cstring>

using namespace vynx_http;

TEST(ByteBufferTest, DefaultConstruction) {
    byte_buffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.capacity(), byte_buffer::default_capacity);
}

TEST(ByteBufferTest, CustomCapacity) {
    byte_buffer buf(1024);
    EXPECT_EQ(buf.capacity(), 1024);
    EXPECT_TRUE(buf.empty());
}

TEST(ByteBufferTest, ConstructFromData) {
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    byte_buffer buf(data, sizeof(data));

    EXPECT_EQ(buf.size(), 3);
    EXPECT_FALSE(buf.empty());
}

TEST(ByteBufferTest, MoveConstruct) {
    byte_buffer original(1024);
    std::byte data = std::byte{0x01};
    original.write(&data, 1);

    byte_buffer moved(std::move(original));
    EXPECT_EQ(moved.size(), 1);
}

TEST(ByteBufferTest, MoveAssign) {
    byte_buffer original(1024);
    std::byte data = std::byte{0x01};
    original.write(&data, 1);

    byte_buffer target;
    target = std::move(original);
    EXPECT_EQ(target.size(), 1);
}

TEST(ByteBufferTest, WriteAndRead) {
    byte_buffer buf;
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    buf.write(data, sizeof(data));
    EXPECT_EQ(buf.size(), 3);

    std::byte read_buf[3];
    std::size_t bytes_read = buf.read(read_buf, sizeof(read_buf));
    EXPECT_EQ(bytes_read, 3);
    EXPECT_EQ(read_buf[0], std::byte{0x01});
    EXPECT_EQ(read_buf[1], std::byte{0x02});
    EXPECT_EQ(read_buf[2], std::byte{0x03});
}

TEST(ByteBufferTest, WriteFromBuffer) {
    byte_buffer source;
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}};
    source.write(data, sizeof(data));

    byte_buffer target;
    target.write(source);

    EXPECT_EQ(target.size(), 2);
}

TEST(ByteBufferTest, Peek) {
    byte_buffer buf;
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    buf.write(data, sizeof(data));

    std::byte peek_buf[2];
    std::size_t peeked = buf.peek(peek_buf, sizeof(peek_buf));
    EXPECT_EQ(peeked, 2);
    EXPECT_EQ(peek_buf[0], std::byte{0x01});
    EXPECT_EQ(peek_buf[1], std::byte{0x02});

    // Peek should not advance read position
    EXPECT_EQ(buf.size(), 3);
}

TEST(ByteBufferTest, Find) {
    byte_buffer buf;
    std::byte data[] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    buf.write(data, sizeof(data));

    EXPECT_EQ(buf.find(std::byte{0x03}), 2);
    EXPECT_EQ(buf.find(std::byte{0x06}), byte_buffer::npos);
}

TEST(ByteBufferTest, FindSubstring) {
    byte_buffer buf;
    std::byte data[] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    buf.write(data, sizeof(data));

    std::byte pattern[] = {std::byte{0x02}, std::byte{0x03}};
    EXPECT_EQ(buf.find(pattern, sizeof(pattern)), 1);
}

TEST(ByteBufferTest, Compact) {
    byte_buffer buf;
    std::byte data[] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    buf.write(data, sizeof(data));

    // Read first 2 bytes
    std::byte read_buf[2];
    buf.read(read_buf, sizeof(read_buf));

    EXPECT_EQ(buf.size(), 3);

    buf.compact();
    EXPECT_EQ(buf.size(), 3);

    // Verify remaining data is correct
    std::byte verify[3];
    std::size_t bytes_read = buf.read(verify, sizeof(verify));
    EXPECT_EQ(bytes_read, 3);
    EXPECT_EQ(verify[0], std::byte{0x03});
    EXPECT_EQ(verify[1], std::byte{0x04});
    EXPECT_EQ(verify[2], std::byte{0x05});
}

TEST(ByteBufferTest, Reset) {
    byte_buffer buf;
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}};
    buf.write(data, sizeof(data));

    EXPECT_EQ(buf.size(), 2);
    buf.reset();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0);
}

TEST(ByteBufferTest, EnsureCapacity) {
    byte_buffer buf(16);
    EXPECT_EQ(buf.capacity(), 16);

    buf.ensure_capacity(100);
    EXPECT_GE(buf.capacity(), 100);
}

TEST(ByteBufferTest, AdvanceRead) {
    byte_buffer buf;
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    buf.write(data, sizeof(data));

    buf.advance_read(2);
    EXPECT_EQ(buf.size(), 1);
    EXPECT_EQ(*buf.read_ptr(), std::byte{0x03});
}

TEST(ByteBufferTest, AdvanceWrite) {
    byte_buffer buf(16);
    buf.advance_write(5);
    EXPECT_EQ(buf.size(), 5);
    EXPECT_EQ(buf.available(), 11);
}
