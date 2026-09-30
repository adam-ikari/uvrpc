# Dependency resolution for UVRPC.
#
# Contract (consumed by the top-level CMakeLists.txt, tests/ and benchmark/):
#   LIBUV_LIBRARIES / LIBUV_INCLUDE_DIR
#   FLATCC_LIBRARIES / FLATCC_INCLUDE_DIR / FLATCC_INSTALL_DIR
#   MIMALLOC_LIBRARIES / MIMALLOC_INCLUDE_DIR   (only when allocator=mimalloc)
#   UTHASH_INCLUDE_DIR
#
# Resolution order per dependency:
#   1. explicit user override (UVRPC_<DEP>_DIR cache variable, if supported)
#   2. vendored submodule build under deps/ (produced by scripts/setup_deps.sh)
#   3. system install fallback (where one plausibly exists)
#
# NOTE: LIBUV_LIBRARIES is intentionally a single static archive file path when
# possible: the uvrpc_merged target runs `ar x ${LIBUV_LIBRARIES}` to fold the
# dependency objects into libuvrpc_full.a.

set(UVRPC_DEPS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../deps)

# ---------------------------------------------------------------- libuv ------
find_library(UVRPC_SYSTEM_LIBUV
    NAMES uv_a uv
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    NO_DEFAULT_PATH)
find_path(UVRPC_SYSTEM_LIBUV_INCLUDE
    NAMES uv.h
    PATHS /usr/include /usr/local/include
    NO_DEFAULT_PATH)

set(LIBUV_INSTALL_DIR "${UVRPC_DEPS_ROOT}/libuv/build"
    CACHE PATH "Prefix containing the built libuv (install tree or CMake build dir)")

if(EXISTS ${LIBUV_INSTALL_DIR}/lib/libuv.a)
    # installed tree: <prefix>/{include/uv.h,lib/libuv.a}
    set(LIBUV_INCLUDE_DIR ${LIBUV_INSTALL_DIR}/include)
    set(LIBUV_LIBRARIES ${LIBUV_INSTALL_DIR}/lib/libuv.a)
elseif(EXISTS ${LIBUV_INSTALL_DIR}/libuv_a.a)
    # in-source CMake build dir (setup_deps.sh): deps/libuv/build/libuv_a.a
    set(LIBUV_INCLUDE_DIR ${UVRPC_DEPS_ROOT}/libuv/include)
    set(LIBUV_LIBRARIES ${LIBUV_INSTALL_DIR}/libuv_a.a)
elseif(EXISTS ${LIBUV_INSTALL_DIR}/libuv.a)
    # libuv >= 1.48 names its static archive libuv.a in the build dir
    set(LIBUV_INCLUDE_DIR ${UVRPC_DEPS_ROOT}/libuv/include)
    set(LIBUV_LIBRARIES ${LIBUV_INSTALL_DIR}/libuv.a)
elseif(EXISTS ${LIBUV_INSTALL_DIR}/lib/libuv_a.a)
    set(LIBUV_INCLUDE_DIR ${UVRPC_DEPS_ROOT}/libuv/include)
    set(LIBUV_LIBRARIES ${LIBUV_INSTALL_DIR}/lib/libuv_a.a)
elseif(UVRPC_SYSTEM_LIBUV AND UVRPC_SYSTEM_LIBUV_INCLUDE)
    get_filename_component(_uv_libdir ${UVRPC_SYSTEM_LIBUV} DIRECTORY)
    set(LIBUV_INCLUDE_DIR ${UVRPC_SYSTEM_LIBUV_INCLUDE})
    set(LIBUV_LIBRARIES ${UVRPC_SYSTEM_LIBUV})
    message(STATUS "libuv: using system build ${UVRPC_SYSTEM_LIBUV}")
else()
    message(FATAL_ERROR
        "libuv not found. Run ./scripts/setup_deps.sh, or point -DLIBUV_INSTALL_DIR=<prefix> "
        "at a built libuv (need lib/libuv.a or libuv_a.a + include/uv.h).")
endif()
if(NOT EXISTS ${LIBUV_LIBRARIES})
    message(FATAL_ERROR "libuv library missing: ${LIBUV_LIBRARIES}")
endif()
message(STATUS "libuv: ${LIBUV_LIBRARIES}")

# -------------------------------------------------------------- flatcc -------
set(FLATCC_INSTALL_DIR "${UVRPC_DEPS_ROOT}/flatcc"
    CACHE PATH "Prefix where flatcc was installed by scripts/setup_deps.sh")

find_path(FLATCC_INCLUDE_DIR
    NAMES flatcc/flatcc_builder.h
    PATHS ${FLATCC_INSTALL_DIR}/include
          ${UVRPC_DEPS_ROOT}/flatcc/include
          ${UVRPC_DEPS_ROOT}/flatcc/lib/include
          /usr/include /usr/local/include)

