#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace vynx_http {

// Non-owning view of contiguous bytes
class byte_span {
   public:
    using value_type = std::byte;
    using pointer = const std::byte*;
    using reference = const std::byte&;
    using iterator = const std::byte*;
    using const_iterator = const std::byte*;
    using size_type = std::size_t;

    // Default construct (empty span)
    constexpr byte_span() noexcept : data_(nullptr), size_(0) {}

    // Construct from pointer and size
    constexpr byte_span(const std::byte* data, size_type size) noexcept :
        data_(data), size_(size) {}

    // Construct from array
    template <std::size_t N>
    constexpr byte_span(const std::array<std::byte, N>& arr) noexcept :
        data_(arr.data()), size_(N) {}

    // Construct from vector
    explicit byte_span(const std::vector<std::byte>& vec) noexcept :
        data_(vec.data()), size_(vec.size()) {}

    // Construct from string_view (treats chars as bytes)
    explicit byte_span(std::string_view sv) noexcept :
        data_(reinterpret_cast<const std::byte*>(sv.data())), size_(sv.size()) {}

    // Copy construct
    constexpr byte_span(const byte_span&) noexcept = default;

    // Copy assign
    constexpr byte_span& operator=(const byte_span&) noexcept = default;

    // Data access
    [[nodiscard]] constexpr const std::byte* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    // Element access
    constexpr const std::byte& operator[](size_type pos) const noexcept { return data_[pos]; }

    // Iterator support
    [[nodiscard]] constexpr const std::byte* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr const std::byte* end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr const std::byte* cbegin() const noexcept { return data_; }
    [[nodiscard]] constexpr const std::byte* cend() const noexcept { return data_ + size_; }

    // Subspan operations
    [[nodiscard]] constexpr byte_span subspan(size_type offset, size_type count) const noexcept {
        return byte_span(data_ + offset, count);
    }

    [[nodiscard]] constexpr byte_span subspan(size_type offset) const noexcept {
        return byte_span(data_ + offset, size_ - offset);
    }

    // Convert to string_view
    [[nodiscard]] std::string_view to_string_view() const noexcept {
        return std::string_view(reinterpret_cast<const char*>(data_), size_);
    }

    // Convert to vector
    [[nodiscard]] std::vector<std::byte> to_vector() const {
        return std::vector<std::byte>(data_, data_ + size_);
    }

    // Compare
    constexpr bool operator==(const byte_span& other) const noexcept {
        if (size_ != other.size_)
            return false;
        return std::memcmp(data_, other.data_, size_) == 0;
    }

    constexpr bool operator!=(const byte_span& other) const noexcept { return !(*this == other); }

    // Find byte
    [[nodiscard]] constexpr size_type find(std::byte value, size_type pos = 0) const noexcept {
        for (size_type i = pos; i < size_; ++i) {
            if (data_[i] == value)
                return i;
        }
        return npos;
    }

    // Find substring
    [[nodiscard]] constexpr size_type find(byte_span pattern, size_type pos = 0) const noexcept {
        if (pattern.size_ > size_)
            return npos;
        for (size_type i = pos; i <= size_ - pattern.size_; ++i) {
            if (std::memcmp(data_ + i, pattern.data_, pattern.size_) == 0) {
                return i;
            }
        }
        return npos;
    }

    static constexpr size_type npos = static_cast<size_type>(-1);

   private:
    const std::byte* data_;
    size_type size_;
};

// Helper to create byte_span from literals
inline byte_span operator""_bs(const char* str, std::size_t len) noexcept {
    return byte_span(reinterpret_cast<const std::byte*>(str), len);
}

}  // namespace vynx_http
