cmake_minimum_required(VERSION 3.10)
project(benchmark C)

# Release mode flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG -O2)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

# Set C standard (C99, consistent with the main project)
set(CMAKE_C_STANDARD 99)
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

# Find required packages
find_package(Threads REQUIRED)

# Per-transport round-trip latency + throughput benchmark.
# Usage: ./dist/bin/perf_benchmark [requests] [transport]
#   transport: sameloop | inproc | ipc | udp | tcp  (default: inproc)
add_executable(perf_benchmark
    perf_benchmark.c
)
target_link_libraries(perf_benchmark
    uvrpc
    Threads::Threads
    m
    rt
    dl
)
add_dependencies(perf_benchmark generate_flatcc)

# Direct-call baseline: measures raw function-call overhead (no transport,
# no serialization) as a reference point for SAMELOOP/INPROC overhead.
add_executable(direct_call_benchmark
    direct_call_benchmark.c
)
target_link_libraries(direct_call_benchmark
    m
    rt
)
