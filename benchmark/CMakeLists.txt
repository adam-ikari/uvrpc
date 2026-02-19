cmake_minimum_required(VERSION 3.10)
project(benchmark C)

# Release mode flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG -O2)
    # Strip debug symbols for performance
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


# Performance benchmark (includes both SERVER_CLIENT and BROADCAST modes)
add_executable(benchmark perf_benchmark.c)
target_link_libraries(benchmark uvrpc Threads::Threads m rt dl)
