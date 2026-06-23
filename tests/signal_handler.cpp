#include <gtest/gtest.h>
#include <vynx_http/signal_handler.h>

using namespace vynx_http;

TEST(SignalHandlerTest, Create) {
    auto sh = signal_handler::create();
    ASSERT_TRUE(sh.ok());
}

TEST(SignalHandlerTest, DefaultState) {
    auto sh = signal_handler::create().value();
    EXPECT_FALSE(sh->has_received(signal_type::sigint));
    EXPECT_FALSE(sh->has_received(signal_type::sigterm));
    EXPECT_EQ(sh->signal_count(), 0u);
}

TEST(SignalHandlerTest, RegisterCallback) {
    auto sh = signal_handler::create().value();
    bool called = false;
    auto cb_result =
        sh->register_callback(signal_type::sigint, [&](signal_type /*s*/) { called = true; });
    EXPECT_TRUE(cb_result.ok());
    (void)called;
}

TEST(SignalHandlerTest, UnregisterCallback) {
    auto sh = signal_handler::create().value();
    sh->register_callback(signal_type::sigint, [](signal_type /*s*/) {});
    auto result = sh->unregister_callback(signal_type::sigint);
    EXPECT_TRUE(result.ok());
}

TEST(SignalHandlerTest, IgnoreAndRestore) {
    auto sh = signal_handler::create().value();
    auto ignore_result = sh->ignore(signal_type::sigpipe);
    EXPECT_TRUE(ignore_result.ok());
    auto restore_result = sh->restore_default(signal_type::sigpipe);
    EXPECT_TRUE(restore_result.ok());
}

TEST(SignalHandlerTest, LastSignalDefault) {
    auto sh = signal_handler::create().value();
    EXPECT_EQ(sh->last_signal(), signal_type::unknown);
}
