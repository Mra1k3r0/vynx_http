#include <gtest/gtest.h>
#include <vynx_http/http_headers.h>
#include <vynx_http/http_method.h>
#include <vynx_http/http_request.h>
#include <vynx_http/http_response.h>

using namespace vynx_http;

// ════════════════════════════════════════════════════════════════════════
// http_method
// ════════════════════════════════════════════════════════════════════════

TEST(HttpMethodTest, ToStringAll) {
    EXPECT_EQ(to_string(http_method::get), "GET");
    EXPECT_EQ(to_string(http_method::post), "POST");
    EXPECT_EQ(to_string(http_method::put), "PUT");
    EXPECT_EQ(to_string(http_method::patch), "PATCH");
    EXPECT_EQ(to_string(http_method::del), "DELETE");
    EXPECT_EQ(to_string(http_method::head), "HEAD");
    EXPECT_EQ(to_string(http_method::options), "OPTIONS");
    EXPECT_EQ(to_string(http_method::trace), "TRACE");
    EXPECT_EQ(to_string(http_method::connect), "CONNECT");
}

TEST(HttpMethodTest, ParseGet) {
    auto result = parse_http_method("GET");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::get);
}

TEST(HttpMethodTest, ParsePost) {
    auto result = parse_http_method("POST");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::post);
}

TEST(HttpMethodTest, ParsePut) {
    auto result = parse_http_method("PUT");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::put);
}

TEST(HttpMethodTest, ParsePatch) {
    auto result = parse_http_method("PATCH");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::patch);
}

TEST(HttpMethodTest, ParseDelete) {
    auto result = parse_http_method("DELETE");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::del);
}

TEST(HttpMethodTest, ParseHead) {
    auto result = parse_http_method("HEAD");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::head);
}

TEST(HttpMethodTest, ParseOptions) {
    auto result = parse_http_method("OPTIONS");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::options);
}

TEST(HttpMethodTest, ParseTrace) {
    auto result = parse_http_method("TRACE");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::trace);
}

TEST(HttpMethodTest, ParseConnect) {
    auto result = parse_http_method("CONNECT");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::connect);
}

TEST(HttpMethodTest, ParseCaseInsensitive) {
    auto result = parse_http_method("get");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), http_method::get);

    auto result2 = parse_http_method("Post");
    ASSERT_TRUE(result2.ok());
    EXPECT_EQ(result2.value(), http_method::post);

    auto result3 = parse_http_method("delete");
    ASSERT_TRUE(result3.ok());
    EXPECT_EQ(result3.value(), http_method::del);
}

TEST(HttpMethodTest, ParseInvalid) {
    auto result = parse_http_method("INVALID");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), error_code::http_invalid_method);
}

TEST(HttpMethodTest, ParseEmpty) {
    auto result = parse_http_method("");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), error_code::http_invalid_method);
}

// ════════════════════════════════════════════════════════════════════════
// http_headers
// ════════════════════════════════════════════════════════════════════════

class HttpHeadersTest : public ::testing::Test {
   protected:
    http_headers headers_;
};

TEST_F(HttpHeadersTest, InitiallyEmpty) {
    EXPECT_TRUE(headers_.empty());
    EXPECT_EQ(headers_.size(), 0u);
}

TEST_F(HttpHeadersTest, AddAndGetSize) {
    headers_.add("Content-Type", "text/html");
    EXPECT_EQ(headers_.size(), 1u);
    EXPECT_FALSE(headers_.empty());

    headers_.add("Content-Length", "42");
    EXPECT_EQ(headers_.size(), 2u);
}

TEST_F(HttpHeadersTest, AddDuplicates) {
    headers_.add("Set-Cookie", "a=1");
    headers_.add("Set-Cookie", "b=2");
    EXPECT_EQ(headers_.size(), 2u);
}

TEST_F(HttpHeadersTest, SetReplacesExisting) {
    headers_.add("Content-Type", "text/html");
    headers_.set("Content-Type", "application/json");
    EXPECT_EQ(headers_.size(), 1u);
    auto val = headers_.get("Content-Type");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "application/json");
}

TEST_F(HttpHeadersTest, SetAddsNew) {
    headers_.set("Content-Type", "text/html");
    EXPECT_EQ(headers_.size(), 1u);
}

TEST_F(HttpHeadersTest, GetCaseInsensitive) {
    headers_.add("Content-Type", "text/html");
    EXPECT_TRUE(headers_.get("content-type").has_value());
    EXPECT_TRUE(headers_.get("CONTENT-TYPE").has_value());
    EXPECT_TRUE(headers_.get("Content-Type").has_value());
    EXPECT_EQ(*headers_.get("content-type"), "text/html");
}

TEST_F(HttpHeadersTest, GetMissing) {
    EXPECT_FALSE(headers_.get("Content-Type").has_value());
}

