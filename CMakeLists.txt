cmake_minimum_required(VERSION 3.10)
project(uvrpc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# Dependencies
set(DEPS_ROOT ${CMAKE_SOURCE_DIR}/deps)

# libuv
set(LIBUV_ROOT ${DEPS_ROOT}/libuv)
# nng
set(NNG_ROOT ${DEPS_ROOT}/nng)
# msgpack-c (mpack)
set(MSGPACK_ROOT ${DEPS_ROOT}/msgpack-c)

# mpack sources
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

# Experimental test programs
add_executable(test_server examples/test_server.c)
target_include_directories(test_server PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${LIBUV_ROOT}/include
    ${NNG_ROOT}/include
    ${MSGPACK_ROOT}/src/mpack
)
target_link_libraries(test_server 
    ${NNG_ROOT}/build/libnng.a
    msgpackc
    -lstdc++
    -lpthread
)

add_executable(test_client examples/test_client.c)
target_include_directories(test_client PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${LIBUV_ROOT}/include
    ${NNG_ROOT}/include
    ${MSGPACK_ROOT}/src/mpack
)
target_link_libraries(test_client 
    ${NNG_ROOT}/build/libnng.a
    msgpackc
    -lstdc++
    -lpthread
)