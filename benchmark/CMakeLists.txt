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

# Unified benchmark - comprehensive benchmark suite (like 3DMark)
add_executable(uvrpc_benchmark
    uvrpc_benchmark.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_client.c
)

target_link_libraries(uvrpc_benchmark
    ${PROJECT_ROOT}/dist/lib/libuvrpc.so
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.so
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(uvrpc_benchmark PROPERTIES LINK_FLAGS "-pthread")

# DSL-generated API benchmark
add_executable(benchmark_dsl
    benchmark_dsl.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_client.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_server_stub.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_rpc_common.c
)

target_link_libraries(benchmark_dsl
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(benchmark_dsl PROPERTIES LINK_FLAGS "-static -pthread")


# Production Benchmark - simple performance testing without scoring
add_executable(production_benchmark
    production_benchmark.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_client.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_server_stub.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_rpc_common.c
)

target_link_libraries(production_benchmark
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(production_benchmark PROPERTIES LINK_FLAGS "-static -pthread")
add_executable(test_ipc_simple
    test_ipc_simple.c
    ${GENERATED_DIR}/benchmark_benchmarkservice_server_stub.c
)

target_link_libraries(test_ipc_simple
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(test_ipc_simple PROPERTIES LINK_FLAGS "-static -pthread")


add_executable(test_ipc_minimal
    test_ipc_minimal.c
)

target_link_libraries(test_ipc_minimal
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    -lm -lrt -ldl
    ${PROJECT_ROOT}/deps/mimalloc/build_shared/libmimalloc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ${PROJECT_ROOT}/deps/flatcc/lib/libflatcc.a
)

set_target_properties(test_ipc_minimal PROPERTIES LINK_FLAGS "-static -pthread")
