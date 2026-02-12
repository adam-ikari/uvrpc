cmake_minimum_required(VERSION 3.15)
project(uvrpc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Options
option(UVRPC_STATIC_LINKING "Use static linking" ON)
option(UVRPC_BUILD_TESTS "Build tests" OFF)
option(UVRPC_BUILD_EXAMPLES "Build examples" ON)

# Find required packages
find_package(Threads REQUIRED)

# Dependencies
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/uthash/include
    ${DEPS_DIR}/flatcc/include
    ${DEPS_DIR}/nng/include
    ${DEPS_DIR}/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

# Source files
set(UVRPC_SOURCES
    src/uvrpc_config.c
    src/uvrpc_msgpack.c
    src/uvrpc_server.c
    src/uvrpc_client.c
    src/uvrpc_service.c
)

# Headers
set(UVRPC_HEADERS
    include/uvrpc.h
    src/uvrpc_msgpack.h
    src/uvrpc_service.h
)

# Static library
add_library(uvrpc STATIC ${UVRPC_SOURCES})
target_link_libraries(uvrpc INTERFACE Threads::Threads m rt)

# FlatBuffers schema
set(SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schema)
set(RPC_SCHEMA ${SCHEMA_DIR}/rpc.fbs)
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# Custom command to generate FlatCC code
add_custom_command(
    OUTPUT ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${DEPS_DIR}/flatcc/bin/flatcc -c -w -o ${GENERATED_DIR} ${RPC_SCHEMA}
    DEPENDS ${RPC_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from schema"
)

add_custom_target(generate_flatcc DEPENDS ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h)
add_dependencies(uvrpc generate_flatcc)

# Examples
if(UVRPC_BUILD_EXAMPLES)
    add_executable(echo_server examples/echo_server.c ${UVRPC_SOURCES})
    add_executable(echo_client examples/echo_client.c ${UVRPC_SOURCES})

    if(UVRPC_STATIC_LINKING)
        target_link_libraries(echo_server 
            ${DEPS_DIR}/nng/build/libnng.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        target_link_options(echo_server PRIVATE "-Wl,--whole-archive" "-Wl,--no-whole-archive")
        
        target_link_libraries(echo_client
            ${DEPS_DIR}/nng/build/libnng.a
            ${DEPS_DIR}/libuv/build/libuv.a
            ${DEPS_DIR}/flatcc/lib/libflatcc.a
            Threads::Threads m rt
        )
        target_link_options(echo_client PRIVATE "-Wl,--whole-archive" "-Wl,--no-whole-archive")
    endif()

    add_dependencies(echo_server generate_flatcc)
    add_dependencies(echo_client generate_flatcc)
endif()

# Print configuration summary
message(STATUS "=== UVRPC Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Static linking: ${UVRPC_STATIC_LINKING}")
message(STATUS "Build tests: ${UVRPC_BUILD_TESTS}")
message(STATUS "Build examples: ${UVRPC_BUILD_EXAMPLES}")
message(STATUS "=============================")