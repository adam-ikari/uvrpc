#!/bin/bash
# UVRPC Build Script
# Usage: ./build.sh [clean|debug|release|system|mimalloc]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
ALLOCATOR="mimalloc"
ACTION="build"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        clean)
            ACTION="clean"
            shift
            ;;
        debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        release)
            BUILD_TYPE="Release"
            shift
            ;;
        system)
            ALLOCATOR="system"
            shift
            ;;
        mimalloc)
            ALLOCATOR="mimalloc"
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [clean|debug|release|system|mimalloc]"
            exit 1
            ;;
    esac
done

# Print configuration
echo -e "${GREEN}=== UVRPC Build Configuration ===${NC}"
echo "Build Type: $BUILD_TYPE"
echo "Allocator: $ALLOCATOR"
echo "Build Dir: build/"
echo "Dist Dir: dist/"
echo -e "${GREEN}================================${NC}"

# Clean if requested
if [ "$ACTION" = "clean" ]; then
    echo -e "${YELLOW}Cleaning build and dist directories...${NC}"
    rm -rf build/ dist/
    echo -e "${GREEN}Clean complete!${NC}"
    exit 0
fi

# Configure CMake
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake -B build \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DUVRPC_ALLOCATOR_DEFAULT=$ALLOCATOR \
    -S .

# Build
echo -e "${YELLOW}Building...${NC}"
cmake --build build --config $BUILD_TYPE

# Summary
echo -e "${GREEN}=== Build Complete ===${NC}"
echo "Library: dist/lib/libuvrpc.a"
echo "Binaries: dist/bin/"
echo -e "${GREEN}====================${NC}"
