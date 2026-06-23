add_library(vynx_http
    # Phase 1: Foundation
    error_code.cpp
    logger.cpp
    buffer_pool.cpp
    # Phase 2: Core Networking
    tcp_socket.cpp
    platform.cpp
    event_loop.cpp
    dns_resolver.cpp
    # Phase 2: Completion
    timeout_manager.cpp
    signal_handler.cpp
    # Phase 3: HTTP Types
    http_method.cpp
    http_headers.cpp
    http_request.cpp
    http_response.cpp
    http_parser.cpp
    http_client.cpp
    # TLS support
    tls_context.cpp
)

target_include_directories(vynx_http
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(vynx_http PUBLIC cxx_std_20)

# OpenSSL (TLS support)
# On CI/other machines, pass -DOPENSSL_ROOT_DIR=<path> to cmake.
# Locally on MinGW, defaults to third_party/openssl-mingw/openssl-out.
if(NOT OPENSSL_ROOT_DIR AND EXISTS ${CMAKE_SOURCE_DIR}/third_party/openssl-mingw/openssl-out)
    set(OPENSSL_ROOT_DIR ${CMAKE_SOURCE_DIR}/third_party/openssl-mingw/openssl-out)
endif()
find_package(OpenSSL REQUIRED)
target_link_libraries(vynx_http PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# Platform-specific settings
if(WIN32)
    # OpenSSL static on Windows needs these system libraries.
    target_link_libraries(vynx_http PUBLIC ws2_32 crypt32 advapi32 bcrypt normaliz)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(vynx_http PRIVATE pthread)
endif()
