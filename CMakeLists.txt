cmake_minimum_required(VERSION 3.15)
project(uvrpc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Release mode flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

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

# Debug logging options
option(UVRPC_DEBUG_LOGGING "Enable debug logging (adds UVRPC_DEBUG and UVBUS_DEBUG macros)" OFF)

if(UVRPC_DEBUG_LOGGING)
    add_definitions(-DUVRPC_DEBUG)
    add_definitions(-DUVBUS_DEBUG)
    message(STATUS "Debug logging: ENABLED")
else()
    message(STATUS "Debug logging: DISABLED")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Enable CTest at the top level so `ctest` run from build/ discovers tests.
# (tests/CMakeLists.txt also calls enable_testing(); this makes it work from
# the build root regardless, and lets us attach a default timeout below.)
enable_testing()
include(CTest)

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
    src/uvrpc_context.c
    src/uvrpc_flatbuffers.c
    src/uvrpc_server.c
    src/uvrpc_client.c
    src/uvrpc_service.c
    src/uvrpc_msgid.c
    src/uvrpc_idmap.c
    src/uvrpc_allocator.c
    src/uvrpc_async.c
    src/uvrpc_primitives.c
    src/uvrpc_config.c
    src/uvrpc_strerror.c
    src/uvasync.c
    src/uvbus.c
    src/uvbus_transport_tcp.c
    src/uvbus_transport_udp.c
    src/uvbus_transport_ipc.c
    src/uvbus_transport_inproc.c
    src/uvbus_transport_sameloop.c
    src/uvbus_loop_registry.c
)

# Headers
set(UVRPC_HEADERS
    include/uvrpc.h
    include/uvrpc_allocator.h
    include/uvrpc_context.h
    include/uvrpc_flatbuffers.h
    include/uvrpc_idmap.h
    include/uvrpc_msgid.h
    include/uvrpc_primitives.h
    include/uvrpc_strerror.h
    include/uvrpc_async.h
    include/uvrpc_service.h
    include/uvbus.h
    include/uvbus_config.h
    src/uvrpc_flatbuffers.h
    src/uvrpc_service.h
)

# Static library
add_library(uvrpc STATIC ${UVRPC_SOURCES})
target_compile_options(uvrpc PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(uvrpc PUBLIC Threads::Threads m rt)
target_include_directories(uvrpc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GENERATED_DIR}
)

# Shared library with all dependencies linked
add_library(uvrpc_shared SHARED ${UVRPC_SOURCES})
target_compile_options(uvrpc_shared PRIVATE -Wall -Wextra -Wpedantic)
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

target_link_libraries(uvrpc PUBLIC ${FLATCC_LIBRARIES} ${FLATCC_RT_LIBRARY})
target_include_directories(uvrpc PUBLIC ${FLATCC_INCLUDE_DIR})

# Add dependencies for shared library (same as static)
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    target_link_libraries(uvrpc_shared PUBLIC ${MIMALLOC_LIBRARIES})
    target_include_directories(uvrpc_shared PUBLIC ${MIMALLOC_INCLUDE_DIR})
endif()

target_link_libraries(uvrpc_shared PUBLIC ${LIBUV_LIBRARIES})
target_include_directories(uvrpc_shared PUBLIC ${LIBUV_INCLUDE_DIR})

target_link_libraries(uvrpc_shared PUBLIC ${FLATCC_LIBRARIES} ${FLATCC_RT_LIBRARY})
target_include_directories(uvrpc_shared PUBLIC ${FLATCC_INCLUDE_DIR})

# FlatBuffers schema
set(SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schema)
set(RPC_SCHEMA ${SCHEMA_DIR}/rpc.fbs)
set(BENCHMARK_SERVER_SCHEMA ${SCHEMA_DIR}/benchmark_server.fbs)
set(BENCHMARK_SCHEMA ${SCHEMA_DIR}/benchmark.fbs)
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

# Custom command to generate RPC FlatCC code
add_custom_command(
    OUTPUT ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h
            ${GENERATED_DIR}/rpc_verifier.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FLATCC_COMPILER} -c -v -w -o ${GENERATED_DIR} ${RPC_SCHEMA}
    DEPENDS ${RPC_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from RPC schema"
)

