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
add_executable(perf_server perf_server.c ${GENERATED_DIR}/rpc/rpc_user_impl.c ${GENERATED_DIR}/rpc_benchmark/rpc_server_stub.c)
target_link_libraries(perf_server uvrpc rpc_benchmark Threads::Threads m rt)
add_dependencies(perf_server generate_flatcc)

add_executable(perf_benchmark perf_benchmark.c)
target_link_libraries(perf_benchmark uvrpc rpc_benchmark Threads::Threads m rt)
add_dependencies(perf_benchmark generate_flatcc)

# RPC benchmark library (generated code - client only)
set(RPC_BENCHMARK_SOURCES
    ${GENERATED_DIR}/rpc_benchmark/rpc_client.c
)

add_library(rpc_benchmark STATIC ${RPC_BENCHMARK_SOURCES})
target_include_directories(rpc_benchmark PUBLIC ${GENERATED_DIR}/rpc_benchmark)
target_include_directories(rpc_benchmark PUBLIC ${GENERATED_DIR}/rpc)

# Link mimalloc if available
if(USE_MIMALLOC)
    target_link_libraries(perf_server uvrpc
        ${DEPS_DIR}/mimalloc/lib/libmimalloc.a
    )
    target_link_libraries(perf_client uvrpc
        ${DEPS_DIR}/mimalloc/lib/libmimalloc.a
    )
    target_link_libraries(latency_test uvrpc
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