cmake_minimum_required(VERSION 3.15)
project(uvrpc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Options
option(UVRPC_STATIC_LINKING "Use static linking" ON)
option(UVRPC_BUILD_TESTS "Build tests" OFF)
option(UVRPC_BUILD_EXAMPLES "Build examples" ON)

# Memory allocator option (default: mimalloc)
set(UVRPC_ALLOCATOR_DEFAULT "mimalloc" CACHE STRING "Memory allocator: system or mimalloc")
set_property(CACHE UVRPC_ALLOCATOR_DEFAULT PROPERTY STRINGS "system" "mimalloc")

# Add Mimalloc include if using mimalloc
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/mimalloc/include)
    add_definitions(-DUVRPC_ALLOCATOR=1)
else()
    add_definitions(-DUVRPC_ALLOCATOR=0)
endif()

message(STATUS "UVRPC Allocator: ${UVRPC_ALLOCATOR_DEFAULT}")

# Find required packages
find_package(Threads REQUIRED)

# Dependencies
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/uthash/include
    ${DEPS_DIR}/flatcc/include
    ${DEPS_DIR}/nng/include
    ${DEPS_DIR}/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

# Source files
set(UVRPC_SOURCES
    src/uvrpc_config.c
    src/uvrpc_msgpack.c
    src/uvrpc_server.c
    src/uvrpc_client.c
    src/uvrpc_service.c
    src/uvrpc_transport.c
    src/uvrpc_frame.c
    src/uvrpc_msgid.c
    src/uvrpc_idmap.c
    src/uvrpc_publisher.c
    src/uvrpc_subscriber.c
    src/uvrpc_allocator.c
    src/uvrpc_async.c
)

# Headers
set(UVRPC_HEADERS
    include/uvrpc.h
    include/uvrpc_async.h
    src/uvrpc_msgpack.h
    src/uvrpc_service.h
)

# Static library
add_library(uvrpc STATIC ${UVRPC_SOURCES})

# Link libraries
target_link_libraries(uvrpc PUBLIC Threads::Threads m rt)

# Add Mimalloc if using it
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    target_link_libraries(uvrpc PUBLIC ${DEPS_DIR}/mimalloc/lib/libmimalloc.a)
endif()

# FlatBuffers schema
set(SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schema)
set(RPC_SCHEMA ${SCHEMA_DIR}/rpc.fbs)
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# Custom command to generate FlatCC code
add_custom_command(
    OUTPUT ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${DEPS_DIR}/flatcc/bin/flatcc -c -w -o ${GENERATED_DIR} ${RPC_SCHEMA}
    DEPENDS ${RPC_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from schema"
)

add_custom_target(generate_flatcc DEPENDS ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h)
add_dependencies(uvrpc generate_flatcc)

# Examples
if(UVRPC_BUILD_EXAMPLES)
    add_executable(simple_server examples/simple_server.c)
    add_executable(simple_client examples/simple_client.c)
    add_executable(debug_test examples/debug_test.c)
    add_executable(perf_test examples/perf_test.c)
    add_executable(gateway_demo examples/gateway_demo.c)
    add_executable(allocator_demo examples/allocator_demo.c)
    add_executable(msgid_demo examples/msgid_demo.c)
    add_executable(loop_injection_example examples/loop_injection_example.c)
    add_executable(broadcast_publisher examples/broadcast_publisher.c)
    add_executable(broadcast_subscriber examples/broadcast_subscriber.c)
    add_executable(async_await_demo examples/async_await_demo.c)
    add_executable(async_chain_demo examples/async_chain_demo.c)
    add_executable(concurrent_demo examples/concurrent_demo.c)

    if(UVRPC_STATIC_LINKING)
        target_link_libraries(simple_server
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(simple_client
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(debug_test
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(perf_test
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(gateway_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(allocator_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(msgid_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(loop_injection_example
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(broadcast_publisher
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(broadcast_subscriber
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(async_await_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(async_chain_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        target_link_libraries(concurrent_demo
            ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        
        # Add Mimalloc with whole-archive if needed
        if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
            target_link_options(simple_server PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(simple_client PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(debug_test PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(perf_test PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(gateway_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(allocator_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(msgid_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(loop_injection_example PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(broadcast_publisher PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(broadcast_subscriber PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(async_await_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(async_chain_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
            target_link_options(concurrent_demo PRIVATE "-Wl,--whole-archive" ${DEPS_DIR}/mimalloc/lib/libmimalloc.a "-Wl,--no-whole-archive")
        endif()
        
        add_dependencies(simple_server generate_flatcc)
        add_dependencies(simple_client generate_flatcc)
        add_dependencies(debug_test generate_flatcc)
        add_dependencies(perf_test generate_flatcc)
        add_dependencies(gateway_demo generate_flatcc)
        add_dependencies(allocator_demo generate_flatcc)
        add_dependencies(msgid_demo generate_flatcc)
        add_dependencies(loop_injection_example generate_flatcc)
        add_dependencies(broadcast_publisher generate_flatcc)
        add_dependencies(broadcast_subscriber generate_flatcc)
        add_dependencies(async_await_demo generate_flatcc)
        add_dependencies(async_chain_demo generate_flatcc)
        add_dependencies(concurrent_demo generate_flatcc)
    else()
        target_link_libraries(simple_server uvrpc)
        target_link_libraries(simple_client uvrpc)
        target_link_libraries(debug_test uvrpc)
        target_link_libraries(perf_test uvrpc)
        target_link_libraries(gateway_demo uvrpc)
        target_link_libraries(allocator_demo uvrpc)
        target_link_libraries(msgid_demo uvrpc)
        target_link_libraries(loop_injection_example uvrpc)
        target_link_libraries(broadcast_publisher uvrpc)
        target_link_libraries(broadcast_subscriber uvrpc)
        target_link_libraries(async_await_demo uvrpc)
        target_link_libraries(async_chain_demo uvrpc)
        target_link_libraries(concurrent_demo uvrpc)
        
        add_dependencies(simple_server generate_flatcc)
        add_dependencies(simple_client generate_flatcc)
        add_dependencies(debug_test generate_flatcc)
        add_dependencies(perf_test generate_flatcc)
        add_dependencies(gateway_demo generate_flatcc)
        add_dependencies(allocator_demo generate_flatcc)
        add_dependencies(msgid_demo generate_flatcc)
        add_dependencies(loop_injection_example generate_flatcc)
        add_dependencies(broadcast_publisher generate_flatcc)
        add_dependencies(broadcast_subscriber generate_flatcc)
        add_dependencies(async_await_demo generate_flatcc)
        add_dependencies(async_chain_demo generate_flatcc)
        add_dependencies(concurrent_demo generate_flatcc)
    endif()
endif()

# Print configuration summary
message(STATUS "=== UVRPC Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Static linking: ${UVRPC_STATIC_LINKING}")
message(STATUS "Build tests: ${UVRPC_BUILD_TESTS}")
message(STATUS "Build examples: ${UVRPC_BUILD_EXAMPLES}")
message(STATUS "=============================")