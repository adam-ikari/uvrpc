cmake_minimum_required(VERSION 3.10)
project(uvrpc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Git submodule 依赖路径
set(DEPS_ROOT ${CMAKE_SOURCE_DIR}/deps)

# libuv (from git submodule)
set(LIBUV_ROOT ${DEPS_ROOT}/libuv)
if(NOT EXISTS ${LIBUV_ROOT}/include/uv.h)
    message(FATAL_ERROR "libuv not found in deps. Run: git submodule update --init --recursive")
endif()

# ZeroMQ (from git submodule)
set(ZMQ_ROOT ${DEPS_ROOT}/zeromq)
if(NOT EXISTS ${ZMQ_ROOT}/include/zmq.h)
    message(FATAL_ERROR "ZeroMQ not found in deps. Run: git submodule update --init --recursive")
endif()

# uvzmq (from git submodule)
set(UVZMQ_INCLUDE_DIR ${DEPS_ROOT}/uvzmq/include)
if(NOT EXISTS ${UVZMQ_INCLUDE_DIR}/uvzmq.h)
    message(FATAL_ERROR "uvzmq not found in deps. Run: git submodule update --init --recursive")
endif()

# uthash (from git submodule)
set(UTHASH_INCLUDE_DIR ${DEPS_ROOT}/uthash/include)
if(NOT EXISTS ${UTHASH_INCLUDE_DIR}/uthash.h)
    message(FATAL_ERROR "uthash not found in deps. Run: git submodule update --init --recursive")
endif()

# flatbuffers (from git submodule)
set(FLATBUFFERS_INCLUDE_DIR ${DEPS_ROOT}/flatbuffers/include)
set(FLATBUFFERS_ROOT ${DEPS_ROOT}/flatbuffers)
if(NOT EXISTS ${FLATBUFFERS_INCLUDE_DIR}/flatbuffers/flatbuffers.h)
    message(FATAL_ERROR "flatbuffers not found in deps. Run: git submodule update --init --recursive")
endif()

# 库源文件
set(UVRPC_SOURCES
    src/uvrpc_server.c
    src/uvrpc_client.c
)

set(UVRPC_HEADERS
    include/uvrpc.h
    src/uvrpc_internal.h
)

# 创建 uvrpc 静态库
add_library(uvrpc STATIC ${UVRPC_SOURCES})

target_include_directories(uvrpc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${FLATBUFFERS_INCLUDE_DIR}
    ${UVZMQ_INCLUDE_DIR}
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${ZMQ_ROOT}/include
)

target_link_libraries(uvrpc
    ${LIBUV_ROOT}/.libs/libuv.a
    ${ZMQ_ROOT}/build/libzmq.a
    ${DEPS_ROOT}/uvzmq/build/libuvzmq.a
)

# 添加链接选项
target_link_options(uvrpc PRIVATE -pthread)

# Flatbuffers 编译器（从 git submodule 构建）
find_program(FLATC flatc HINTS ${FLATBUFFERS_ROOT})
if(NOT FLATC)
    # 如果 flatc 不存在，构建它
    message(STATUS "Building flatc compiler from submodule...")
    add_subdirectory(${FLATBUFFERS_ROOT} ${CMAKE_BINARY_DIR}/flatbuffers)
    set(FLATC flatc)
endif()

# 生成 rpc_generated.h
set(RPC_FBS ${CMAKE_SOURCE_DIR}/examples/rpc.fbs)
set(RPC_GENERATED ${CMAKE_SOURCE_DIR}/examples/rpc_generated.h)

add_custom_command(
    OUTPUT ${RPC_GENERATED}
    COMMAND ${FLATC} -c -o ${CMAKE_SOURCE_DIR}/examples ${RPC_FBS}
    DEPENDS ${RPC_FBS}
    COMMENT "Generating rpc_generated.h from rpc.fbs"
)

# 生成 echo_generated.h
set(ECHO_FBS ${CMAKE_SOURCE_DIR}/examples/echo.fbs)
set(ECHO_GENERATED ${CMAKE_SOURCE_DIR}/examples/echo_generated.h)

add_custom_command(
    OUTPUT ${ECHO_GENERATED}
    COMMAND ${FLATC} -c -o ${CMAKE_SOURCE_DIR}/examples ${ECHO_FBS}
    DEPENDS ${ECHO_FBS}
    COMMENT "Generating echo_generated.h from echo.fbs"
)

# Echo 服务器示例
add_executable(echo_server examples/echo_server.c)
add_dependencies(echo_server ${RPC_GENERATED} ${ECHO_GENERATED})
target_include_directories(echo_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples
    ${FLATBUFFERS_INCLUDE_DIR}
    ${UVZMQ_INCLUDE_DIR}
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${ZMQ_ROOT}/include
)
target_link_libraries(echo_server uvrpc ${LIBUV_ROOT}/.libs/libuv.a ${ZMQ_ROOT}/build/libzmq.a ${DEPS_ROOT}/uvzmq/build/libuvzmq.a)
target_link_options(echo_server PRIVATE -pthread)

# Echo 客户端示例
add_executable(echo_client examples/echo_client.c)
add_dependencies(echo_client ${RPC_GENERATED} ${ECHO_GENERATED})
target_include_directories(echo_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples
    ${FLATBUFFERS_INCLUDE_DIR}
    ${UVZMQ_INCLUDE_DIR}
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${ZMQ_ROOT}/include
)
target_link_libraries(echo_client uvrpc ${LIBUV_ROOT}/.libs/libuv.a ${ZMQ_ROOT}/build/libzmq.a ${DEPS_ROOT}/uvzmq/build/libuvzmq.a)
target_link_options(echo_client PRIVATE -pthread)

# ==================== 测试目标 ====================

# 基础测试
add_executable(test_basic tests/test_basic.c)
add_dependencies(test_basic ${RPC_GENERATED} ${ECHO_GENERATED})
target_include_directories(test_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples
    ${CMAKE_SOURCE_DIR}/tests
    ${FLATBUFFERS_INCLUDE_DIR}
    ${UVZMQ_INCLUDE_DIR}
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${ZMQ_ROOT}/include
)
target_link_libraries(test_basic uvrpc ${LIBUV_ROOT}/.libs/libuv.a ${ZMQ_ROOT}/build/libzmq.a ${DEPS_ROOT}/uvzmq/build/libuvzmq.a)
target_link_options(test_basic PRIVATE -pthread)

# ROUTER_DEALER 测试
add_executable(test_router_dealer tests/test_router_dealer.c)
add_dependencies(test_router_dealer ${RPC_GENERATED} ${ECHO_GENERATED})
target_include_directories(test_router_dealer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples
    ${CMAKE_SOURCE_DIR}/tests
    ${FLATBUFFERS_INCLUDE_DIR}
    ${UVZMQ_INCLUDE_DIR}
    ${UTHASH_INCLUDE_DIR}
    ${LIBUV_ROOT}/include
    ${ZMQ_ROOT}/include
)
target_link_libraries(test_router_dealer uvrpc ${LIBUV_ROOT}/.libs/libuv.a ${ZMQ_ROOT}/build/libzmq.a ${DEPS_ROOT}/uvzmq/build/libuvzmq.a)
target_link_options(test_router_dealer PRIVATE -pthread)

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
message(STATUS "ZeroMQ root: ${ZMQ_ROOT}")
message(STATUS "Flatbuffers include: ${FLATBUFFERS_INCLUDE_DIR}")
message(STATUS "uvzmq include: ${UVZMQ_INCLUDE_DIR}")
message(STATUS "uthash include: ${UTHASH_INCLUDE_DIR}")