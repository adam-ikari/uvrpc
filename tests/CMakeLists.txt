# UVRPC Unit Tests CMakeLists.txt

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
    unit/test_config.cpp
)

# Integration tests (require libuv)
add_executable(uvrpc_integration_tests
    unit/test_async.cpp
    unit/test_uvbus.cpp
    unit/test_context.cpp
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
gtest_discover_tests(uvrpc_tests PROPERTIES TIMEOUT 60)

# Register integration tests with CTest
gtest_discover_tests(uvrpc_integration_tests PROPERTIES TIMEOUT 60)

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

# UVBus broadcast test
add_executable(uvbus_broadcast_test
    uvbus_broadcast_test.c
)

target_link_libraries(uvbus_broadcast_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_broadcast_test
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

add_test(NAME uvbus_broadcast_tests COMMAND uvbus_broadcast_test)
set_tests_properties(uvbus_broadcast_tests PROPERTIES TIMEOUT 60)

# Register C tests with CTest
add_test(NAME uvbus_unit_tests COMMAND uvbus_test)
add_test(NAME uvbus_simple_test COMMAND uvbus_simple_test)
set_tests_properties(uvbus_unit_tests uvbus_simple_test PROPERTIES TIMEOUT 60)

# The two in-process transports must agree on what uvbus_send() means from a
# server. SAMELOOP implemented only the client direction and dereferenced NULL
# when a server used it; nothing caught that because the examples covering it
# were not in the suite.
add_executable(inproc_send_direction_test
    inproc_send_direction_test.c
)

target_link_libraries(inproc_send_direction_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(inproc_send_direction_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
)

add_test(NAME inproc_send_direction COMMAND inproc_send_direction_test)
set_tests_properties(inproc_send_direction PROPERTIES TIMEOUT 60)

# A request whose response never arrives must end, with an error the
# application can tell apart from any other. Without a deadline the
# pending-callback slot is only released when a response arrives, so a lost
# datagram on UDP consumes it permanently.
add_executable(call_timeout_test
    call_timeout_test.c
)

target_link_libraries(call_timeout_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(call_timeout_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
)

add_test(NAME call_timeout COMMAND call_timeout_test)
set_tests_properties(call_timeout PROPERTIES TIMEOUT 30)

# Acceptance scenarios. Each file is one scenario so it can be run, read and
# debugged on its own, and each asserts something an application would notice
# breaking -- not just that a call returned 0.
add_executable(span_invariance
    acceptance/span_invariance.c
)

target_link_libraries(span_invariance
    uvrpc
    pthread
    m
    rt
)

target_include_directories(span_invariance
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
)

# Protocol robustness: a peer that can connect must not be able to kill the
# process. Frames whose length is legal but whose payload is not a valid
# FlatBuffers root used to reach the reader and take it out of bounds.
add_executable(protocol_robustness
    acceptance/protocol_robustness.c
)

target_link_libraries(protocol_robustness
    uvrpc
    pthread
    m
    rt
)

target_include_directories(protocol_robustness
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
)

# Backpressure: the three limits fail fast, name themselves, consume nothing,
# and give the resource back once the in-flight work drains. Needs TCP -- on
# loopback the response completes inline, so no limit is ever reached.
add_executable(backpressure
    acceptance/backpressure.c
)

target_link_libraries(backpressure
    uvrpc
    pthread
    m
    rt
)

target_include_directories(backpressure
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
)

add_test(NAME protocol_robustness COMMAND protocol_robustness)
add_test(NAME backpressure COMMAND backpressure)
set_tests_properties(backpressure PROPERTIES
    LABELS "acceptance"
    TIMEOUT 90)

set_tests_properties(protocol_robustness PROPERTIES
    LABELS "acceptance"
    TIMEOUT 60)

add_test(NAME span_invariance COMMAND span_invariance)


set_tests_properties(span_invariance PROPERTIES
    LABELS "acceptance"
    TIMEOUT 30)

# Registry contract: INPROC and SAMELOOP share a registry the caller creates and
# hands to every config that has to see the others. It is an internal header, so
# the test includes it directly.
add_executable(loop_registry_test
    loop_registry_test.c
)

target_link_libraries(loop_registry_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(loop_registry_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/flatcc/include
)

add_test(NAME loop_registry COMMAND loop_registry_test)
set_tests_properties(loop_registry PROPERTIES TIMEOUT 60)

# Allocator ownership test. Only meaningful in custom-allocator builds: with the
# system or mimalloc allocator uvrpc_free() bottoms out in free(), so a flatcc
# buffer released through it is indistinguishable from a legitimate one.
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "custom")
    add_executable(allocator_ownership_test
        allocator_ownership_test.c
    )

    target_link_libraries(allocator_ownership_test
        uvrpc
        pthread
        m
        rt
    )

    target_include_directories(allocator_ownership_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/flatcc/include
    )

    add_test(NAME allocator_ownership COMMAND allocator_ownership_test)
    set_tests_properties(allocator_ownership PROPERTIES TIMEOUT 60)
endif()

# DSL generator round trip. Drives the generated client function over a live
# connection and asserts on what the server decoded -- a test that re-implemented
# the encoder in C would pass against a generator that emits broken code.
# The generated sources come from the build-wide generate_dsl target, so this
# test and the log_service demos exercise the same output.
if(UVRPC_DSL_AVAILABLE)
    # The GENERATED property is directory-scoped, so it has to be set here too:
    # add_executable resolves sources at generate time and these do not exist
    # until generate_dsl runs.
    set_source_files_properties(${UVRPC_DSL_SOURCES} PROPERTIES GENERATED TRUE)
    add_executable(dsl_codegen_test
        dsl_codegen_test.c
        ${UVRPC_DSL_SOURCES}
    )

    add_dependencies(dsl_codegen_test generate_dsl)

    target_link_libraries(dsl_codegen_test
        uvrpc
        pthread
        m
        rt
    )

    target_include_directories(dsl_codegen_test
        PRIVATE
        ${UVRPC_DSL_GEN_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/flatcc/include
    )

    add_test(NAME dsl_codegen COMMAND dsl_codegen_test)
    set_tests_properties(dsl_codegen PROPERTIES TIMEOUT 120)
else()
    message(STATUS "Skipping dsl_codegen test: python3 with jinja2 not available")
endif()

# Integration tests
add_subdirectory(integration)
