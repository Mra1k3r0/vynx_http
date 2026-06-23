#include <gtest/gtest.h>
#include <vynx_http/http_client.h>
#include <vynx_http/http_parser.h>

#include <string>

using namespace vynx_http;

// Helper to make a byte_span from a string literal.
static byte_span make_span(const char* s) {
    return byte_span(reinterpret_cast<const std::byte*>(s), std::strlen(s));
}

static byte_span make_span(const char* s, std::size_t len) {
    return byte_span(reinterpret_cast<const std::byte*>(s), len);
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — basic status line
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, SimpleGetResponse) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& resp = parser.response();
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_EQ(resp.reason, "OK");
    EXPECT_TRUE(resp.is_success());
}

TEST(HttpParserTest, StatusLineWithoutReason) {
    const char* raw =
        "HTTP/1.1 204\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().status_code, 204);
}

TEST(HttpParserTest, Http10Response) {
    const char* raw =
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().status_code, 200);
}

TEST(HttpParserTest, InvalidVersion) {
    const char* raw = "HTTP/2.0 200 OK\r\n";
    http_parser parser;
    (void)parser.feed(make_span(raw));
    EXPECT_EQ(parser.state(), parser_state::error);
}

TEST(HttpParserTest, MissingStatusCode) {
    const char* raw = "HTTP/1.1\r\n";
    http_parser parser;
    (void)parser.feed(make_span(raw));
    EXPECT_EQ(parser.state(), parser_state::error);
}

TEST(HttpParserTest, Status404) {
    const char* raw =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().status_code, 404);
    EXPECT_EQ(parser.response().reason, "Not Found");
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — headers
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, HeadersParsed) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "X-Custom: value\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& headers = parser.response().headers;
    EXPECT_TRUE(headers.contains("Content-Type"));
    EXPECT_EQ(*headers.get("Content-Type"), "text/html");
    EXPECT_TRUE(headers.contains("X-Custom"));
    EXPECT_EQ(*headers.get("X-Custom"), "value");
}

TEST(HttpParserTest, HeaderWithColonInValue) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Location: http://example.com:8080/path\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());

    auto loc = parser.response().headers.get("Location");
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(*loc, "http://example.com:8080/path");
}

TEST(HttpParserTest, HeaderWithLeadingWhitespace) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "X-Value:   indented value\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());

    auto val = parser.response().headers.get("X-Value");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "indented value");
}

TEST(HttpParserTest, InvalidHeaderNoColon) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "BadHeader\r\n"
        "\r\n";

    http_parser parser;
    (void)parser.feed(make_span(raw));
    EXPECT_EQ(parser.state(), parser_state::error);
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — content-length body
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, ContentLengthBody) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& body = parser.response().body;
    EXPECT_EQ(body.size(), 13u);
    std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    EXPECT_EQ(body_str, "Hello, World!");
}

TEST(HttpParserTest, ContentLengthZero) {
    const char* raw =
        "HTTP/1.1 204 No Content\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().body.size(), 0u);
}

TEST(HttpParserTest, ContentLengthBodyLarge) {
    std::string body_data(10000, 'X');
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10000\r\n"
        "\r\n" +
        body_data;

    http_parser parser;
    auto result = parser.feed(make_span(raw.c_str()));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().body.size(), 10000u);
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — chunked transfer encoding
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, ChunkedBody) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\n"
        "Hello\r\n"
        "6\r\n"
        " World\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& body = parser.response().body;
    std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    EXPECT_EQ(body_str, "Hello World");
}

TEST(HttpParserTest, ChunkedSingleChunk) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "d\r\n"
        "Hello, World!\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& body = parser.response().body;
    std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    EXPECT_EQ(body_str, "Hello, World!");
}

TEST(HttpParserTest, ChunkedHexUppercase) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "A\r\n"
        "0123456789\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& body = parser.response().body;
    std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    EXPECT_EQ(body_str, "0123456789");
}

TEST(HttpParserTest, ChunkedEmptyBody) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().body.size(), 0u);
}

TEST(HttpParserTest, ChunkedWithExtension) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5;ext=value\r\n"
        "Hello\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    auto result = parser.feed(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    const auto& body = parser.response().body;
    std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    EXPECT_EQ(body_str, "Hello");
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — incremental feeding
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, IncrementalFeed) {
    http_parser parser;

    // Feed status line
    auto r1 = parser.feed(make_span("HTTP/1.1 200 OK\r\n"));
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(parser.state(), parser_state::headers);

    // Feed headers
    auto r2 = parser.feed(make_span("Content-Length: 5\r\n\r\n"));
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(parser.state(), parser_state::body_raw);

    // Feed body
    auto r3 = parser.feed(make_span("hello"));
    ASSERT_TRUE(r3.ok());
    EXPECT_EQ(parser.state(), parser_state::complete);

    EXPECT_EQ(parser.response().body.size(), 5u);
}

TEST(HttpParserTest, ByteAtATime) {
    const char* raw = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    http_parser parser;

    for (std::size_t i = 0; raw[i] != '\0'; ++i) {
        auto r = parser.feed(make_span(&raw[i], 1));
        ASSERT_TRUE(r.ok());
    }

    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().body.size(), 3u);
}

