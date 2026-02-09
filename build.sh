#!/bin/bash
set -e

echo "Building uvrpc..."

# 检查并初始化 git submodules
echo "Checking git submodules..."

if [ ! -d "deps/libuv" ] || [ ! -d "deps/zeromq" ] || [ ! -d "deps/uvzmq" ] || [ ! -d "deps/uthash" ] || [ ! -d "deps/flatbuffers" ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
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