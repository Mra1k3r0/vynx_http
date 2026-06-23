#include "vynx_http/buffer_pool.h"

namespace vynx_http {

// Default construct with default config
buffer_pool::buffer_pool() : buffer_pool(config{}) {}

// Construct with configuration
buffer_pool::buffer_pool(config cfg) : config_(cfg), pool_(), mutex_() {
    // Pre-allocate initial buffers
    for (std::size_t i = 0; i < config_.initial_size; ++i) {
        pool_.push_back(std::make_unique<byte_buffer>(config_.buffer_capacity));
    }
}

}  // namespace vynx_http
