cmake_minimum_required(VERSION 3.10)
project(uvrpc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# 内存分配器选择
set(UVRPC_ALLOCATOR "mimalloc" CACHE STRING "Memory allocator: system, mimalloc, custom")

option(UVRPC_USE_MIMALLOC "Use mimalloc allocator (high performance)" OFF)
option(UVRPC_USE_CUSTOM_ALLOCATOR "Use custom allocator (define UVRPC_CUSTOM_MALLOC/FREE)" OFF)

if(UVRPC_ALLOCATOR STREQUAL "mimalloc")
    set(UVRPC_USE_MIMALLOC ON CACHE BOOL "Auto-set by UVRPC_ALLOCATOR" FORCE)
elseif(UVRPC_ALLOCATOR STREQUAL "custom")
    set(UVRPC_USE_CUSTOM_ALLOCATOR ON CACHE BOOL "Auto-set by UVRPC_ALLOCATOR" FORCE)
endif()

if(UVRPC_USE_MIMALLOC)
    message(STATUS "Using mimalloc allocator")
    add_definitions(-DUVRPC_USE_MIMALLOC)
elseif(UVRPC_USE_CUSTOM_ALLOCATOR)
    message(STATUS "Using custom allocator (define UVRPC_CUSTOM_MALLOC/FREE)")
    add_definitions(-DUVRPC_USE_CUSTOM_ALLOCATOR)
else()
    message(STATUS "Using system allocator (malloc/free)")
endif()

# Git submodule 依赖路径
set(DEPS_ROOT ${CMAKE_SOURCE_DIR}/deps)

# mimalloc (from git submodule)
set(MIMALLOC_ROOT ${DEPS_ROOT}/mimalloc)
if(NOT EXISTS ${MIMALLOC_ROOT}/include/mimalloc.h)
    message(FATAL_ERROR "mimalloc not found in deps. Run: git submodule update --init --recursive")
endif()

# libuv (from git submodule)
set(LIBUV_ROOT ${DEPS_ROOT}/libuv)
if(NOT EXISTS ${LIBUV_ROOT}/include/uv.h)
    message(FATAL_ERROR "libuv not found in deps. Run: git submodule update --init --recursive")
endif()

# uvzmq (from git submodule)
set(UVZMQ_ROOT ${DEPS_ROOT}/uvzmq)
if(NOT EXISTS ${UVZMQ_ROOT}/include/uvzmq.h)
    message(FATAL_ERROR "uvzmq not found in deps. Run: git submodule update --init --recursive")
endif()

# libzmq (used by uvzmq)
set(ZEROMQ_ROOT ${UVZMQ_ROOT}/third_party/zmq)
if(NOT EXISTS ${ZEROMQ_ROOT}/include/zmq.h)
    message(FATAL_ERROR "zeromq not found in uvzmq. Run: git submodule update --init --recursive")
endif()

# uthash (from git submodule)
set(UTHASH_INCLUDE_DIR ${DEPS_ROOT}/uthash/include)
if(NOT EXISTS ${UTHASH_INCLUDE_DIR}/uthash.h)
    message(FATAL_ERROR "uthash not found in deps. Run: git submodule update --init --recursive")
endif()

# msgpack-c (from git submodule - using mpack implementation)
set(MSGPACK_ROOT ${DEPS_ROOT}/msgpack-c)
if(NOT EXISTS ${MSGPACK_ROOT}/src/mpack/mpack.h)
    message(FATAL_ERROR "msgpack-c not found in deps. Run: git submodule update --init --recursive")
endif()

# 编译 mpack 源文件
set(MSGPACK_SOURCES
    ${MSGPACK_ROOT}/src/mpack/mpack-common.c
    ${MSGPACK_ROOT}/src/mpack/mpack-writer.c
    ${MSGPACK_ROOT}/src/mpack/mpack-reader.c
    ${MSGPACK_ROOT}/src/mpack/mpack-node.c
    ${MSGPACK_ROOT}/src/mpack/mpack-expect.c
    ${MSGPACK_ROOT}/src/mpack/mpack-platform.c
)

add_library(msgpackc STATIC ${MSGPACK_SOURCES})
target_include_directories(msgpackc PUBLIC ${MSGPACK_ROOT}/src/mpack)

# 库源文件
set(UVRPC_SOURCES
    src/uvrpc_utils.c
    src/msgpack_wrapper.c
    src/uvzmq_impl.c
    src/uvrpc_config.c
    src/uvrpc_new.c
)

set(UVRPC_HEADERS
    include/uvrpc.h
    src/uvrpc_internal.h
    src/msgpack_wrapper.h
)

# 创建 uvrpc 静态库
add_library(uvrpc STATIC ${UVRPC_SOURCES})

target_include_directories(uvrpc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)

target_link_libraries(uvrpc
    ${LIBUV_ROOT}/build/libuv.a
    ${ZEROMQ_ROOT}/build/lib/libzmq.a
    msgpackc
    -lstdc++
    -ldl -lrt -lpthread
)

# 如果使用 mimalloc，链接 mimalloc 库
if(UVRPC_USE_MIMALLOC)
    add_subdirectory(${MIMALLOC_ROOT} ${CMAKE_BINARY_DIR}/mimalloc EXCLUDE_FROM_ALL)
    target_link_libraries(uvrpc mimalloc)
    message(STATUS "Linking with mimalloc library")
endif()

# 添加链接选项
target_link_options(uvrpc PRIVATE -pthread)

# ==================== 示例目标 ====================
# 注意：这些示例需要更新为使用新API (uvrpc.h)
# 暂时禁用，等待重写

# Async 客户端示例
add_executable(echoservice_async_client 
    generated/echoservice_gen.c
    generated/echoservice_gen_client.c
    generated/echoservice_async_client_example.c
)
target_include_directories(echoservice_async_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(echoservice_async_client uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(echoservice_async_client PRIVATE -pthread)

# Echo 服务器示例
add_executable(echoservice_server 
    generated/echoservice_gen.c
    generated/echoservice_server_example.c
)
target_include_directories(echoservice_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(echoservice_server uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(echoservice_server PRIVATE -pthread)

# Echo 服务器示例
add_executable(echo_server examples/echo_server.c)
# ...
# # (其他示例和测试暂时禁用)

# ==================== 新API性能测试 ====================

# 新API性能测试
add_executable(test_new_api_perf tests/test_new_api_perf.c)
target_include_directories(test_new_api_perf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(test_new_api_perf uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread -lm)
target_link_options(test_new_api_perf PRIVATE -pthread)

# 安装规则
install(TARGETS uvrpc
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${CMAKE_SOURCE_DIR}/include/uvrpc.h
    DESTINATION include
)

# 打印配置信息
message(STATUS "libuv root: ${LIBUV_ROOT}")
message(STATUS "uvzmq root: ${UVZMQ_ROOT}")
message(STATUS "zeromq root: ${ZEROMQ_ROOT}")
message(STATUS "msgpack-c include: ${MSGPACK_ROOT}/include")
message(STATUS "uthash include: ${UTHASH_INCLUDE_DIR}")

# ==================== 测试生成的客户端 ====================
add_executable(test_generated_client 
    tests/test_generated_client.c
    generated/echoservice_gen.c
    generated/echoservice_gen_client.c
)
target_include_directories(test_generated_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(test_generated_client uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(test_generated_client PRIVATE -pthread)

# ==================== Benchmark using generated code ====================
add_executable(gen_benchmark 
    benchmark/gen_benchmark.c
    generated/echoservice_gen.c
    generated/echoservice_gen_client.c
)
target_include_directories(gen_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(gen_benchmark uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread -lm)
target_link_options(gen_benchmark PRIVATE -pthread)

# ==================== 测试生成代码的性能 ====================
add_executable(test_generated_code_perf 
    tests/test_generated_code_perf.c
    generated/echoservice_gen.c
    generated/echoservice_gen_client.c
)
target_include_directories(test_generated_code_perf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${MSGPACK_ROOT}/src/mpack
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(test_generated_code_perf uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread -lm)
target_link_options(test_generated_code_perf PRIVATE -pthread)

# ==================== Benchmark Programs ====================
add_executable(benchmark_server benchmark/benchmark_server.c)
target_include_directories(benchmark_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(benchmark_server uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(benchmark_server PRIVATE -pthread)

add_executable(benchmark_client benchmark/benchmark_client.c)
target_include_directories(benchmark_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(benchmark_client uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(benchmark_client PRIVATE -pthread)

add_executable(perf_test benchmark/perf_test.c)
target_include_directories(perf_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(perf_test uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread -lm)
target_link_options(perf_test PRIVATE -pthread)

add_executable(unified_test benchmark/unified_test.c)
target_include_directories(unified_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUV_ROOT}/include
    ${UVZMQ_ROOT}/include
    ${ZEROMQ_ROOT}/include
)
target_link_libraries(unified_test uvrpc ${LIBUV_ROOT}/build/libuv.a ${ZEROMQ_ROOT}/build/lib/libzmq.a -ldl -lrt -lpthread)
target_link_options(unified_test PRIVATE -pthread)