TEST_F(HttpHeadersTest, ContainsCaseInsensitive) {
    headers_.add("X-Custom", "value");
    EXPECT_TRUE(headers_.contains("x-custom"));
    EXPECT_TRUE(headers_.contains("X-CUSTOM"));
    EXPECT_FALSE(headers_.contains("X-Other"));
}

TEST_F(HttpHeadersTest, RemoveCaseInsensitive) {
    headers_.add("X-Remove", "value");
    headers_.add("X-Keep", "other");
    EXPECT_EQ(headers_.size(), 2u);

    headers_.remove("x-remove");
    EXPECT_EQ(headers_.size(), 1u);
    EXPECT_FALSE(headers_.contains("X-Remove"));
    EXPECT_TRUE(headers_.contains("X-Keep"));
}

TEST_F(HttpHeadersTest, RemoveNonexistent) {
    headers_.add("X-Keep", "other");
    headers_.remove("X-NotExist");
    EXPECT_EQ(headers_.size(), 1u);
}

TEST_F(HttpHeadersTest, Clear) {
    headers_.add("A", "1");
    headers_.add("B", "2");
    headers_.clear();
    EXPECT_TRUE(headers_.empty());
}

TEST_F(HttpHeadersTest, FieldsAccess) {
    headers_.add("A", "1");
    const auto& fields = headers_.fields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].name, "A");
    EXPECT_EQ(fields[0].value, "1");
}

TEST_F(HttpHeadersTest, HostDefault) {
    EXPECT_EQ(headers_.host("fallback"), "fallback");
}

TEST_F(HttpHeadersTest, HostFromHeader) {
    headers_.add("Host", "example.com");
    EXPECT_EQ(headers_.host("fallback"), "example.com");
}

TEST_F(HttpHeadersTest, ContentLengthPresent) {
    headers_.add("Content-Length", "12345");
    auto cl = headers_.content_length();
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, 12345u);
}

TEST_F(HttpHeadersTest, ContentLengthAbsent) {
    EXPECT_FALSE(headers_.content_length().has_value());
}

TEST_F(HttpHeadersTest, ContentLengthInvalid) {
    headers_.add("Content-Length", "not-a-number");
    EXPECT_FALSE(headers_.content_length().has_value());
}

TEST_F(HttpHeadersTest, IsChunkedTrue) {
    headers_.add("Transfer-Encoding", "chunked");
    EXPECT_TRUE(headers_.is_chunked());
}

TEST_F(HttpHeadersTest, IsChunkedCaseInsensitive) {
    headers_.add("Transfer-Encoding", "Chunked");
    EXPECT_TRUE(headers_.is_chunked());

    headers_.set("Transfer-Encoding", "CHUNKED");
    EXPECT_TRUE(headers_.is_chunked());
}

TEST_F(HttpHeadersTest, IsChunkedFalse) {
    EXPECT_FALSE(headers_.is_chunked());

    headers_.add("Transfer-Encoding", "identity");
    EXPECT_FALSE(headers_.is_chunked());
}

TEST_F(HttpHeadersTest, ConnectionKeepAliveDefault) {
    // HTTP/1.1 defaults to keep-alive.
    EXPECT_TRUE(headers_.connection_keep_alive());
}

TEST_F(HttpHeadersTest, ConnectionKeepAliveExplicit) {
    headers_.add("Connection", "keep-alive");
    EXPECT_TRUE(headers_.connection_keep_alive());
}

TEST_F(HttpHeadersTest, ConnectionClose) {
    headers_.add("Connection", "close");
    EXPECT_FALSE(headers_.connection_keep_alive());
}

TEST_F(HttpHeadersTest, ConnectionCloseCaseInsensitive) {
    headers_.add("Connection", "Close");
    EXPECT_FALSE(headers_.connection_keep_alive());
}

// ════════════════════════════════════════════════════════════════════════
// http_request
// ════════════════════════════════════════════════════════════════════════

TEST(HttpRequestTest, DefaultMethod) {
    http_request req;
    EXPECT_EQ(req.method, http_method::get);
}

TEST(HttpRequestTest, RequestLine) {
    http_request req;
    req.method = http_method::get;
    req.uri = "/index.html";
    auto line = req.request_line();
    EXPECT_EQ(line, "GET /index.html HTTP/1.1\r\n");
}

TEST(HttpRequestTest, RequestLinePost) {
    http_request req;
    req.method = http_method::post;
    req.uri = "/api/data";
    auto line = req.request_line();
    EXPECT_EQ(line, "POST /api/data HTTP/1.1\r\n");
}

