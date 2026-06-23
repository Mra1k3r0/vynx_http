#include <gtest/gtest.h>
#include <vynx_http/event_loop.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace vynx_http;

TEST(EventLoopTest, Create) {
    auto result = event_loop::create();
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.value(), nullptr);
}

TEST(EventLoopTest, DefaultStats) {
    auto result = event_loop::create();
    ASSERT_TRUE(result.ok());
    auto& loop = *result.value();

    const auto& st = loop.get_stats();
    EXPECT_EQ(st.events_processed, 0u);
    EXPECT_EQ(st.timers_fired, 0u);
    EXPECT_EQ(st.tasks_executed, 0u);
}

TEST(EventLoopTest, Post) {
    auto result = event_loop::create();
    ASSERT_TRUE(result.ok());
    auto& loop = *result.value();

    std::atomic<bool> executed{false};
    loop.post([&executed]() { executed = true; });

    auto run_result = loop.run_once(0);
    EXPECT_TRUE(run_result.ok());
    EXPECT_TRUE(executed.load());
}

TEST(EventLoopTest, ScheduleTimer) {
    auto result = event_loop::create();
    ASSERT_TRUE(result.ok());
    auto& loop = *result.value();

    std::atomic<bool> fired{false};
    auto id =
        loop.schedule_timer(std::chrono::milliseconds(10), [&fired](timer_id) { fired = true; });

    EXPECT_NE(id, invalid_timer_id);

    // Run a few iterations to let the timer fire
    for (int i = 0; i < 20; ++i) {
        loop.run_once(50);
        if (fired.load())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(fired.load());
}

TEST(EventLoopTest, CancelTimer) {
    auto result = event_loop::create();
    ASSERT_TRUE(result.ok());
    auto& loop = *result.value();

    std::atomic<bool> fired{false};
    auto id =
        loop.schedule_timer(std::chrono::milliseconds(100), [&fired](timer_id) { fired = true; });

    // Cancel before it fires
    auto cancel_result = loop.cancel_timer(id);
    EXPECT_TRUE(cancel_result.ok());

    // Run once with a short timeout - timer should not fire
    loop.run_once(10);
    // Small sleep to give it a moment
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(fired.load());
}