# Generate FlatCC code from benchmark-server schema
add_custom_command(
    OUTPUT ${GENERATED_DIR}/benchmark_server_reader.h ${GENERATED_DIR}/benchmark_server_builder.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FLATCC_COMPILER} -c -w -o ${GENERATED_DIR} ${BENCHMARK_SERVER_SCHEMA}
    DEPENDS ${BENCHMARK_SERVER_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from benchmark-server schema"
)

# Generate FlatCC code from benchmark schema
add_custom_command(
    OUTPUT ${GENERATED_DIR}/benchmark_reader.h ${GENERATED_DIR}/benchmark_builder.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FLATCC_COMPILER} -c -w -o ${GENERATED_DIR} ${BENCHMARK_SCHEMA}
    DEPENDS ${BENCHMARK_SCHEMA}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FlatCC code from benchmark schema"
)

add_custom_target(generate_flatcc DEPENDS ${GENERATED_DIR}/rpc_reader.h ${GENERATED_DIR}/rpc_builder.h ${GENERATED_DIR}/rpc_verifier.h ${GENERATED_DIR}/benchmark_server_reader.h ${GENERATED_DIR}/benchmark_server_builder.h ${GENERATED_DIR}/benchmark_reader.h ${GENERATED_DIR}/benchmark_builder.h)
add_dependencies(uvrpc generate_flatcc)
add_dependencies(uvrpc_shared generate_flatcc)

# RPC DSL codegen (Python + Jinja2). Not part of the library -- uvrpc builds
# without it -- but the DSL test and the log_service demos do, so they are
# wired to a shared generated directory here. When no interpreter can import
# jinja2 the target is simply not created and the dependents are skipped.
# The build tree, not ${GENERATED_DIR} (the source tree): these files are
# build products and must not show up as untracked files in the checkout.
set(UVRPC_DSL_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/log_service)
set(UVRPC_DSL_AVAILABLE FALSE)
find_package(Python3 COMPONENTS Interpreter QUIET)
# find_package resolves to whatever Python3_EXECUTABLE finds first, which on a
# developer machine is often a user-local interpreter without jinja2 while the
# system one has it. Probe the candidates and take the first that can import it.
set(_dsl_candidates ${Python3_EXECUTABLE} /usr/bin/python3 python3)
foreach(_candidate IN LISTS _dsl_candidates)
    if(_candidate AND NOT UVRPC_DSL_AVAILABLE)
        execute_process(
            COMMAND ${_candidate} -c "import jinja2"
            RESULT_VARIABLE _dsl_rc
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_dsl_rc EQUAL 0)
            set(UVRPC_DSL_AVAILABLE TRUE)
            set(UVRPC_DSL_PYTHON ${_candidate})
        endif()
    endif()
endforeach()

