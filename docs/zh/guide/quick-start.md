# 快速开始

本指南将帮助您在 5 分钟内开始使用 UVRPC。

## 前置要求

- C 编译器（gcc 或 clang）
- CMake (>= 3.10)
- Make
- Git

## 克隆项目

```bash
git clone --recursive https://github.com/adam-ikari/uvrpc.git
cd uvrpc
```

## 构建

### 使用构建脚本（推荐）

```bash
# 一键构建
./build.sh

# 或使用 Makefile
make
```

### 使用 CMake

```bash
# 配置构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build --config Release

# 安装（可选）
sudo cmake --install build
```

## 运行示例

### 1. 简单的服务器-客户端示例

**启动服务器**（在一个终端）：

```bash
./dist/bin/simple_server tcp://127.0.0.1:5555
```

**运行客户端**（在另一个终端）：

```bash
./dist/bin/simple_client tcp://127.0.0.1:5555
```

### 2. 广播模式示例

**启动发布者**（在一个终端）：

```bash
./dist/bin/broadcast_publisher udp://127.0.0.1:6000
```

**启动订阅者**（在另一个终端）：

```bash
./dist/bin/broadcast_subscriber udp://127.0.0.1:6000
```

### 3. 性能测试

**启动测试服务器**（在一个终端）：

```bash
./dist/bin/benchmark --server -a tcp://127.0.0.1:5555
```

**运行性能测试**（在另一个终端）：

```bash
# 基本测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555

# 指定测试时长
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000

# 多客户端测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -c 10
```

## 下一步

- [API 指南](/zh/guide/api-guide) - 深入了解 UVRPC API
- [性能测试](/zh/guide/benchmark) - 学习如何进行性能测试
- [设计哲学](/zh/guide/design-philosophy) - 了解 UVRPC 的设计原则
- [单线程模型](/zh/guide/single-thread-model) - 理解单线程事件循环模型
- [示例程序](/en/examples/) - 查看更多示例代码

## 获取帮助

如果遇到问题：

1. 查看 [文档](/zh/)
2. 检查 [示例程序](../../examples/)
3. 运行测试：`make test`
4. 提交 Issue：[GitHub Issues](https://github.com/adam-ikari/uvrpc/issues)