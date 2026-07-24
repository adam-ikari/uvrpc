# UVRPC Integration Tests CMakeLists.txt

cmake_minimum_required(VERSION 3.15)

# Test executables
add_executable(test_tcp
    test_tcp.c
)

add_executable(test_ipc
    test_ipc.c
)

add_executable(test_inproc
    test_inproc.c
)

add_executable(test_udp
    test_udp.c
)

add_executable(test_multi_client
    test_multi_client.c
)

add_executable(test_reconnect
    test_reconnect.c)

add_executable(test_error_handling
    test_error_handling.c
)

# Link against UVRPC library
target_link_libraries(test_tcp
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_ipc
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_inproc
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_udp
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_multi_client
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_reconnect
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)

target_link_libraries(test_error_handling
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

target_include_directories(test_ipc
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

target_include_directories(test_inproc
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

target_include_directories(test_udp
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

target_include_directories(test_multi_client
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

target_include_directories(test_reconnect
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

target_include_directories(test_error_handling
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

# Register integration tests with CTest. Each gets a timeout so a hang fails
# fast instead of blocking the suite.
add_test(NAME test_tcp            COMMAND test_tcp)
add_test(NAME test_ipc            COMMAND test_ipc)
add_test(NAME test_inproc         COMMAND test_inproc)
add_test(NAME test_multi_client   COMMAND test_multi_client)
add_test(NAME test_reconnect      COMMAND test_reconnect)
add_test(NAME test_error_handling COMMAND test_error_handling)
set_tests_properties(test_tcp test_ipc test_inproc test_multi_client
                     test_reconnect test_error_handling
                     PROPERTIES TIMEOUT 60)

# test_udp: the UDP transport's send path previously did not dispatch received
# datagrams to the server (recv_cb was passed callback_ctx instead of recv_ctx,
# so the server pointer was NULL) and cleanup freed the embedded udp_handle
# before uv_close completed (use-after-free). Both are fixed; the test now passes.
add_test(NAME test_udp            COMMAND test_udp)
set_tests_properties(test_udp PROPERTIES TIMEOUT 30)

# E2E streaming test (INPROC, same-loop). Previously an orphan built only by
# tests/run_stream_tests.sh; now built and registered here.
add_executable(test_stream_e2e
    ../e2e/test_stream_e2e.c
)
target_link_libraries(test_stream_e2e
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)
target_include_directories(test_stream_e2e
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
add_test(NAME test_stream_e2e COMMAND test_stream_e2e)
set_tests_properties(test_stream_e2e PROPERTIES TIMEOUT 60)

# Transport lifetime test: server-freed-before-client ordering must not UAF or
# double-free (regression coverage for INPROC/SAMELOOP teardown).
add_executable(test_transport_lifetime
    ../e2e/test_transport_lifetime.c
)
target_link_libraries(test_transport_lifetime
    uvrpc
    ${LIBUV_LIBRARIES}
    pthread
    m
    rt
)
target_include_directories(test_transport_lifetime
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
add_test(NAME test_transport_lifetime COMMAND test_transport_lifetime)
set_tests_properties(test_transport_lifetime PROPERTIES TIMEOUT 30)
