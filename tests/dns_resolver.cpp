#include <gtest/gtest.h>
#include <vynx_http/dns_resolver.h>

using namespace vynx_http;

TEST(DnsResolverTest, Create) {
    auto result = dns_resolver::create();
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.value(), nullptr);
}

TEST(DnsResolverTest, DefaultConfig) {
    auto result = dns_resolver::create();
    ASSERT_TRUE(result.ok());
    auto& resolver = *result.value();

    const auto& cfg = resolver.config();
    EXPECT_EQ(cfg.primary_server, "8.8.8.8");
    EXPECT_EQ(cfg.secondary_server, "8.8.4.4");
    EXPECT_EQ(cfg.port, 53);
    EXPECT_TRUE(cfg.enable_ipv6);
    EXPECT_TRUE(cfg.enable_canonical_names);
}

TEST(DnsResolverTest, ClearCache) {
    auto result = dns_resolver::create();
    ASSERT_TRUE(result.ok());
    auto& resolver = *result.value();

    // Should not crash
    resolver.clear_cache();
}

TEST(DnsResolverTest, CacheStats) {
    auto result = dns_resolver::create();
    ASSERT_TRUE(result.ok());
    auto& resolver = *result.value();

    auto stats = resolver.get_cache_stats();
    EXPECT_EQ(stats.size, 0u);
    EXPECT_EQ(stats.hits, 0u);
    EXPECT_EQ(stats.misses, 0u);
    EXPECT_EQ(stats.expired, 0u);
}

TEST(DnsResolverTest, ResolveLocalhost) {
    auto result = dns_resolver::create();
    ASSERT_TRUE(result.ok());
    auto& resolver = *result.value();

    auto resolve_result = resolver.resolve("localhost");
    // On systems with proper hosts file this should succeed
    // On systems without it, it should fail gracefully
    if (resolve_result.ok()) {
        auto addresses = resolve_result.value();
        EXPECT_FALSE(addresses.empty());
    }
}
