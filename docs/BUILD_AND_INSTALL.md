# UVRPC 构建和安装指南

## 系统要求

### 操作系统
- Linux (推荐)
- macOS
- Windows (通过 WSL)

### 编译器
- GCC >= 4.9
- Clang >= 3.5
- MSVC >= 2015 (Windows)

### 构建工具
- CMake >= 3.15
- Make 或 Ninja

## 依赖

### 必需依赖
- libuv >= 1.0
- FlatCC >= 0.6.0
- uthash

### 可选依赖
- mimalloc >= 1.0 (高性能内存分配器)
- gtest >= 1.10 (单元测试)

## 依赖安装

### Ubuntu/Debian

```bash
# 安装编译工具
sudo apt-get update
sudo apt-get install -y build-essential cmake git

# 安装 libuv
sudo apt-get install -y libuv1-dev

# 克隆项目（包含其他依赖）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 设置其他依赖
./scripts/setup_deps.sh
```

### macOS

```bash
# 安装 Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake libuv

# 克隆项目（包含其他依赖）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 设置其他依赖
./scripts/setup_deps.sh
```

### 从源码编译依赖

如果预编译依赖不可用，可以从源码编译：

```bash
# libuv
cd deps/libuv
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install
cd ../..

# mimalloc
cd deps/mimalloc
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install
cd ../..

# FlatCC
cd deps/flatcc
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install
cd ../..
```

## 构建选项

### 使用构建脚本（推荐）

```bash
# 默认构建（Release 模式，mimalloc）
./build.sh

# Debug 模式
./build.sh debug

# 使用系统分配器
./build.sh release system

# 使用自定义分配器（需要实现）
./build.sh release custom
```

### 使用 CMake

```bash
# 创建构建目录
mkdir build && cd build

# 配置（默认 Release 模式，mimalloc）
cmake ..

# Debug 模式
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 使用系统分配器
cmake -DUVRPC_ALLOCATOR=SYSTEM ..

# 自定义安装前缀
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..

# 编译
make -j$(nproc)

# 运行测试
make test

# 安装
sudo make install
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|-----|-------|------|
| CMAKE_BUILD_TYPE | Release | 构建类型 (Debug/Release/RelWithDebInfo) |
| UVRPC_ALLOCATOR | MIMALLOC | 内存分配器 (SYSTEM/MIMALLOC/CUSTOM) |
| BUILD_TESTING | ON | 是否构建测试 |
| BUILD_EXAMPLES | ON | 是否构建示例 |
| BUILD_BENCHMARK | ON | 是否构建基准测试 |
| CMAKE_INSTALL_PREFIX | /usr/local | 安装前缀 |

## 构建产物

### 静态库
- `dist/lib/libuvrpc.a` - UVRPC 静态库

### 可执行文件
- `dist/bin/simple_server` - 简单服务端示例
- `dist/bin/simple_client` - 简单客户端示例
- `dist/bin/uvrpc_tests` - 单元测试
- `dist/bin/test_tcp` - TCP 集成测试
- `dist/bin/uvrpc_benchmark` - 性能基准测试

### 头文件
- `include/uvrpc.h` - 主头文件
- `include/uvrpc_allocator.h` - 内存分配器头文件

## 运行测试

### 单元测试

```bash
# 运行所有单元测试
./dist/bin/uvrpc_tests

# 运行特定测试
./dist/bin/uvrpc_tests --gtest_filter=AllocatorTest.*

# 输出详细日志
./dist/bin/uvrpc_tests --gtest_verbose
```

### 集成测试

```bash
# TCP 集成测试
./dist/bin/test_tcp

# UDP 集成测试（需要实现）
./dist/bin/test_udp

# IPC 集成测试（需要实现）
./dist/bin/test_ipc
```

### 性能测试

```bash
# 运行基准测试
./dist/bin/uvrpc_benchmark

# 指定参数
./dist/bin/uvrpc_benchmark --address tcp://127.0.0.1:5555 --clients 10 --requests 10000
```

## 性能测试

### 吞吐量测试

```bash
# 启动服务器
./dist/bin/perf_server

# 运行客户端（默认模式）
./dist/bin/perf_client 127.0.0.1:5555 10000