// ════════════════════════════════════════════════════════════════════════
// http_parser — reset
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, Reset) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "abc";

    http_parser parser;
    parser.feed(make_span(raw));
    EXPECT_EQ(parser.state(), parser_state::complete);

    parser.reset();
    EXPECT_EQ(parser.state(), parser_state::status_line);
    EXPECT_EQ(parser.response().status_code, 0);
    EXPECT_TRUE(parser.response().headers.empty());
    EXPECT_EQ(parser.response().body.size(), 0u);

    // Parse again
    parser.feed(make_span(raw));
    EXPECT_EQ(parser.state(), parser_state::complete);
    EXPECT_EQ(parser.response().status_code, 200);
}

// ════════════════════════════════════════════════════════════════════════
// parse_response free function
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, ParseResponseFreeFunction) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "test";

    auto result = parse_response(make_span(raw));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().status_code, 200);
    EXPECT_EQ(result.value().body.size(), 4u);
}

TEST(HttpParserTest, ParseResponseIncomplete) {
    const char* raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel";
    auto result = parse_response(make_span(raw));
    EXPECT_FALSE(result.ok());
}

// ════════════════════════════════════════════════════════════════════════
// http_response convenience
// ════════════════════════════════════════════════════════════════════════

TEST(HttpParserTest, ResponseIsChunked) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n";

    http_parser parser;
    parser.feed(make_span(raw));
    EXPECT_TRUE(parser.response().is_chunked());
}

TEST(HttpParserTest, ResponseContentLength) {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 42\r\n"
        "\r\n";

    http_parser parser;
    parser.feed(make_span(raw));
    auto cl = parser.response().content_length();
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, 42u);
}

// ════════════════════════════════════════════════════════════════════════
// cookie_jar
// ════════════════════════════════════════════════════════════════════════

TEST(CookieJarTest, InitiallyEmpty) {
    cookie_jar jar;
    EXPECT_EQ(jar.size(), 0u);
}

TEST(CookieJarTest, SetCookieEntry) {
    cookie_jar jar;
    cookie_entry entry;
    entry.name = "session";
    entry.value = "abc123";
    entry.domain = "example.com";
    entry.path = "/";
    jar.set_cookie(entry);
    EXPECT_EQ(jar.size(), 1u);
}

TEST(CookieJarTest, SetCookieReplacesSameDomainPath) {
    cookie_jar jar;
    cookie_entry e1;
    e1.name = "session";
    e1.value = "old";
    e1.domain = "example.com";
    e1.path = "/";
    jar.set_cookie(e1);

    cookie_entry e2;
    e2.name = "session";
    e2.value = "new";
    e2.domain = "example.com";
    e2.path = "/";
    jar.set_cookie(e2);

    EXPECT_EQ(jar.size(), 1u);
    auto header = jar.build_cookie_header("example.com");
    EXPECT_EQ(header, "session=new");
}

TEST(CookieJarTest, BuildCookieHeaderMatchingDomain) {
    cookie_jar jar;
    cookie_entry entry;
    entry.name = "id";
    entry.value = "42";
    entry.domain = "example.com";
    entry.path = "/";
    jar.set_cookie(entry);

    EXPECT_EQ(jar.build_cookie_header("example.com"), "id=42");
    EXPECT_EQ(jar.build_cookie_header("sub.example.com"), "id=42");
    EXPECT_EQ(jar.build_cookie_header("other.com"), "");
}

TEST(CookieJarTest, BuildCookieHeaderMatchingPath) {
    cookie_jar jar;
    cookie_entry entry;
    entry.name = "id";
    entry.value = "42";
    entry.domain = "example.com";
    entry.path = "/api";
    jar.set_cookie(entry);

    EXPECT_EQ(jar.build_cookie_header("example.com", "/api"), "id=42");
    EXPECT_EQ(jar.build_cookie_header("example.com", "/api/sub"), "id=42");
    EXPECT_EQ(jar.build_cookie_header("example.com", "/other"), "");
}

TEST(CookieJarTest, BuildCookieHeaderMultiple) {
    cookie_jar jar;
    cookie_entry e1;
    e1.name = "a";
    e1.value = "1";
    e1.domain = "example.com";
    e1.path = "/";
    jar.set_cookie(e1);

    cookie_entry e2;
    e2.name = "b";
    e2.value = "2";
    e2.domain = "example.com";
    e2.path = "/";
    jar.set_cookie(e2);

    auto header = jar.build_cookie_header("example.com");
    // Order depends on insertion order.
    EXPECT_TRUE(header == "a=1; b=2" || header == "b=2; a=1");
}

TEST(CookieJarTest, SetCookieFromString) {
    cookie_jar jar;
    jar.set_cookie("session=abc123; Domain=example.com; Path=/; Secure", "example.com");
    EXPECT_EQ(jar.size(), 1u);

    auto header = jar.build_cookie_header("example.com");
    EXPECT_EQ(header, "session=abc123");
}

TEST(CookieJarTest, SetCookieDefaultsDomain) {
    cookie_jar jar;
    jar.set_cookie("token=xyz", "myhost.com");
    EXPECT_EQ(jar.size(), 1u);

    auto header = jar.build_cookie_header("myhost.com");
    EXPECT_EQ(header, "token=xyz");
}

TEST(CookieJarTest, Clear) {
    cookie_jar jar;
    jar.set_cookie("a=1", "example.com");
    jar.set_cookie("b=2", "example.com");
    EXPECT_EQ(jar.size(), 2u);

    jar.clear();
    EXPECT_EQ(jar.size(), 0u);
}
