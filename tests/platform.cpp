#include <gtest/gtest.h>
#include <vynx_http/platform.h>

using namespace vynx_http;

TEST(PlatformTest, CreatePoller) {
    auto result = poller::create();
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.value(), nullptr);
}

TEST(PlatformTest, EventFlags) {
    // Test operator|
    auto combined = event_type::read | event_type::write;
    EXPECT_TRUE(has_flag(combined, event_type::read));
    EXPECT_TRUE(has_flag(combined, event_type::write));
    EXPECT_FALSE(has_flag(combined, event_type::error));

    // Test single flag
    auto read_only = event_type::read;
    EXPECT_TRUE(has_flag(read_only, event_type::read));
    EXPECT_FALSE(has_flag(read_only, event_type::write));

    // Test operator&
    auto masked = combined & event_type::read;
    EXPECT_TRUE(has_flag(masked, event_type::read));
    EXPECT_FALSE(has_flag(masked, event_type::write));

    // Test none
    auto none = event_type::none;
    EXPECT_FALSE(has_flag(none, event_type::read));
    EXPECT_FALSE(has_flag(none, event_type::write));

    // Test all flags combined
    auto all = event_type::read | event_type::write | event_type::accept | event_type::connect |
               event_type::error | event_type::hangup;
    EXPECT_TRUE(has_flag(all, event_type::read));
    EXPECT_TRUE(has_flag(all, event_type::write));
    EXPECT_TRUE(has_flag(all, event_type::accept));
    EXPECT_TRUE(has_flag(all, event_type::connect));
    EXPECT_TRUE(has_flag(all, event_type::error));
    EXPECT_TRUE(has_flag(all, event_type::hangup));
}
