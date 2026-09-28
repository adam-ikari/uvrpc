#!/bin/bash
# Setup UVRPC dependencies (git submodules + out-of-tree builds).
#
# Contract with cmake/Dependencies.cmake:
#   deps/libuv/build     -> libuv_a.a (static)
#   deps/flatcc          -> bin/flatcc, lib/libflatcc*.a, include/flatcc/ (install prefix)
#   deps/mimalloc/build  -> static mimalloc library
#   deps/gtest/build/lib -> libgtest.a, libgtest_main.a
#
# Usage: ./scripts/setup_deps.sh [clean]

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cd "$(dirname "$0")/.."

if [ "${1:-}" = "clean" ]; then
    echo -e "${YELLOW}Removing dependency build directories...${NC}"
    rm -rf deps/libuv/build deps/mimalloc/build deps/gtest/build
    rm -rf deps/flatcc/bin deps/flatcc/lib deps/flatcc/include deps/flatcc/share deps/flatcc/build
    echo -e "${GREEN}Done.${NC}"
    exit 0
fi

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo -e "${YELLOW}Initializing git submodules...${NC}"
# --force: re-materialise working trees even when HEAD already matches the
# recorded gitlink (a previously aborted checkout leaves an empty tree that a
# plain update would silently skip).
git submodule update --init --recursive --force

# ---------------------------------------------------------------- libuv ------
if [ -f deps/libuv/build/libuv_a.a ] || [ -f deps/libuv/build/libuv.a ] \
   || [ -f deps/libuv/build/lib/libuv.a ]; then
    echo "libuv already built"
else
    echo -e "${YELLOW}Building libuv (static)...${NC}"
    # -fPIC is required: uvrpc_shared links these objects into a shared library.
    cmake -S deps/libuv -B deps/libuv/build \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build deps/libuv/build -j"$NPROC"
fi

# -------------------------------------------------------------- flatcc -------
if [ -f deps/flatcc/bin/flatcc ]; then
    echo "flatcc already built"
else
    echo -e "${YELLOW}Building flatcc (install to deps/flatcc)...${NC}"
    cmake -S deps/flatcc -B deps/flatcc/build \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_FLATCC_TESTS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build deps/flatcc/build -j"$NPROC"
    cmake --install deps/flatcc/build --prefix deps/flatcc
fi

# ------------------------------------------------------------- mimalloc ------
if [ -f deps/mimalloc/build/libmimalloc.a ] \
   || [ -f deps/mimalloc/build/libmimalloc-static.a ] \
   || [ -f deps/mimalloc/out/lib/libmimalloc.a ]; then
    echo "mimalloc already built"
else
    echo -e "${YELLOW}Building mimalloc (static)...${NC}"
    cmake -S deps/mimalloc -B deps/mimalloc/build \
        -DCMAKE_BUILD_TYPE=Release -DMI_BUILD_SHARED=OFF -DMI_BUILD_STATIC=ON \
        -DMI_BUILD_TESTS=OFF -DMI_BUILD_OBJECT=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build deps/mimalloc/build -j"$NPROC"
fi

# ---------------------------------------------------------------- gtest ------
if [ -f deps/gtest/build/lib/libgtest.a ]; then
    echo "gtest already built"
else
    echo -e "${YELLOW}Building gtest...${NC}"
    cmake -S deps/gtest -B deps/gtest/build \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_GMOCK=OFF -DINSTALL_GTEST=OFF
    cmake --build deps/gtest/build -j"$NPROC"
fi

echo -e "${GREEN}✓ Dependency setup complete!${NC}"
echo ""
echo "Next: ./build.sh          (default: Release, mimalloc allocator)"
echo "      ./build.sh release system"
