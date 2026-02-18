cmake_minimum_required(VERSION 3.10)
project(benchmark C)

# Force Release mode
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "Benchmark requires Release mode. Setting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Add compile definitions for Release mode
add_definitions(-DNDEBUG -O2)

# Strip debug symbols for performance
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Paths
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DEPS_DIR ${PROJECT_ROOT}/deps)
set(GENERATED_DIR ${PROJECT_ROOT}/generated)

# Include directories
include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/deps/uthash/include
    ${DEPS_DIR}/flatcc/include
    ${DEPS_DIR}/libuv/include
    ${GENERATED_DIR}
    ${GENERATED_DIR}/rpc_benchmark
)

# Check if mimalloc is available
if(EXISTS ${DEPS_DIR}/mimalloc/lib/libmimalloc.a)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../deps/mimalloc/include)
    add_definitions(-DUVRPC_ALLOCATOR=1)
    set(USE_MIMALLOC TRUE)
else()
    add_definitions(-DUVRPC_ALLOCATOR=0)
    set(USE_MIMALLOC FALSE)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Performance test programs
add_executable(server perf_server.c 
    ${GENERATED_DIR}/rpc/rpc_user_impl.c 
    ${GENERATED_DIR}/rpc_benchmark/rpc_server_stub.c)
target_link_libraries(server uvrpc rpc_benchmark Threads::Threads m rt)
add_dependencies(server generate_flatcc)

add_executable(client perf_benchmark.c)
target_link_libraries(client uvrpc rpc_benchmark Threads::Threads m rt)
add_dependencies(client generate_flatcc)

# RPC benchmark library (generated code - client only)
set(RPC_BENCHMARK_SOURCES
    ${GENERATED_DIR}/rpc_benchmark/rpc_client.c
    ${GENERATED_DIR}/rpc_benchmark/rpc_server_stub.c
    ${PROJECT_ROOT}/src/uvrpc_async.c  # Add async implementation
)

add_library(rpc_benchmark STATIC ${RPC_BENCHMARK_SOURCES})
target_include_directories(rpc_benchmark PUBLIC ${GENERATED_DIR}/rpc_benchmark)
target_include_directories(rpc_benchmark PUBLIC ${GENERATED_DIR}/rpc)
target_include_directories(rpc_benchmark PUBLIC ${PROJECT_ROOT}/include)
target_include_directories(rpc_benchmark PUBLIC ${PROJECT_ROOT}/src)
target_link_libraries(rpc_benchmark uvrpc Threads::Threads m rt)

# Link mimalloc if available
if(USE_MIMALLOC)
    target_link_libraries(server uvrpc
        ${DEPS_DIR}/mimalloc/lib/libmimalloc.a
    )
    target_link_libraries(client uvrpc
        ${DEPS_DIR}/mimalloc/lib/libmimalloc.a
    )
endif()

# Print configuration
message(STATUS "")
message(STATUS "=== Benchmark Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C flags: ${CMAKE_C_FLAGS}")
message(STATUS "Allocator: ${USE_MIMALLOC}")
message(STATUS "================================")
message(STATUS "")

# Stress test with multiple independent loops
add_executable(stress_test_multi_loop stress_test_multi_loop.c 
    ${GENERATED_DIR}/rpc_benchmark/rpc_server_stub.c
    ${GENERATED_DIR}/rpc/rpc_user_impl.c)
target_link_libraries(stress_test_multi_loop uvrpc Threads::Threads m rt)
add_dependencies(stress_test_multi_loop generate_flatcc)

# Unified benchmark with fork/process support
add_executable(unified_benchmark unified_benchmark.c 
    ${GENERATED_DIR}/rpc_benchmark/rpc_server_stub.c
    ${GENERATED_DIR}/rpc_benchmark/rpc_client.c
    ${GENERATED_DIR}/rpc/rpc_user_impl.c)
target_link_libraries(unified_benchmark uvrpc Threads::Threads m rt)
add_dependencies(unified_benchmark generate_flatcc)