if(UVRPC_DSL_AVAILABLE)
    set(_dsl_ns log_logservice)
    file(GLOB _dsl_templates ${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/*.j2)
    add_custom_command(
        OUTPUT ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_client.c
               ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_rpc_common.c
               ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_server_stub.c
               ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_api.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${UVRPC_DSL_GEN_DIR}
        COMMAND ${UVRPC_DSL_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uvrpcc.py
                --flatcc ${FLATCC_COMPILER}
                ${CMAKE_CURRENT_SOURCE_DIR}/schema/log_service.fbs
                -o ${UVRPC_DSL_GEN_DIR}
        # The templates decide what the generated code does, so editing one
        # must rerun the generator rather than leave dependents on stale output.
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/uvrpcc.py
                ${CMAKE_CURRENT_SOURCE_DIR}/schema/log_service.fbs
                ${_dsl_templates}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating RPC DSL code from log_service.fbs"
        VERBATIM
    )
    # add_executable resolves its sources at generate time, so sources that a
    # custom command has not produced yet have to be declared GENERATED or
    # configuration fails outright on a clean checkout.
    set(UVRPC_DSL_SOURCES
        ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_client.c
        ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_rpc_common.c
        ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_server_stub.c
    )
    set_source_files_properties(${UVRPC_DSL_SOURCES} PROPERTIES GENERATED TRUE)
    add_custom_target(generate_dsl DEPENDS ${UVRPC_DSL_GEN_DIR}/${_dsl_ns}_api.h)
    message(STATUS "RPC DSL codegen: ${UVRPC_DSL_PYTHON}")
else()
    message(STATUS "RPC DSL codegen unavailable (no python3 with jinja2); DSL test and log_service demos are skipped")
endif()

# Create merged static library with all dependencies.
#
# Built with an `ar` MRI script (CREATE/ADDLIB/SAVE) instead of extracting every
# archive into one directory and repacking. The old `ar x` + `ar rcs *.o` flattening
# keyed objects by basename, so libuv's src/random.c and mimalloc's src/random.c
# collided and one was silently dropped -- users linking -l:libuvrpc_full.a got
# "undefined reference to `uv_random'".
# Snapshot the freshly linked archive before uvrpc_merged overwrites
# dist/lib/libuvrpc.a with the merged result -- otherwise each rebuild would
# ADDLIB the previous merged archive into itself and grow unboundedly.
add_custom_command(TARGET uvrpc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:uvrpc> ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc_pristine.a
    COMMENT "Snapshotting pristine libuvrpc archive"
    VERBATIM
)

set(UVRPC_MERGE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/uvrpc_merge.mri)
file(WRITE ${UVRPC_MERGE_SCRIPT} "CREATE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a\n")
file(APPEND ${UVRPC_MERGE_SCRIPT} "ADDLIB ${CMAKE_CURRENT_BINARY_DIR}/libuvrpc_pristine.a\n")
file(APPEND ${UVRPC_MERGE_SCRIPT} "ADDLIB ${LIBUV_LIBRARIES}\n")
file(APPEND ${UVRPC_MERGE_SCRIPT} "ADDLIB ${FLATCC_LIBRARIES}\n")
file(APPEND ${UVRPC_MERGE_SCRIPT} "ADDLIB ${FLATCC_RT_LIBRARY}\n")
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    file(APPEND ${UVRPC_MERGE_SCRIPT} "ADDLIB ${MIMALLOC_LIBRARIES}\n")
endif()
file(APPEND ${UVRPC_MERGE_SCRIPT} "SAVE\nEND\n")

add_custom_target(uvrpc_merged ALL
    DEPENDS uvrpc ${UVRPC_MERGE_SCRIPT}
    COMMAND bash -c "${CMAKE_AR} -M < ${UVRPC_MERGE_SCRIPT}"
    COMMAND ${CMAKE_RANLIB} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc_full.a ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libuvrpc.a
    COMMENT "Creating merged static library with all dependencies"
    VERBATIM
)

# Examples
if(UVRPC_BUILD_EXAMPLES)

# Build benchmark
add_subdirectory(benchmark)

    # -static and -fsanitize are mutually exclusive: the driver refuses the
    # link ("cannot specify -static with -fsanitize=address"), so an ASan or
    # UBSan build of the examples could never be produced. Detect the sanitizer
    # and drop the static link rather than making whoever runs one remember to
    # turn the examples off.
    set(_uvrpc_sanitizing FALSE)
    if(CMAKE_C_FLAGS MATCHES "-fsanitize" OR CMAKE_EXE_LINKER_FLAGS MATCHES "-fsanitize")
        set(_uvrpc_sanitizing TRUE)
    endif()

    function(create_example_target name)
        add_executable(${name} examples/${name}.c)

        # Always use static linking for example programs
        target_link_libraries(${name}
            uvrpc
            Threads::Threads
            m
            rt
            dl
        )

        if(NOT _uvrpc_sanitizing)
            set_target_properties(${name} PROPERTIES LINK_FLAGS "-static -pthread")
        endif()

        add_dependencies(${name} generate_flatcc)
    endfunction()

    # Create example targets
    
    # Basic RPC examples
    create_example_target(simple_server)
    create_example_target(simple_client)
    create_example_target(simple_inproc)
    create_example_target(udp_rpc_demo)
    create_example_target(sameloop_rpc_demo)
    
    # Oneway RPC examples
    create_example_target(test_oneway)

    # The log_service demos are generated from the DSL rather than hand-written,
    # so they need the generated sources. They were skipped for years because
    # nothing produced them; generate_dsl now does, and building them here is
    # what keeps the generator and the library from drifting apart -- these two
    # files were the only real users of the generated API, and both had rotted
    # against it unnoticed.
    if(UVRPC_DSL_AVAILABLE)
        foreach(_demo log_simple_demo log_service_demo)
            add_executable(${_demo}
                examples/${_demo}.c
                ${UVRPC_DSL_SOURCES}
            )
            target_link_libraries(${_demo} uvrpc Threads::Threads m rt dl)
            target_include_directories(${_demo}
                PRIVATE
                ${UVRPC_DSL_GEN_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/generated
                ${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv/include
                ${CMAKE_CURRENT_SOURCE_DIR}/deps/flatcc/include
            )
            add_dependencies(${_demo} generate_dsl)
        endforeach()
    else()
        message(STATUS "Skipping log_service demos: RPC DSL codegen unavailable")
    endif()
    
    # Streaming RPC examples
    create_example_target(streaming_demo)

    # Advanced features
    create_example_target(async_await_demo)
    create_example_target(async_chain_demo)
    create_example_target(concurrent_demo)
    create_example_target(primitives_demo)
    create_example_target(rpc_dsl_demo)
    create_example_target(flatbuffers_simple_demo)

    # Loop lifecycle examples
    create_example_target(loop_injection_example)
    
    # UVBus examples
    create_example_target(uvbus_demo)
    create_example_target(uvbus_server_only)
    create_example_target(uvbus_client_only)
    
    # SameLoop examples
    create_example_target(test_sameloop)
    create_example_target(test_sameloop_recursion)
    create_example_target(test_sameloop_stress)
    create_example_target(test_sameloop_multiclient)
    
    # Utility and test examples
    create_example_target(debug_test)
    create_example_target(allocator_demo)
    create_example_target(msgid_demo)
    create_example_target(gateway_demo)
    
    # Note: multi_service_loop_reuse, generated_client_example, 
    # stream_dsl_usage_example, and stream_benchmark_dsl 
    # require additional schema generation, skipped for now
endif()

# Print configuration summary
message(STATUS "=== UVRPC Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Static linking: ${UVRPC_STATIC_LINKING}")
message(STATUS "Build tests: ${UVRPC_BUILD_TESTS}")
message(STATUS "Build examples: ${UVRPC_BUILD_EXAMPLES}")
message(STATUS "=============================")
# Simple test for primitives
add_executable(test_primitives_simple
    examples/test_primitives_simple.c
)
target_link_libraries(test_primitives_simple uvrpc pthread)

# Test Promise combinators
add_executable(test_promise_combinators
    examples/test_promise_combinators.c
)
target_link_libraries(test_promise_combinators uvrpc pthread)

# Test Promise basic
add_executable(test_promise_basic
    examples/test_promise_basic.c
)
target_link_libraries(test_promise_basic uvrpc pthread)

# Test Promise race debug
add_executable(test_promise_race_debug
    examples/test_promise_race_debug.c
)
target_link_libraries(test_promise_race_debug uvrpc pthread)

# Test Promise race simple
add_executable(test_promise_race_simple
    examples/test_promise_race_simple.c
)
target_link_libraries(test_promise_race_simple uvrpc pthread)

# Test Promise race only
add_executable(test_promise_race_only
    examples/test_promise_race_only.c
)
target_link_libraries(test_promise_race_only uvrpc pthread)

# uvasync demo
add_executable(uvasync_demo
    examples/uvasync_demo.c
)
target_link_libraries(uvasync_demo uvrpc pthread)

# uvasync minimal test
add_executable(uvasync_minimal_test
    examples/uvasync_minimal_test.c
)
target_link_libraries(uvasync_minimal_test uvrpc pthread)

# Tests
if(UVRPC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
