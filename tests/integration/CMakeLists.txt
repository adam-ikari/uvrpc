# UVRPC Integration Tests CMakeLists.txt

cmake_minimum_required(VERSION 3.15)

# Test executables
add_executable(test_tcp
    test_tcp.c
)

# Link against UVRPC library
target_link_libraries(test_tcp
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

# Add include directories
target_include_directories(test_tcp
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)