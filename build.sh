#!/bin/bash
set -e

echo "Building uvrpc..."

# 检查并初始化 git submodules
echo "Checking git submodules..."

if [ ! -d "deps/libuv" ] || [ ! -d "deps/uvzmq" ] || [ ! -d "deps/uthash" ] || [ ! -d "deps/msgpack-c" ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
fi

# 构建 libuv
echo "Building libuv..."
cd deps/libuv
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
make -j$(nproc)
cd ../../..

# 构建 uvzmq (包括 libzmq)
echo "Building uvzmq..."
cd deps/uvzmq
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DUVZMQ_BUILD_TESTS=OFF -DUVZMQ_BUILD_BENCHMARKS=OFF -DUVZMQ_BUILD_EXAMPLES=OFF
make -j$(nproc)
cd ../../..

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