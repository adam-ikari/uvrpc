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

# Unified benchmark - auto-starts server and runs tests
add_executable(benchmark
    benchmark.c
)

target_link_libraries(benchmark
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(benchmark PROPERTIES LINK_FLAGS "-static -pthread")