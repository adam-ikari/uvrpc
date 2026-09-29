# UVRPC 构建和安装指南

## 系统要求

### 操作系统
- Linux（推荐，CI 全量覆盖）
- macOS
- Windows：只支持 WSL。源码里没有任何 `#ifdef _WIN32` 分支，CMake 用的是 GCC 风格的
  `-static -pthread` 链接标志，也没有 MSVC 的 CI job，别按原生 Windows 构建规划。

### 编译器
- GCC >= 4.9
- Clang >= 3.5

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
git clone --recursive https://github.com/adam-ikari/uvrpc.git
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
git clone --recursive https://github.com/adam-ikari/uvrpc.git
cd uvrpc

# 设置其他依赖
./scripts/setup_deps.sh
```

### 从源码编译依赖

`scripts/setup_deps.sh` 就是官方路径，它按 `cmake/Dependencies.cmake` 期望的目录布局
构建每个依赖：libuv / gtest / mimalloc 只 build 不 install（产物留在 `deps/<x>/build/`），
flatcc 额外 `--prefix deps/flatcc` 安装（要提供 `bin/flatcc`）。手工执行等价步骤时
必须保持同样的落盘位置，否则 configure 找不到：

```bash
git submodule update --init --recursive --force

cmake -S deps/libuv -B deps/libuv/build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build deps/libuv/build -j

cmake -S deps/flatcc -B deps/flatcc/build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_FLATCC_TESTS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build deps/flatcc/build -j
cmake --install deps/flatcc/build --prefix deps/flatcc

cmake -S deps/mimalloc -B deps/mimalloc/build \
    -DCMAKE_BUILD_TYPE=Release -DMI_BUILD_SHARED=OFF -DMI_BUILD_STATIC=ON \
    -DMI_BUILD_TESTS=OFF -DMI_BUILD_OBJECT=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build deps/mimalloc/build -j

cmake -S deps/gtest -B deps/gtest/build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_GMOCK=OFF -DINSTALL_GTEST=OFF
cmake --build deps/gtest/build -j
```

`-DCMAKE_POSITION_INDEPENDENT_CODE=ON` 不是可省的：`uvrpc_shared` 要把这些目标文件
链进共享库。

覆盖查找位置用这些缓存变量：`LIBUV_INSTALL_DIR`、`FLATCC_INSTALL_DIR`、
`MIMALLOC_INSTALL_DIR`。

## 构建选项

### 使用构建脚本（推荐）

依赖必须先构建：`cmake` 找不到系统 flatcc，所以第一步固定是 `setup_deps.sh`。

```bash
./scripts/setup_deps.sh           # 拉子模块并构建 libuv/flatcc/mimalloc/gtest

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
./scripts/setup_deps.sh    # 先构建依赖，否则 configure 在 flatcc 处失败

# 创建构建目录
mkdir build && cd build

# 配置（默认 Release 模式，mimalloc）
cmake ..

# Debug 模式
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 使用系统分配器
cmake -DUVRPC_ALLOCATOR_DEFAULT=system ..

# 自定义安装前缀
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..

# 编译
make -j$(nproc)

# 运行测试（测试默认不构建，需要 -DUVRPC_BUILD_TESTS=ON）
ctest --output-on-failure

# 安装
sudo make install
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|-----|-------|------|
| CMAKE_BUILD_TYPE | Release | 构建类型 (Debug/Release/RelWithDebInfo) |
| UVRPC_ALLOCATOR_DEFAULT | mimalloc | 内存分配器 (system/mimalloc/custom) |
| UVRPC_BUILD_TESTS | OFF | 是否构建测试 |
| UVRPC_BUILD_EXAMPLES | ON | 是否构建示例 |
| UVRPC_DEBUG_LOGGING | OFF | 是否启用调试日志 |
| CMAKE_INSTALL_PREFIX | /usr/local | 安装前缀 |

## 构建产物

### 静态库
- `dist/lib/libuvrpc.a` - UVRPC 静态库

