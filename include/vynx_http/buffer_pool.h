#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "byte_buffer.h"

namespace vynx_http {

// Pool of reusable byte buffers to reduce allocations
class buffer_pool {
   public:
    // Pool configuration
    struct config {
        std::size_t initial_size = 16;
        std::size_t max_size = 256;
        std::size_t buffer_capacity = 4096;
    };

    // Construct with configuration
    explicit buffer_pool(config cfg);

    // Default construct with default config
    buffer_pool();

    // Non-copyable
    buffer_pool(const buffer_pool&) = delete;
    buffer_pool& operator=(const buffer_pool&) = delete;

    // Acquire a buffer from the pool
    std::unique_ptr<byte_buffer> acquire() {
        std::lock_guard lock(mutex_);

        if (!pool_.empty()) {
            auto buffer = std::move(pool_.back());
            pool_.pop_back();
            buffer->reset();
            return buffer;
        }

        // Pool is empty, create new buffer
        return std::make_unique<byte_buffer>(config_.buffer_capacity);
    }

    // Return a buffer to the pool
    void release(std::unique_ptr<byte_buffer> buffer) {
        if (!buffer)
            return;

        std::lock_guard lock(mutex_);

        // Only return to pool if under capacity
        if (pool_.size() < config_.max_size) {
            buffer->reset();
            pool_.push_back(std::move(buffer));
        }
        // Otherwise let it destruct
    }

    // Get current pool size
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return pool_.size();
    }

    // Get pool configuration
    const config& get_config() const noexcept { return config_; }

    // Clear all buffers from pool
    void clear() {
        std::lock_guard lock(mutex_);
        pool_.clear();
    }

   private:
    config config_;
    std::vector<std::unique_ptr<byte_buffer>> pool_;
    mutable std::mutex mutex_;
};

// RAII guard for buffer acquisition
class buffer_guard {
   public:
    buffer_guard(buffer_pool& pool) : pool_(pool), buffer_(pool.acquire()) {}

    ~buffer_guard() {
        if (buffer_) {
            pool_.release(std::move(buffer_));
        }
    }

    // Non-copyable
    buffer_guard(const buffer_guard&) = delete;
    buffer_guard& operator=(const buffer_guard&) = delete;

    // Movable
    buffer_guard(buffer_guard&& other) noexcept :
        pool_(other.pool_), buffer_(std::move(other.buffer_)) {}

    buffer_guard& operator=(buffer_guard&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                pool_.release(std::move(buffer_));
            }
            // pool_ reference stays the same
            buffer_ = std::move(other.buffer_);
        }
        return *this;
    }

    // Access buffer
    byte_buffer* get() noexcept { return buffer_.get(); }
    const byte_buffer* get() const noexcept { return buffer_.get(); }

    byte_buffer* operator->() noexcept { return buffer_.get(); }
    const byte_buffer* operator->() const noexcept { return buffer_.get(); }

    byte_buffer& operator*() noexcept { return *buffer_; }
    const byte_buffer& operator*() const noexcept { return *buffer_; }

    // Release ownership
    std::unique_ptr<byte_buffer> release() { return std::move(buffer_); }

   private:
    buffer_pool& pool_;
    std::unique_ptr<byte_buffer> buffer_;
};

}  // namespace vynx_http
