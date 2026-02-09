#!/bin/bash
set -e

echo "Building uvrpc..."

# 检查依赖
echo "Checking dependencies..."

if ! pkg-config --exists libuv; then
    echo "Error: libuv not found. Install with: sudo apt-get install libuv-dev"
    exit 1
fi

if ! pkg-config --exists libzmq; then
    echo "Error: libzmq not found. Install with: sudo apt-get install libzmq3-dev"
    exit 1
fi

if ! command -v flatc &> /dev/null; then
    echo "Error: flatc not found. Install FlatBuffers from https://github.com/google/flatbuffers"
    exit 1
fi

# 创建构建目录
mkdir -p build
cd build

# 运行 CMake
echo "Running CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 编译
echo "Compiling..."
make -j$(nproc)

echo "Build completed successfully!"
echo ""
echo "Run examples:"
echo "  ./echo_server [bind_addr]  (default: tcp://*:5555)"
echo "  ./echo_client [server_addr] [message]  (default: tcp://127.0.0.1:5555 'Hello, uvrpc!')"