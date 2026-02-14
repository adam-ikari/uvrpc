#!/bin/bash
# Setup UVRPC dependencies using git submodules

set -e

BLUE='[0;34m'
GREEN='[0;32m'
YELLOW='[0;33m'
NC='[0m'

echo "${BLUE}=== UVRPC Dependency Setup ===${NC}"

# Initialize submodules
echo "${YELLOW}Initializing git submodules...${NC}"
git submodule update --init --recursive

# Build dependencies
echo "${YELLOW}Building dependencies...${NC}"

# Build libuv
if [ ! -f "deps/libuv/build/libuv.a" ]; then
    echo "Building libuv..."
    cd deps/libuv
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    make -j$(nproc)
    cd ../../..
else
    echo "libuv already built"
fi

# Build mimalloc
if [ ! -f "deps/mimalloc/lib/libmimalloc.a" ]; then
    echo "Building mimalloc..."
    cd deps/mimalloc
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DMI_BUILD_STATIC=ON -DMI_BUILD_SHARED=OFF
    make -j$(nproc)
    cd ../../..
else
    echo "mimalloc already built"
fi

# Build flatcc
if [ ! -f "deps/flatcc/lib/libflatcc.a" ]; then
    echo "Building flatcc..."
    cd deps/flatcc
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    cd ../../..
else
    echo "flatcc already built"
fi

echo "${GREEN}✓ Dependency setup complete!${NC}"
echo ""
echo "You can now build UVRPC with:"
echo "  make"

# Build gtest
if [ ! -f "deps/gtest/build/lib/libgtest.a" ]; then
    echo "Building gtest..."
    cd deps/gtest
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_GMOCK=OFF
    make -j$(nproc)
    cd ../../..
else
    echo "gtest already built"
fi
