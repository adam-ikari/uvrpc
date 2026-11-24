cmake_minimum_required(VERSION 3.10)
project(benchmark C)

# Release mode flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG -O2)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

# Set C standard (C99, consistent with the main project)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Paths
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DEPS_DIR ${PROJECT_ROOT}/deps)
set(GENERATED_DIR ${PROJECT_ROOT}/generated)

# Include directories
include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/deps/uthash/include
    ${DEPS_DIR}/flatcc/include
    ${DEPS_DIR}/libuv/include
    ${GENERATED_DIR}
)

# Find required packages
find_package(Threads REQUIRED)

# Per-transport round-trip latency + throughput benchmark.
# Usage: ./dist/bin/perf_benchmark [requests] [transport]
#   transport: sameloop | inproc | ipc | udp | tcp  (default: inproc)
add_executable(perf_benchmark
    perf_benchmark.c
)
target_link_libraries(perf_benchmark
    uvrpc
    Threads::Threads
    m
    rt
    dl
)
add_dependencies(perf_benchmark generate_flatcc)

# Same workload, gRPC-style call shape: a blocking unary await instead of the
# explicit UV_RUN_ONCE pump. Kept as a separate binary so perf_benchmark stays
# the stable baseline used by the CI gate.
add_executable(uvrpc_sync_ping_pong
    uvrpc_sync_ping_pong.cc
)
target_link_libraries(uvrpc_sync_ping_pong
    uvrpc
    Threads::Threads
    m
    rt
    dl
)
add_dependencies(uvrpc_sync_ping_pong generate_flatcc)

# Direct-call baseline: measures raw function-call overhead (no transport,
# no serialization) as a reference point for SAMELOOP/INPROC overhead.
add_executable(direct_call_benchmark
    direct_call_benchmark.c
)
target_link_libraries(direct_call_benchmark
    m
    rt
)

# Optional C++ gRPC comparison benchmark.
#
# Off by default: a normal build must not require a gRPC/protobuf toolchain.
# Enable with -DUVRPC_GRPC_BENCHMARK=ON after installing protoc,
# protobuf-compiler-grpc and libgrpc++-dev. No generated stubs are committed;
# protoc produces them at configure time with the plugin version installed on
# this host, so they cannot drift from echo.proto.
option(UVRPC_GRPC_BENCHMARK "Build the C++ gRPC comparison benchmark" OFF)
if(UVRPC_GRPC_BENCHMARK)
    find_package(Protobuf REQUIRED)
    find_package(PkgConfig REQUIRED)
    find_program(GRPC_CPP_PLUGIN NAMES grpc_cpp_plugin REQUIRED)
    pkg_check_modules(GRPCXX REQUIRED IMPORTED_TARGET grpc++)

    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(GRPC_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/grpc_proto)
    set(GRPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/grpc_proto)
    file(MAKE_DIRECTORY ${GRPC_GEN_DIR})
    add_custom_command(
        OUTPUT ${GRPC_GEN_DIR}/echo.pb.cc ${GRPC_GEN_DIR}/echo.pb.h
               ${GRPC_GEN_DIR}/echo.grpc.pb.cc ${GRPC_GEN_DIR}/echo.grpc.pb.h
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${GRPC_GEN_DIR}
                --grpc_out=${GRPC_GEN_DIR}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                -I ${GRPC_PROTO_DIR}
                ${GRPC_PROTO_DIR}/echo.proto
        DEPENDS ${GRPC_PROTO_DIR}/echo.proto
        COMMENT "Generating C++ gRPC stubs from echo.proto"
        VERBATIM
    )

    add_executable(grpc_ping_pong
        grpc_ping_pong.cc
        ${GRPC_GEN_DIR}/echo.pb.cc
        ${GRPC_GEN_DIR}/echo.grpc.pb.cc
    )
    target_include_directories(grpc_ping_pong PRIVATE ${GRPC_GEN_DIR})
    target_link_libraries(grpc_ping_pong
        PRIVATE PkgConfig::GRPCXX ${Protobuf_LIBRARIES} Threads::Threads
    )
endif()
