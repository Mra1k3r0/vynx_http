#include <gtest/gtest.h>
#include <vynx_http/buffer_pool.h>

#include <thread>
#include <vector>

using namespace vynx_http;

TEST(BufferPoolTest, DefaultConstruction) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);  // Pre-allocated
    EXPECT_EQ(pool.get_config().buffer_capacity, 4096);
}

TEST(BufferPoolTest, CustomConfig) {
    buffer_pool::config cfg;
    cfg.initial_size = 8;
    cfg.max_size = 32;
    cfg.buffer_capacity = 2048;

    buffer_pool pool(cfg);
    EXPECT_EQ(pool.size(), 8);
    EXPECT_EQ(pool.get_config().buffer_capacity, 2048);
    EXPECT_EQ(pool.get_config().max_size, 32);
}

TEST(BufferPoolTest, AcquireAndRelease) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    auto buffer = pool.acquire();
    EXPECT_NE(buffer, nullptr);
    EXPECT_EQ(pool.size(), 15);

    pool.release(std::move(buffer));
    EXPECT_EQ(pool.size(), 16);
}

TEST(BufferPoolTest, AcquireFromEmptyPool) {
    buffer_pool::config cfg;
    cfg.initial_size = 0;

    buffer_pool pool(cfg);
    EXPECT_EQ(pool.size(), 0);

    auto buffer = pool.acquire();
    EXPECT_NE(buffer, nullptr);
    EXPECT_EQ(pool.size(), 0);
}

TEST(BufferPoolTest, ReleaseNullptr) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    pool.release(nullptr);
    EXPECT_EQ(pool.size(), 16);
}

TEST(BufferPoolTest, ExceedMaxSize) {
    buffer_pool::config cfg;
    cfg.initial_size = 2;
    cfg.max_size = 2;

    buffer_pool pool(cfg);
    EXPECT_EQ(pool.size(), 2);

    auto buf1 = pool.acquire();
    auto buf2 = pool.acquire();
    auto buf3 = pool.acquire();

    pool.release(std::move(buf1));
    pool.release(std::move(buf2));
    pool.release(std::move(buf3));

    EXPECT_EQ(pool.size(), 2);  // Only 2 returned to pool
}

TEST(BufferPoolTest, Clear) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    pool.clear();
    EXPECT_EQ(pool.size(), 0);
}

TEST(BufferPoolTest, BufferReset) {
    buffer_pool pool;
    auto buffer = pool.acquire();

    // Write some data
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}};
    buffer->write(data, sizeof(data));
    EXPECT_EQ(buffer->size(), 2);

    pool.release(std::move(buffer));

    // Acquire again - should be reset
    auto buffer2 = pool.acquire();
    EXPECT_EQ(buffer2->size(), 0);
}

TEST(BufferPoolTest, BufferGuard) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    {
        buffer_guard guard(pool);
        EXPECT_NE(guard.get(), nullptr);
        EXPECT_EQ(pool.size(), 15);
    }

    EXPECT_EQ(pool.size(), 16);
}

TEST(BufferPoolTest, BufferGuardMove) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    {
        buffer_guard guard1(pool);
        EXPECT_EQ(pool.size(), 15);

        buffer_guard guard2(std::move(guard1));
        EXPECT_EQ(pool.size(), 15);
    }

    EXPECT_EQ(pool.size(), 16);
}

TEST(BufferPoolTest, BufferGuardRelease) {
    buffer_pool pool;
    EXPECT_EQ(pool.size(), 16);

    std::unique_ptr<byte_buffer> released;
    {
        buffer_guard guard(pool);
        released = guard.release();
    }

    // The buffer was released from the guard, not returned to pool
    // Pool size is 15 because we acquired one buffer
    EXPECT_EQ(pool.size(), 15);
    EXPECT_NE(released, nullptr);
}

TEST(BufferPoolTest, ThreadSafety) {
    buffer_pool pool;
    constexpr int num_threads = 10;
    constexpr int operations_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&pool]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                auto buffer = pool.acquire();
                // Write something to ensure buffer is usable
                std::byte data = std::byte{0x01};
                buffer->write(&data, 1);
                pool.release(std::move(buffer));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Pool should be intact
    EXPECT_GE(pool.size(), 0);
    EXPECT_LE(pool.size(), pool.get_config().max_size);
}
