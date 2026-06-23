# Test configuration
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)

FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(vynx_http_tests
    error_code.cpp
    byte_span.cpp
    byte_buffer.cpp
    buffer_pool.cpp
    result.cpp
    tcp_socket.cpp
    event_loop.cpp
    dns_resolver.cpp
    platform.cpp
    timeout_manager.cpp
    signal_handler.cpp
    # Phase 3: HTTP types and parser
    http_types.cpp
    http_parser.cpp
)

target_link_libraries(vynx_http_tests
    PRIVATE
        vynx_http
        GTest::gtest_main
)

target_include_directories(vynx_http_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(vynx_http_tests)
