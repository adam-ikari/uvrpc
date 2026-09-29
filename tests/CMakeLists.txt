# UVRPC Unit Tests CMakeLists.txt

# Enable testing
enable_testing()

# GTest paths (built from submodule)
set(GTEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../deps/gtest)
set(GTEST_INCLUDE_DIR ${GTEST_ROOT}/googletest/include)
set(GTEST_LIBRARY_DIR ${GTEST_ROOT}/build/lib)

# Find GTest
set(GTEST_LIBRARY ${GTEST_LIBRARY_DIR}/libgtest.a)
set(GTEST_MAIN_LIBRARY ${GTEST_LIBRARY_DIR}/libgtest_main.a)

# Verify GTest exists
if(NOT EXISTS ${GTEST_LIBRARY})
    message(FATAL_ERROR "GTest library not found at ${GTEST_LIBRARY}. Please run ./scripts/setup_deps.sh first.")
endif()

# Unit tests (no libuv dependency)
add_executable(uvrpc_tests
    unit/test_allocator.cpp
    unit/test_flatbuffers.cpp
    unit/test_msgid.cpp
    unit/test_client.cpp
    unit/test_config.cpp
)

# Integration tests (require libuv)
add_executable(uvrpc_integration_tests
    unit/test_async.cpp
    unit/test_uvbus.cpp
    unit/test_context.cpp
    unit/test_server_context.cpp
    unit/test_client_context.cpp
    unit/test_client_retry.cpp
    unit/test_client_concurrency.cpp
    unit/test_server_stats.cpp
)

# Link against UVRPC library
target_link_libraries(uvrpc_tests
    uvrpc
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    pthread
    m
    rt
)

# Link integration tests against UVRPC library
target_link_libraries(uvrpc_integration_tests
    uvrpc
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    pthread
    m
    rt
)

# Add include directories for unit tests
target_include_directories(uvrpc_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
    ${GTEST_INCLUDE_DIR}
)

# Add include directories for integration tests
target_include_directories(uvrpc_integration_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
    ${GTEST_INCLUDE_DIR}
)

# Register unit tests with CTest
include(GoogleTest)
gtest_discover_tests(uvrpc_tests PROPERTIES TIMEOUT 60)

# Register integration tests with CTest
gtest_discover_tests(uvrpc_integration_tests PROPERTIES TIMEOUT 60)

# UVBus C tests (pure C, no GTest dependency)
add_executable(uvbus_test
    uvbus_test.c
)

target_link_libraries(uvbus_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)

# UVBus simple test
add_executable(uvbus_simple_test
    uvbus_simple_test.c
)

target_link_libraries(uvbus_simple_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_simple_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)

# UVBus broadcast test
add_executable(uvbus_broadcast_test
    uvbus_broadcast_test.c
)

target_link_libraries(uvbus_broadcast_test
    uvrpc
    pthread
    m
    rt
)

target_include_directories(uvbus_broadcast_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/flatcc/include
)

add_test(NAME uvbus_broadcast_tests COMMAND uvbus_broadcast_test)
set_tests_properties(uvbus_broadcast_tests PROPERTIES TIMEOUT 60)

# Register C tests with CTest
add_test(NAME uvbus_unit_tests COMMAND uvbus_test)
add_test(NAME uvbus_simple_test COMMAND uvbus_simple_test)
set_tests_properties(uvbus_unit_tests uvbus_simple_test PROPERTIES TIMEOUT 60)

# Allocator ownership test. Only meaningful in custom-allocator builds: with the
# system or mimalloc allocator uvrpc_free() bottoms out in free(), so a flatcc
# buffer released through it is indistinguishable from a legitimate one.
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "custom")
    add_executable(allocator_ownership_test
        allocator_ownership_test.c
    )

    target_link_libraries(allocator_ownership_test
        uvrpc
        pthread
        m
        rt
    )

    target_include_directories(allocator_ownership_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/flatcc/include
    )

    add_test(NAME allocator_ownership COMMAND allocator_ownership_test)
    set_tests_properties(allocator_ownership PROPERTIES TIMEOUT 60)
endif()

# DSL generator round trip. The generator is a Python tool and is not part of
# the library build, so this test only exists where it can run: with python3 and
# jinja2 present it drives uvrpcc.py over schema/log_service.fbs and checks that
# the code it emits encodes and decodes a struct array. Without them the test is
# skipped rather than failing, so a plain C build still works.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(_dsl_python "")
# find_package picks whatever Python3_EXECUTABLE resolves to, which on a
# developer machine is often a user-local interpreter without jinja2 while the
# system one has it. Probe the candidates and take the first that can import
# the dependency, so the test runs wherever the generator can actually run.
set(_dsl_candidates ${Python3_EXECUTABLE} /usr/bin/python3 python3)
foreach(_candidate IN LISTS _dsl_candidates)
    if(_candidate AND NOT _dsl_python)
        execute_process(
            COMMAND ${_candidate} -c "import jinja2"
            RESULT_VARIABLE _dsl_rc
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_dsl_rc EQUAL 0)
            set(_dsl_python ${_candidate})
        endif()
    endif()
endforeach()
if(_dsl_python)
    set(_dsl_codegen_ok TRUE)
    message(STATUS "DSL codegen test: using ${_dsl_python}")
endif()

if(_dsl_codegen_ok)
    set(_dsl_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/dsl_gen)
    set(_dsl_gen_ns log_logservice)
    file(GLOB _dsl_templates ${CMAKE_CURRENT_SOURCE_DIR}/../tools/templates/*.j2)

    add_custom_command(
        OUTPUT ${_dsl_gen_dir}/${_dsl_gen_ns}_client.c
               ${_dsl_gen_dir}/${_dsl_gen_ns}_rpc_common.c
               ${_dsl_gen_dir}/${_dsl_gen_ns}_server_stub.c
               ${_dsl_gen_dir}/${_dsl_gen_ns}_api.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_dsl_gen_dir}
        COMMAND ${_dsl_python} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/uvrpcc.py
                --flatcc ${FLATCC_COMPILER}
                ${CMAKE_CURRENT_SOURCE_DIR}/../schema/log_service.fbs
                -o ${_dsl_gen_dir}
        # The templates decide what the generated code does, so an edit to one
        # of them must rerun the generator -- otherwise the test would keep
        # asserting against stale output.
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/uvrpcc.py
                ${CMAKE_CURRENT_SOURCE_DIR}/../schema/log_service.fbs
                ${_dsl_templates}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        COMMENT "Generating RPC DSL code from log_service.fbs"
        VERBATIM
    )

    add_executable(dsl_codegen_test
        dsl_codegen_test.c
        ${_dsl_gen_dir}/${_dsl_gen_ns}_client.c
        ${_dsl_gen_dir}/${_dsl_gen_ns}_rpc_common.c
        ${_dsl_gen_dir}/${_dsl_gen_ns}_server_stub.c
    )

    target_link_libraries(dsl_codegen_test
        uvrpc
        pthread
        m
        rt
    )

    target_include_directories(dsl_codegen_test
        PRIVATE
        ${_dsl_gen_dir}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/libuv/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/flatcc/include
    )

    add_test(NAME dsl_codegen COMMAND dsl_codegen_test)
    set_tests_properties(dsl_codegen PROPERTIES TIMEOUT 120)
else()
    message(STATUS "Skipping dsl_codegen test: python3 with jinja2 not available")
endif()

# Integration tests
add_subdirectory(integration)
