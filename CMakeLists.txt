cmake_minimum_required(VERSION 3.10)
project(uvrpc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# 查找依赖
find_package(PkgConfig REQUIRED)

# libuv
pkg_check_modules(LIBUV REQUIRED libuv)

# ZeroMQ
pkg_check_modules(ZMQ REQUIRED libzmq)

# flatbuffers
find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h
    HINTS /usr/local/include /usr/include
    PATH_SUFFIXES flatbuffers)

if(NOT FLATBUFFERS_INCLUDE_DIR)
    message(FATAL_ERROR "Flatbuffers not found")
endif()

# uvzmq - 假设在同一目录下
find_path(UVZMQ_INCLUDE_DIR uvzmq.h
    HINTS ${CMAKE_SOURCE_DIR}/../uvzmq/include /usr/local/include /usr/include)

if(NOT UVZMQ_INCLUDE_DIR)
    message(WARNING "uvzmq include dir not found, using ../uvzmq/include")
    set(UVZMQ_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../uvzmq/include)
endif()

# uthash
find_path(UTHASH_INCLUDE_DIR uthash.h
    HINTS /usr/local/include /usr/include)

if(NOT UTHASH_INCLUDE_DIR)
    message(FATAL_ERROR "uthash not found")
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
    ${LIBUV_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(uvrpc
    ${LIBUV_LIBRARIES}
    ${ZMQ_LIBRARIES}
)

# Flatbuffers 生成器
find_program(FLATC flatc)
if(NOT FLATC)
    message(FATAL_ERROR "flatc compiler not found")
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
    ${LIBUV_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)
target_link_libraries(echo_server uvrpc ${LIBUV_LIBRARIES} ${ZMQ_LIBRARIES})

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
    ${LIBUV_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)
target_link_libraries(echo_client uvrpc ${LIBUV_LIBRARIES} ${ZMQ_LIBRARIES})

# 安装规则
install(TARGETS uvrpc
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${CMAKE_SOURCE_DIR}/include/uvrpc.h
    DESTINATION include
)

# 打印配置信息
message(STATUS "libuv include: ${LIBUV_INCLUDE_DIRS}")
message(STATUS "libuv libraries: ${LIBUV_LIBRARIES}")
message(STATUS "ZeroMQ include: ${ZMQ_INCLUDE_DIRS}")
message(STATUS "ZeroMQ libraries: ${ZMQ_LIBRARIES}")
message(STATUS "Flatbuffers include: ${FLATBUFFERS_INCLUDE_DIR}")
message(STATUS "uvzmq include: ${UVZMQ_INCLUDE_DIR}")
message(STATUS "uthash include: ${UTHASH_INCLUDE_DIR}")