### 可执行文件
- `dist/bin/simple_server` - 简单服务端示例
- `dist/bin/simple_client` - 简单客户端示例
- `dist/bin/uvrpc_tests` - 单元测试（需 `-DUVRPC_BUILD_TESTS=ON` 才会构建）
- `dist/bin/test_tcp` - TCP 集成测试
- `dist/bin/perf_benchmark` - 性能基准测试

### 头文件
- `include/uvrpc.h` - 主头文件
- `include/uvrpc_allocator.h` - 内存分配器头文件

## 运行测试

### 单元测试

```bash
# 运行所有单元测试
./dist/bin/uvrpc_tests

# 按用例名过滤
./dist/bin/uvrpc_tests --gtest_filter=AllocatorTest.*

# 列出全部用例名（gtest 没有 --gtest_verbose）
./dist/bin/uvrpc_tests --gtest_list_tests

# UVRPC 自身的调试日志由构建选项决定，不是运行时开关
# cmake -DUVRPC_DEBUG_LOGGING=ON ..
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

使用 `perf_benchmark` 测量各传输的往返延迟与吞吐量（顺序 ping-pong，单线程）：

```bash
# 构建（Release，关闭调试日志以保证计时准确）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUVRPC_DEBUG_LOGGING=OFF
cmake --build build --target perf_benchmark

# 运行各传输
./dist/bin/perf_benchmark 100000 sameloop
./dist/bin/perf_benchmark 100000 inproc
./dist/bin/perf_benchmark 100000 ipc
./dist/bin/perf_benchmark 100000 udp
./dist/bin/perf_benchmark 100000 tcp
```

### 性能参考

顺序 ping-pong，8 字节负载，Release 构建，单线程，由 `Benchmark` 工作流在 GitHub
Actions runner 上实测（绝对值随主机而变，完整表见 [Benchmark 指南](/guide/benchmark)）：

| 传输层 | 往返延迟 | 顺序吞吐量 (1/延迟) | 适用场景 |
|--------|----------|---------------------|----------|
| SAMELOOP / INPROC | ~1.0 µs | ~1,000,000 req/s | 进程内零拷贝（最快）|
| IPC | ~10.8 µs | ~93,000 req/s | 本地进程间（Unix 套接字）|
| UDP | ~20.1 µs | ~50,000 req/s | 高吞吐、可丢包 |
| TCP | ~20.1 µs | ~50,000 req/s | 可靠网络 RPC |

> "吞吐量"为顺序往返延迟的倒数（单请求在途），非流水线吞吐。实际性能取决于硬件。

详见 [Benchmark 指南](/guide/benchmark)。

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
cmake -DLIBUV_INSTALL_DIR=/path/to/libuv/build ..
```

**问题**：找不到 mimalloc 库

**解决**：
```bash
cmake -DMIMALLOC_INSTALL_DIR=/path/to/mimalloc ..
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

### 关于 PGO

项目**没有** `UVRPC_ENABLE_PGO` 这个选项，CMake 里也不存在 profile 生成/复用的分支。
PGO 只能靠 GCC/Clang 自己的标志手工做两遍，例如：

```bash
# 第一遍：插桩生成 profile
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-fprofile-instr-generate' ..
make -j$(nproc)
LLVM_PROFILE_FILE=uvrpc.profraw ./dist/bin/perf_benchmark 100000 inproc

# 第二遍：用 profile 优化重建
llvm-profdata merge -sparse uvrpc.profraw -o uvrpc.profdata
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-fprofile-use=uvrpc.profdata' ..
make -j$(nproc)
```

GCC 用 `-fprofile-generate` / `-fprofile-use` 对应。这条路径没有被 CI 覆盖，属于自行
承担风险的优化手段，别把它当官方构建配置写进脚本。

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
2. 检查 GitHub Issues：https://github.com/adam-ikari/uvrpc/issues
3. 提交新的 Issue 并提供详细的错误信息