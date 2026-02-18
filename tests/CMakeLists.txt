project(uvrpc_tests VERSION 0.1.0 LANGUAGES CXX CXX)
# UVRPC Unit Tests CMakeLists.txt

cmake_minimum_required(VERSION 3.15)

# Enable testing
enable_testing()

# GTest paths (built from submodule)
set(GTEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../deps/gtest)
set(GTEST_INCLUDE_DIR ${GTEST_ROOT}/googletest/include)
set(GTEST_LIBRARY_DIR ${GTEST_ROOT}/build/lib)

# Find GTest
set(GTEST_LIBRARY ${GTEST_LIBRARY_DIR}/libgtest.a)
set(GTEST_MAIN_LIBRARY ${GTEST_LIBRARY_DIR}/libgtest_main.a)

# Verify GTest exists
if(NOT EXISTS ${GTEST_LIBRARY})
    message(FATAL_ERROR "GTest library not found at ${GTEST_LIBRARY}. Please run ./scripts/setup_deps.sh first.")
endif()

# Test executable
add_executable(uvrpc_tests
    unit/test_allocator.cpp
    unit/test_flatbuffers.cpp
    unit/test_msgid.cpp
    unit/test_client.cpp
    unit/test_server.cpp
    unit/test_config.cpp
    unit/test_async.cpp
    unit/test_uvbus.cpp
    unit/test_context.cpp
    unit/test_publisher.cpp
    unit/test_subscriber.cpp
)

# Link against UVRPC library
target_link_libraries(uvrpc_tests
    uvrpc
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    pthread
    m
    rt
)

# Add include directories
target_include_directories(uvrpc_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
    ${GTEST_INCLUDE_DIR}
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(uvrpc_tests)

# Integration tests
add_subdirectory(integration)

# Performance tests
add_executable(tcp_performance_test
    tcp_performance_test.c
)

target_link_libraries(tcp_performance_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(tcp_performance_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)

set_target_properties(tcp_performance_test PROPERTIES
    OUTPUT_NAME tcp_performance_test
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../dist/bin
)

# IPC Performance Test
add_executable(ipc_performance_test
    ipc_performance_test.c
)

target_link_libraries(ipc_performance_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(ipc_performance_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)

set_target_properties(ipc_performance_test PROPERTIES
    OUTPUT_NAME ipc_performance_test
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../dist/bin
)