# 使用不同迭代次数
./dist/bin/perf_client 127.0.0.1:5555 100000
```

### 性能模式对比

```bash
# 启动服务器
./dist/bin/perf_server &

# 测试低延迟模式
./dist/bin/perf_client 127.0.0.1:5555 10000

# 测试高吞吐模式（需要修改客户端代码设置性能模式）
```

### 延迟测试

```bash
# 启动服务器
./dist/bin/perf_server &

# 运行延迟测试
./dist/bin/simple_latency_test
```

## 安装

### 系统安装

```bash
cd build
sudo make install
```

这会安装：
- 库文件到 `/usr/local/lib/`
- 头文件到 `/usr/local/include/`
- 可执行文件到 `/usr/local/bin/`

### 自定义安装

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
make
make install
```

### 卸载

```bash
cd build
sudo make uninstall
```

## 交叉编译

### 交叉编译到 ARM

```bash
# 安装交叉编译工具链
sudo apt-get install -y gcc-arm-linux-gnueabihf

# 配置交叉编译
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake ..
make
```

### 交叉编译到 Windows

```bash
# 安装 MinGW
sudo apt-get install -y mingw-w64

# 配置交叉编译
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake ..
make
```

## Docker 构建

### Dockerfile

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libuv1-dev

WORKDIR /app
COPY . .
RUN ./scripts/setup_deps.sh && ./build.sh
```

### 构建镜像

```bash
docker build -t uvrpc:latest .
```

### 运行容器

```bash
docker run -it uvrpc:latest ./dist/bin/uvrpc_tests
```

## 故障排除

### 编译错误

**问题**：找不到 libuv 头文件

**解决**：
```bash
# Ubuntu/Debian
sudo apt-get install libuv1-dev

# macOS
brew install libuv
```

**问题**：找不到 mimalloc

**解决**：
```bash
cd deps/mimalloc
mkdir build && cd build
cmake .. && make
```

### 链接错误

**问题**：找不到 libuv 库

**解决**：
```bash
cmake -DLIBUV_ROOT=/path/to/libuv ..
```

**问题**：找不到 mimalloc 库

**解决**：
```bash
cmake -DMIMALLOC_ROOT=/path/to/mimalloc ..
```

### 运行时错误

**问题**：找不到共享库

**解决**：
```bash
# 添加库路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 或配置动态链接器
sudo ldconfig
```

**问题**：测试超时

**解决**：
```bash
# 增加测试超时时间
ctest --timeout 300
```

## 清理

```bash
# 清理构建产物
make clean

# 清理所有（包括 CMake 缓存）
rm -rf build

# 清理依赖构建产物
rm -rf deps/*/build
```

## 性能优化

### Release 构建

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 使用 LTO (Link Time Optimization)

```bash
cmake -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ..
make -j$(nproc)
```

### 使用 PGO (Profile Guided Optimization)

```bash
# 生成配置文件
cmake -DCMAKE_BUILD_TYPE=Release -DUVRPC_ENABLE_PGO=GENERATE ..
make -j$(nproc)
./dist/bin/uvrpc_benchmark

# 使用配置文件优化
cmake -DCMAKE_BUILD_TYPE=Release -DUVRPC_ENABLE_PGO=USE ..
make -j$(nproc)
```

## CI/CD 集成

### GitHub Actions

```yaml
name: Build and Test

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get update && sudo apt-get install -y build-essential cmake libuv1-dev
      - name: Setup dependencies
        run: ./scripts/setup_deps.sh
      - name: Build
        run: ./build.sh
      - name: Test
        run: ./dist/bin/uvrpc_tests
```

### GitLab CI

```yaml
stages:
  - build
  - test

build:
  stage: build
  script:
    - ./scripts/setup_deps.sh
    - ./build.sh
  artifacts:
    paths:
      - dist/

test:
  stage: test
  script:
    - ./dist/bin/uvrpc_tests
```

## 获取帮助

如果您在构建或安装过程中遇到问题：

1. 查看本文档的故障排除部分
2. 检查 GitHub Issues：https://github.com/your-org/uvrpc/issues
3. 提交新的 Issue 并提供详细的错误信息