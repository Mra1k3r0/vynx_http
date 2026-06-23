cmake_minimum_required(VERSION 3.16)

project(vynx_http
    VERSION 0.1.0
    DESCRIPTION "High-performance C++ HTTP client"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(VYNX_HTTP_BUILD_TESTS "Build tests" ON)
option(VYNX_HTTP_BUILD_EXAMPLES "Build examples" ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)

if(VYNX_HTTP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VYNX_HTTP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