TEST(HttpRequestTest, SerializeMinimal) {
    http_request req;
    req.method = http_method::get;
    req.uri = "/";
    req.host = "example.com";
    req.headers.set("Host", "example.com");

    auto buf = req.serialize();
    std::string raw(reinterpret_cast<const char*>(buf.data()), buf.size());

    EXPECT_TRUE(raw.find("GET / HTTP/1.1\r\n") != std::string::npos);
    EXPECT_TRUE(raw.find("Host: example.com\r\n") != std::string::npos);
    EXPECT_TRUE(raw.find("\r\n\r\n") != std::string::npos);
}

TEST(HttpRequestTest, SerializeWithBody) {
    http_request req;
    req.method = http_method::post;
    req.uri = "/submit";
    req.host = "example.com";
    req.headers.set("Host", "example.com");
    req.headers.set("Content-Type", "text/plain");
    req.headers.set("Content-Length", "5");

    std::string body_data = "hello";
    req.body.write(reinterpret_cast<const std::byte*>(body_data.data()), body_data.size());

    auto buf = req.serialize();
    std::string raw(reinterpret_cast<const char*>(buf.data()), buf.size());

    EXPECT_TRUE(raw.find("POST /submit HTTP/1.1\r\n") != std::string::npos);
    EXPECT_TRUE(raw.find("Content-Type: text/plain\r\n") != std::string::npos);
    EXPECT_TRUE(raw.find("Content-Length: 5\r\n") != std::string::npos);
    EXPECT_TRUE(raw.find("hello") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════
// http_response
// ════════════════════════════════════════════════════════════════════════

TEST(HttpResponseTest, DefaultStatusCode) {
    http_response resp;
    EXPECT_EQ(resp.status_code, 0);
}

TEST(HttpResponseTest, IsSuccess) {
    http_response resp;
    resp.status_code = 200;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 201;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 204;
    EXPECT_TRUE(resp.is_success());
}

TEST(HttpResponseTest, IsNotSuccess) {
    http_response resp;
    resp.status_code = 301;
    EXPECT_FALSE(resp.is_success());

    resp.status_code = 404;
    EXPECT_FALSE(resp.is_success());

    resp.status_code = 500;
    EXPECT_FALSE(resp.is_success());
}

TEST(HttpResponseTest, IsRedirect) {
    http_response resp;
    resp.status_code = 301;
    EXPECT_TRUE(resp.is_redirect());

    resp.status_code = 302;
    EXPECT_TRUE(resp.is_redirect());

    resp.status_code = 304;
    EXPECT_TRUE(resp.is_redirect());
}

TEST(HttpResponseTest, IsNotRedirect) {
    http_response resp;
    resp.status_code = 200;
    EXPECT_FALSE(resp.is_redirect());

    resp.status_code = 404;
    EXPECT_FALSE(resp.is_redirect());
}

TEST(HttpResponseTest, StatusEnum) {
    http_response resp;
    resp.status_code = 200;
    EXPECT_EQ(resp.status(), http_status::ok);

    resp.status_code = 404;
    EXPECT_EQ(resp.status(), http_status::not_found);

    resp.status_code = 500;
    EXPECT_EQ(resp.status(), http_status::internal_server_error);
}

TEST(ReasonPhraseTest, CommonCodes) {
    EXPECT_EQ(reason_phrase(http_status::ok), "OK");
    EXPECT_EQ(reason_phrase(http_status::created), "Created");
    EXPECT_EQ(reason_phrase(http_status::no_content), "No Content");
    EXPECT_EQ(reason_phrase(http_status::moved_permanently), "Moved Permanently");
    EXPECT_EQ(reason_phrase(http_status::found), "Found");
    EXPECT_EQ(reason_phrase(http_status::not_modified), "Not Modified");
    EXPECT_EQ(reason_phrase(http_status::bad_request), "Bad Request");
    EXPECT_EQ(reason_phrase(http_status::unauthorized), "Unauthorized");
    EXPECT_EQ(reason_phrase(http_status::forbidden), "Forbidden");
    EXPECT_EQ(reason_phrase(http_status::not_found), "Not Found");
    EXPECT_EQ(reason_phrase(http_status::method_not_allowed), "Method Not Allowed");
    EXPECT_EQ(reason_phrase(http_status::request_timeout), "Request Timeout");
    EXPECT_EQ(reason_phrase(http_status::too_many_requests), "Too Many Requests");
    EXPECT_EQ(reason_phrase(http_status::internal_server_error), "Internal Server Error");
    EXPECT_EQ(reason_phrase(http_status::bad_gateway), "Bad Gateway");
    EXPECT_EQ(reason_phrase(http_status::service_unavailable), "Service Unavailable");
    EXPECT_EQ(reason_phrase(http_status::gateway_timeout), "Gateway Timeout");
}

TEST(ReasonPhraseTest, UnknownCode) {
    EXPECT_EQ(reason_phrase(static_cast<http_status>(999)), "Unknown");
}
