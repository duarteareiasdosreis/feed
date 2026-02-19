cmake_minimum_required(VERSION 3.14)
project(feed VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Core library (shared between CLI, MCP server, and tests)
set(CORE_SOURCES
    src/storage.cpp
    src/github_client.cpp
    src/classifier.cpp
    src/search.cpp
    src/api.cpp
    src/commands.cpp
)

add_library(feed_core STATIC ${CORE_SOURCES})

target_link_libraries(feed_core
    PUBLIC
    CURL::libcurl
    SQLite::SQLite3
)

# CLI executable
add_executable(feed src/cli_main.cpp)

target_link_libraries(feed
    PRIVATE
    feed_core
)

# MCP wrapper (separate target for future use)
add_library(feed_mcp STATIC
    mcp/mcp_wrapper.cpp
)

target_include_directories(feed_mcp
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(feed_mcp
    PRIVATE
    SQLite::SQLite3
)

# Testing
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # Fetch Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Test executable
    add_executable(feed_tests
        tests/test_main.cpp
        tests/test_storage.cpp
        tests/test_classifier.cpp
        tests/test_search.cpp
        tests/test_api.cpp
        tests/mock_github_client.h
    )

    target_include_directories(feed_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/tests
    )

    target_link_libraries(feed_tests
        PRIVATE
        feed_core
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
    )

    include(GoogleTest)
    gtest_discover_tests(feed_tests)
endif()
