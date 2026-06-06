cmake_minimum_required(VERSION 3.10)
project(benchmark C)

# Release mode flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG -O2)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

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

# Find required packages
find_package(Threads REQUIRED)

# Unified benchmark - comprehensive benchmark suite
add_executable(uvrpc_benchmark
    uvrpc_benchmark.c
)

target_link_libraries(uvrpc_benchmark
    uvrpc
    Threads::Threads
    m
    rt
    dl
)

add_dependencies(uvrpc_benchmark generate_flatcc)

set_target_properties(uvrpc_benchmark PROPERTIES LINK_FLAGS "-static -pthread")
