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

# Unit tests (no libuv dependency)
add_executable(uvrpc_tests
    unit/test_allocator.cpp
    unit/test_flatbuffers.cpp
    unit/test_msgid.cpp
    unit/test_client.cpp
    unit/test_server.cpp
    unit/test_config.cpp
)

# Integration tests (require libuv)
add_executable(uvrpc_integration_tests
    unit/test_async.cpp
    unit/test_uvbus.cpp
    unit/test_context.cpp
    unit/test_publisher.cpp
    unit/test_subscriber.cpp
    unit/test_server_context.cpp
    unit/test_client_context.cpp
    unit/test_client_retry.cpp
    unit/test_client_concurrency.cpp
    unit/test_server_stats.cpp
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

# Link integration tests against UVRPC library
target_link_libraries(uvrpc_integration_tests
    uvrpc
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    pthread
    m
    rt
)

# Add include directories for unit tests
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

# Add include directories for integration tests
target_include_directories(uvrpc_integration_tests
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

# Register unit tests with CTest
include(GoogleTest)
gtest_discover_tests(uvrpc_tests)

# Register integration tests with CTest
gtest_discover_tests(uvrpc_integration_tests)

# UVBus C tests (pure C, no GTest dependency)
add_executable(uvbus_test
    uvbus_test.c
)

target_link_libraries(uvbus_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_test
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

# UVBus performance tests
add_executable(uvbus_perf_test
    uvbus_perf_test.c
)

target_link_libraries(uvbus_perf_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_perf_test
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

# UVBus simple test
add_executable(uvbus_simple_test
    uvbus_simple_test.c
)

target_link_libraries(uvbus_simple_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_simple_test
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

# UVBus simple performance test
add_executable(uvbus_perf_test_simple
    uvbus_perf_test_simple.c
)

target_link_libraries(uvbus_perf_test_simple
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_perf_test_simple
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

# Register C tests with CTest
add_test(NAME uvbus_unit_tests COMMAND uvbus_test)
add_test(NAME uvbus_perf_tests COMMAND uvbus_perf_test)
add_test(NAME uvbus_simple_test COMMAND uvbus_simple_test)
add_test(NAME uvbus_perf_test_simple COMMAND uvbus_perf_test_simple)

# Integration tests
add_subdirectory(integration)
