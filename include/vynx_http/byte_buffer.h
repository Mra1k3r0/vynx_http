#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace vynx_http {

// A resizable byte buffer with memory management
class byte_buffer {
   public:
    // Default capacity
    static constexpr std::size_t default_capacity = 4096;

    // Construct with specified capacity
    explicit byte_buffer(std::size_t capacity = default_capacity) :
        data_(capacity), read_pos_(0), write_pos_(0) {}

    // Construct from existing data
    byte_buffer(const std::byte* data, std::size_t size) :
        data_(data, data + size), read_pos_(0), write_pos_(size) {}

    // Move construct
    byte_buffer(byte_buffer&& other) noexcept :
        data_(std::move(other.data_)), read_pos_(other.read_pos_), write_pos_(other.write_pos_) {
        other.read_pos_ = 0;
        other.write_pos_ = 0;
    }

    // Move assign
    byte_buffer& operator=(byte_buffer&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            read_pos_ = other.read_pos_;
            write_pos_ = other.write_pos_;
            other.read_pos_ = 0;
            other.write_pos_ = 0;
        }
        return *this;
    }

    // Non-copyable
    byte_buffer(const byte_buffer&) = delete;
    byte_buffer& operator=(const byte_buffer&) = delete;

    // Data access
    [[nodiscard]] std::byte* data() noexcept { return data_.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return data_.data(); }

    // Capacity
    [[nodiscard]] std::size_t capacity() const noexcept { return data_.size(); }

    // Size (bytes written)
    [[nodiscard]] std::size_t size() const noexcept { return write_pos_ - read_pos_; }

    // Available space for writing
    [[nodiscard]] std::size_t available() const noexcept { return capacity() - write_pos_; }

    // Check if empty
    [[nodiscard]] bool empty() const noexcept { return read_pos_ == write_pos_; }

    // Get read pointer
    [[nodiscard]] const std::byte* read_ptr() const noexcept { return data_.data() + read_pos_; }

    // Get write pointer
    std::byte* write_ptr() noexcept { return data_.data() + write_pos_; }

    // Advance read position
    void advance_read(std::size_t bytes) noexcept {
        assert(read_pos_ + bytes <= write_pos_);
        read_pos_ += bytes;
    }

    // Advance write position
    void advance_write(std::size_t bytes) noexcept {
        assert(write_pos_ + bytes <= capacity());
        write_pos_ += bytes;
    }

    // Reset positions (keep allocated memory)
    void reset() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
    }

    // Clear all data
    void clear() noexcept { reset(); }

    // Ensure capacity
    void ensure_capacity(std::size_t additional) {
        if (additional > available()) {
            grow(additional);
        }
    }

    // Write data to buffer
    void write(const std::byte* data, std::size_t size) {
        ensure_capacity(size);
        std::memcpy(write_ptr(), data, size);
        write_pos_ += size;
    }

    // Write from another buffer
    void write(const byte_buffer& other) { write(other.read_ptr(), other.size()); }

    // Read data from buffer
    std::size_t read(std::byte* dest, std::size_t max_size) {
        std::size_t available_data = size();
        std::size_t to_read = std::min(max_size, available_data);
        std::memcpy(dest, read_ptr(), to_read);
        read_pos_ += to_read;
        return to_read;
    }

    // Peek at data without advancing read position
    std::size_t peek(std::byte* dest, std::size_t max_size) const {
        std::size_t available_data = size();
        std::size_t to_peek = std::min(max_size, available_data);
        std::memcpy(dest, read_ptr(), to_peek);
        return to_peek;
    }

    // Find byte in buffer
    [[nodiscard]] std::size_t find(std::byte value, std::size_t offset = 0) const {
        const std::byte* start = read_ptr() + offset;
        const std::byte* end = data_.data() + write_pos_;
        const std::byte* pos = std::find(start, end, value);
        if (pos != end) {
            return static_cast<std::size_t>(pos - read_ptr());
        }
        return npos;
    }

    // Find substring in buffer
    [[nodiscard]] std::size_t find(const std::byte* pattern,
                                   std::size_t pattern_size,
                                   std::size_t offset = 0) const {
        if (pattern_size == 0) {
            return offset;
        }
        std::size_t available_data = size();
        if (pattern_size > available_data) {
            return npos;
        }

        for (std::size_t i = offset; i <= available_data - pattern_size; ++i) {
            if (std::memcmp(read_ptr() + i, pattern, pattern_size) == 0) {
                return i;
            }
        }
        return npos;
    }

    // Compact buffer (move unread data to beginning)
    void compact() {
        if (read_pos_ == 0) {
            return;
        }
        std::size_t remaining = size();
        if (remaining > 0) {
            std::memmove(data_.data(), read_ptr(), remaining);
        }
        read_pos_ = 0;
        write_pos_ = remaining;
    }

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

   private:
    void grow(std::size_t additional) {
        std::size_t required = write_pos_ + additional;
        std::size_t new_capacity = capacity();
        while (new_capacity < required) {
            new_capacity *= 2;
        }
        std::vector<std::byte> new_data(new_capacity);
        std::size_t current_size = size();
        if (current_size > 0) {
            std::memcpy(new_data.data(), read_ptr(), current_size);
        }
        data_ = std::move(new_data);
        read_pos_ = 0;
        write_pos_ = current_size;
    }

    std::vector<std::byte> data_;
    std::size_t read_pos_;
    std::size_t write_pos_;
};

}  // namespace vynx_http
