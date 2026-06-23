#include <gtest/gtest.h>
#include <vynx_http/event_loop.h>
#include <vynx_http/timeout_manager.h>

using namespace vynx_http;

TEST(TimeoutManagerTest, Create) {
    auto tm = timeout_manager::create();
    ASSERT_TRUE(tm.ok());
}

TEST(TimeoutManagerTest, DefaultStats) {
    auto tm = timeout_manager::create().value();
    auto s = tm->get_stats();
    EXPECT_EQ(s.timeouts_fired, 0u);
    EXPECT_EQ(s.timeouts_cancelled, 0u);
}

TEST(TimeoutManagerTest, ActiveCount) {
    auto tm = timeout_manager::create().value();
    EXPECT_EQ(tm->active_count(), 0u);
}

TEST(TimeoutManagerTest, StartAndCancel) {
    auto loop_result = event_loop::create();
    ASSERT_TRUE(loop_result.ok());
    auto loop = std::move(loop_result.value());

    auto tm = timeout_manager::create().value();
    tm->set_event_loop(loop.get());

    bool fired = false;
    auto start_result =
        tm->start_timeout(42,
                          timeout_type::connect,
                          std::chrono::milliseconds(100),
                          [&](native_handle /*h*/, timeout_type /*t*/) { fired = true; });
    ASSERT_TRUE(start_result.ok());
    EXPECT_TRUE(tm->is_active(42, timeout_type::connect));
    EXPECT_EQ(tm->active_count(), 1u);

    // Cancel before it fires
    auto cancel_result = tm->cancel_timeout(42, timeout_type::connect);
    ASSERT_TRUE(cancel_result.ok());
    EXPECT_FALSE(tm->is_active(42, timeout_type::connect));
    EXPECT_EQ(tm->active_count(), 0u);
    EXPECT_FALSE(fired);
}

TEST(TimeoutManagerTest, CancelAll) {
    auto loop_result = event_loop::create();
    ASSERT_TRUE(loop_result.ok());
    auto loop = std::move(loop_result.value());

    auto tm = timeout_manager::create().value();
    tm->set_event_loop(loop.get());

    tm->start_timeout(42,
                      timeout_type::connect,
                      std::chrono::milliseconds(5000),
                      [](native_handle /*h*/, timeout_type /*t*/) {});
    tm->start_timeout(42,
                      timeout_type::read,
                      std::chrono::milliseconds(5000),
                      [](native_handle /*h*/, timeout_type /*t*/) {});

    EXPECT_EQ(tm->active_count(), 2u);

    tm->cancel_all(42);
    EXPECT_EQ(tm->active_count(), 0u);
}

TEST(TimeoutManagerTest, ResetIdle) {
    auto loop_result = event_loop::create();
    ASSERT_TRUE(loop_result.ok());
    auto loop = std::move(loop_result.value());

    auto tm = timeout_manager::create().value();
    tm->set_event_loop(loop.get());

    // Start idle timeout
    auto start_result = tm->start_timeout(42,
                                          timeout_type::idle,
                                          std::chrono::milliseconds(5000),
                                          [](native_handle /*h*/, timeout_type /*t*/) {});
    ASSERT_TRUE(start_result.ok());
    EXPECT_TRUE(tm->is_active(42, timeout_type::idle));

    // Reset it (should cancel old, start new)
    auto reset_result = tm->reset_idle(42, std::chrono::milliseconds(5000));
    ASSERT_TRUE(reset_result.ok());
    EXPECT_TRUE(tm->is_active(42, timeout_type::idle));
    EXPECT_EQ(tm->active_count(), 1u);
}

TEST(TimeoutManagerTest, StartDuplicateReplaces) {
    auto loop_result = event_loop::create();
    ASSERT_TRUE(loop_result.ok());
    auto loop = std::move(loop_result.value());

    auto tm = timeout_manager::create().value();
    tm->set_event_loop(loop.get());

    int call_count = 0;

    // Start first timeout
    tm->start_timeout(42,
                      timeout_type::connect,
                      std::chrono::milliseconds(5000),
                      [&call_count](native_handle /*h*/, timeout_type /*t*/) { call_count++; });

    // Start another with same handle+type (should replace)
    tm->start_timeout(42,
                      timeout_type::connect,
                      std::chrono::milliseconds(5000),
                      [&call_count](native_handle /*h*/, timeout_type /*t*/) { call_count++; });

    // Only one should be active
    EXPECT_EQ(tm->active_count(), 1u);

    // Stats should show one was cancelled
    auto s = tm->get_stats();
    EXPECT_EQ(s.timeouts_cancelled, 1u);
}

TEST(TimeoutManagerTest, DifferentHandlesIndependent) {
    auto loop_result = event_loop::create();
    ASSERT_TRUE(loop_result.ok());
    auto loop = std::move(loop_result.value());

    auto tm = timeout_manager::create().value();
    tm->set_event_loop(loop.get());

    tm->start_timeout(1,
                      timeout_type::connect,
                      std::chrono::milliseconds(5000),
                      [](native_handle /*h*/, timeout_type /*t*/) {});
    tm->start_timeout(2,
                      timeout_type::connect,
                      std::chrono::milliseconds(5000),
                      [](native_handle /*h*/, timeout_type /*t*/) {});

    EXPECT_EQ(tm->active_count(), 2u);

    // Cancel only handle 1
    tm->cancel_timeout(1, timeout_type::connect);
    EXPECT_EQ(tm->active_count(), 1u);
    EXPECT_FALSE(tm->is_active(1, timeout_type::connect));
    EXPECT_TRUE(tm->is_active(2, timeout_type::connect));
}

TEST(TimeoutManagerTest, NoEventLoop) {
    auto tm = timeout_manager::create().value();

    // Without setting event loop, start_timeout should still store entry
    auto start_result = tm->start_timeout(42,
                                          timeout_type::connect,
                                          std::chrono::milliseconds(100),
                                          [](native_handle /*h*/, timeout_type /*t*/) {});
    ASSERT_TRUE(start_result.ok());
    EXPECT_TRUE(tm->is_active(42, timeout_type::connect));
    EXPECT_EQ(tm->active_count(), 1u);
}