find_library(FLATCC_BASE_LIBRARY
    NAMES flatccbase flatcc_base flatcc
    PATHS ${FLATCC_INSTALL_DIR}/lib
          ${FLATCC_INSTALL_DIR}/lib64
          ${UVRPC_DEPS_ROOT}/flatcc/build/lib
          ${UVRPC_DEPS_ROOT}/flatcc/build
          /usr/lib /usr/local/lib)

# The runtime archive holds flatcc_verify_field() and friends. Frame decoding
# needs them: the generated reader trusts a buffer once it looks like a root,
# so without the verifier a peer can make the server read out of bounds with a
# single malformed frame.
find_library(FLATCC_RT_LIBRARY
    NAMES flatccrt
    PATHS ${FLATCC_INSTALL_DIR}/lib
          ${FLATCC_INSTALL_DIR}/lib64
          ${UVRPC_DEPS_ROOT}/flatcc/build/lib
          ${UVRPC_DEPS_ROOT}/flatcc/build
          /usr/lib /usr/local/lib)

if(NOT FLATCC_RT_LIBRARY)
    message(FATAL_ERROR
        "flatcc runtime library (libflatccrt) not found. Frame verification "
        "cannot be linked. Run ./scripts/setup_deps.sh to rebuild flatcc.")
endif()

find_library(FLATCC_SUPPORT_LIBRARY
    NAMES flatccsupport flatcc_support
    PATHS ${FLATCC_INSTALL_DIR}/lib
          ${FLATCC_INSTALL_DIR}/lib64
          ${UVRPC_DEPS_ROOT}/flatcc/build/lib
          ${UVRPC_DEPS_ROOT}/flatcc/build
          /usr/lib /usr/local/lib)

if(FLATCC_SUPPORT_LIBRARY)
    # flatcc < 0.6 split the runtime into a second archive; uvrpc_merged can
    # only fold a single archive, so prefer linking both via a search dir.
    message(WARNING
        "flatcc support library found (${FLATCC_SUPPORT_LIBRARY}) but is not "
        "linked -- uvrpc_merged requires a single FLATCC_LIBRARIES archive.")
endif()

if(NOT FLATCC_INCLUDE_DIR OR NOT FLATCC_BASE_LIBRARY)
    message(FATAL_ERROR
        "flatcc not found (include dir: '${FLATCC_INCLUDE_DIR}', base lib: '${FLATCC_BASE_LIBRARY}'). "
        "Run ./scripts/setup_deps.sh to build and install it under deps/flatcc.")
endif()

# Single archive on purpose: the uvrpc_merged target folds ${FLATCC_LIBRARIES}
# into libuvrpc_full.a, and flatcc >= 0.6 installs one libflatcc.a that already
# contains builder + reader support. (libflatccrt.a is reflection support, which
# uvrpc does not use.)
set(FLATCC_LIBRARIES ${FLATCC_BASE_LIBRARY})
message(STATUS "flatcc: ${FLATCC_LIBRARIES}")

# ------------------------------------------------------------- mimalloc ------
if(UVRPC_ALLOCATOR_DEFAULT STREQUAL "mimalloc")
    set(MIMALLOC_INSTALL_DIR "${UVRPC_DEPS_ROOT}/mimalloc"
        CACHE PATH "Prefix containing the built mimalloc (install tree or CMake build dir)")

    find_path(MIMALLOC_INCLUDE_DIR
        NAMES mimalloc.h
        PATHS ${MIMALLOC_INSTALL_DIR}/include
              ${UVRPC_DEPS_ROOT}/mimalloc/include)

    find_library(MIMALLOC_BASE_LIBRARY
        NAMES mimalloc mimalloc-static
        PATHS ${MIMALLOC_INSTALL_DIR}/lib
              ${MIMALLOC_INSTALL_DIR}/lib64
              ${MIMALLOC_INSTALL_DIR}/out/lib
              ${UVRPC_DEPS_ROOT}/mimalloc/build)

    if(NOT MIMALLOC_INCLUDE_DIR OR NOT MIMALLOC_BASE_LIBRARY)
        message(FATAL_ERROR
            "mimalloc not found (include: '${MIMALLOC_INCLUDE_DIR}', lib: '${MIMALLOC_BASE_LIBRARY}'). "
            "Run: git submodule update --init deps/mimalloc && ./scripts/setup_deps.sh "
            "or configure with -DUVRPC_ALLOCATOR_DEFAULT=system.")
    endif()
    set(MIMALLOC_LIBRARIES ${MIMALLOC_BASE_LIBRARY})
    message(STATUS "mimalloc: ${MIMALLOC_LIBRARIES}")
endif()

# --------------------------------------------------------------- uthash ------
find_path(UTHASH_INCLUDE_DIR
    NAMES uthash.h
    PATHS ${UVRPC_DEPS_ROOT}/uthash/src
          ${UVRPC_DEPS_ROOT}/uthash/include
          /usr/include /usr/local/include)

if(NOT UTHASH_INCLUDE_DIR)
    message(FATAL_ERROR "uthash.h not found under deps/uthash. Run ./scripts/setup_deps.sh")
endif()
message(STATUS "uthash: ${UTHASH_INCLUDE_DIR}")
