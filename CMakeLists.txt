cmake_minimum_required(VERSION 3.15)
project(uvrpc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Output directories
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/lib)

# Multi-config generators (like Visual Studio)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

# Options
option(UVRPC_STATIC_LINKING "Use static linking" ON)
option(UVRPC_BUILD_TESTS "Build tests" OFF)
option(UVRPC_BUILD_EXAMPLES "Build examples" ON)

# Memory allocator option (default: mimalloc)
set(UVRPC_ALLOCATOR_DEFAULT "mimalloc" CACHE STRING "Memory allocator: system, mimalloc, or custom")
set_property(CACHE UVRPC_ALLOCATOR_DEFAULT PROPERTY STRINGS "system" "mimalloc" "custom")

# Add Mimalloc include if using mimalloc
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/mimalloc/include)
    add_definitions(-DUVRPC_DEFAULT_ALLOCATOR=1)
elseif(UVRPC_ALLOCATOR_DEFAULT STREQUAL "system")
    add_definitions(-DUVRPC_DEFAULT_ALLOCATOR=0)
elseif(UVRPC_ALLOCATOR_DEFAULT STREQUAL "custom")
    add_definitions(-DUVRPC_DEFAULT_ALLOCATOR=2)
else()
    message(FATAL_ERROR "Invalid allocator type: ${UVRPC_ALLOCATOR_DEFAULT}. Must be 'system', 'mimalloc', or 'custom'")
endif()

message(STATUS "UVRPC Allocator: ${UVRPC_ALLOCATOR_DEFAULT}")

# Find required packages
find_package(Threads REQUIRED)

# Include dependency management
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${UTHASH_INCLUDE_DIR}
    ${FLATCC_INCLUDE_DIR}
    ${LIBUV_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

# Source files
set(UVRPC_SOURCES
    src/uvrpc_config.c
    src/uvrpc_flatbuffers.c
    src/uvrpc_server.c
    src/uvrpc_client.c
    src/uvrpc_service.c
    src/uvrpc_transport.c
    src/uvrpc_flatbuffers.c
    src/uvrpc_msgid.c
    src/uvrpc_idmap.c
    src/uvrpc_publisher.c
    src/uvrpc_subscriber.c
    src/uvrpc_allocator.c
    src/uvrpc_async.c
    src/uvrpc_bus.c
)

# Headers
set(UVRPC_HEADERS
    include/uvrpc.h
    include/uvrpc_async.h
    include/uvrpc_bus.h
    src/uvrpc_flatbuffers.h
    src/uvrpc_service.h
)

# Static library
add_library(uvrpc STATIC ${UVRPC_SOURCES})
target_link_libraries(uvrpc PUBLIC Threads::Threads m rt)
target_include_directories(uvrpc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GENERATED_DIR}
)

# Shared library with all dependencies linked
add_library(uvrpc_shared SHARED ${UVRPC_SOURCES})
set_target_properties(uvrpc_shared PROPERTIES OUTPUT_NAME "uvrpc" VERSION ${PROJECT_VERSION} SOVERSION 0)
target_link_libraries(uvrpc_shared PUBLIC Threads::Threads m rt dl)
target_include_directories(uvrpc_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GENERATED_DIR}
)

# Add dependencies for static library
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    target_link_libraries(uvrpc PUBLIC ${MIMALLOC_LIBRARIES})
    target_include_directories(uvrpc PUBLIC ${MIMALLOC_INCLUDE_DIR})
endif()

target_link_libraries(uvrpc PUBLIC ${LIBUV_LIBRARIES})
target_include_directories(uvrpc PUBLIC ${LIBUV_INCLUDE_DIR})

target_link_libraries(uvrpc PUBLIC ${FLATCC_LIBRARIES})
target_include_directories(uvrpc PUBLIC ${FLATCC_INCLUDE_DIR})

# Add dependencies for shared library (same as static)
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    target_link_libraries(uvrpc_shared PUBLIC ${MIMALLOC_LIBRARIES})
    target_include_directories(uvrpc_shared PUBLIC ${MIMALLOC_INCLUDE_DIR})
endif()

target_link_libraries(uvrpc_shared PUBLIC ${LIBUV_LIBRARIES})
target_include_directories(uvrpc_shared PUBLIC ${LIBUV_INCLUDE_DIR})

target_link_libraries(uvrpc_shared PUBLIC ${FLATCC_LIBRARIES})
target_include_directories(uvrpc_shared PUBLIC ${FLATCC_INCLUDE_DIR})

# FlatBuffers schema
set(SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schema)
set(RPC_SCHEMA ${SCHEMA_DIR}/rpc.fbs)
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# Find flatcc compiler
find_program(FLATCC_COMPILER flatcc
    PATHS
    ${FLATCC_INSTALL_DIR}/bin
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/flatcc/bin
)

if(NOT FLATCC_COMPILER)
    message(FATAL_ERROR "FlatCC compiler not found. Please build flatcc first.")
endif()

# Custom command to generate FlatCC code
add_custom_command(
    OUTPUT ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FLATCC_COMPILER} -c -w -o ${GENERATED_DIR} ${RPC_SCHEMA}
    DEPENDS ${RPC_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from schema"
)

add_custom_target(generate_flatcc DEPENDS ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h)
add_dependencies(uvrpc generate_flatcc)
add_dependencies(uvrpc_shared generate_flatcc)

# Create merged static library with all dependencies
add_custom_target(uvrpc_merged ALL
    DEPENDS uvrpc
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/temp_extract
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/temp_extract
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/temp_extract bash -c "ar x ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc.a"
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/temp_extract bash -c "ar x ${LIBUV_LIBRARIES}"
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/temp_extract bash -c "ar x ${FLATCC_LIBRARIES}"
    COMMAND bash -c "if [ -f '${MIMALLOC_LIBRARIES}' ]; then ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/temp_extract bash -c \"ar x ${MIMALLOC_LIBRARIES}\"; fi"
    COMMAND bash -c "cd ${CMAKE_CURRENT_BINARY_DIR}/temp_extract && ${CMAKE_AR} rcs ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a *.o"
    COMMAND ${CMAKE_RANLIB} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/temp_extract
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc.a
    COMMENT "Creating merged static library with all dependencies"
    VERBATIM
)

# Examples
if(UVRPC_BUILD_EXAMPLES)

# Build benchmark
add_subdirectory(benchmark)

    function(create_example_target name)
        add_executable(${name} examples/${name}.c)

        # Always use static linking for test/example programs
        target_link_libraries(${name}
            uvrpc
            Threads::Threads
            m
            rt
            dl
        )
        
        # Add static linking flags
        set_target_properties(${name} PROPERTIES LINK_FLAGS "-static -pthread")
        
        add_dependencies(${name} generate_flatcc)
    endfunction()

    # Create all example targets
    create_example_target(simple_server)
    create_example_target(simple_client)
    create_example_target(debug_test)
    create_example_target(gateway_demo)
    create_example_target(allocator_demo)
    create_example_target(msgid_demo)
    create_example_target(loop_injection_example)
    create_example_target(broadcast_publisher)
    create_example_target(broadcast_subscriber)
    create_example_target(async_await_demo)
    create_example_target(async_chain_demo)
    create_example_target(concurrent_demo)
endif()

# Print configuration summary
message(STATUS "=== UVRPC Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Static linking: ${UVRPC_STATIC_LINKING}")
message(STATUS "Build tests: ${UVRPC_BUILD_TESTS}")
message(STATUS "Build examples: ${UVRPC_BUILD_EXAMPLES}")
message(STATUS "=============================")
# Tests
if(UVRPC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
