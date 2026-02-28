---
layout: home

hero:
  name: "UVRPC"
  text: "超快速 RPC 框架"
  tagline: "零线程，零锁，零全局变量"
  actions:
    - theme: brand
      text: 开始使用
      link: /en/quick-start
    - theme: alt
      text: GitHub
      link: https://github.com/adam-ikari/uvrpc

features:
  - title: 🚀 超快速
    details: "基于 libuv 事件循环和 FlatBuffers 序列化，INPROC 传输可达 125,000+ ops/s。"
  - title: 🎯 极简设计
    details: "零线程、零锁、零全局变量。所有 I/O 由 libuv 事件循环管理。"
  - title: 🔌 多种传输
    details: "支持 TCP、UDP、IPC 和 INPROC 传输，统一 API。"
  - title: 📦 零拷贝
    details: "FlatBuffers 二进制序列化，最小化内存拷贝，达到最大性能。"
  - title: 🔄 循环注入
    details: "支持自定义 libuv loop，多实例独立或共享循环。"
  - title: 📚 类型安全
    details: "FlatBuffers DSL 生成类型安全的 API，编译时检查。"

---

::: tip 欢迎使用 UVRPC
UVRPC 是一个极简、高性能的 RPC 框架，基于 libuv 事件循环和 FlatBuffers 序列化。
:::

## 快速开始

\`\`\`bash
# 克隆仓库
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc

# 构建
cmake -S . -B build
cmake --build build

# 运行示例
./dist/bin/simple_client
\`\`\`

## 性能

| 传输层 | 吞吐量 (ops/s) | 平均延迟 |
|--------|----------------|----------|
| INPROC | 125,000+ | 0.03 ms |
| IPC | 91,895 | 0.10 ms |
| UDP | 91,685 | 0.15 ms |
| TCP | 86,930 | 0.18 ms |

### Fork/Exec 架构性能 (2026-02-28)

| 传输层 | 客户端数 | 请求数 | 吞吐量 (ops/s) | 平均延迟 |
|--------|---------|--------|----------------|----------|
| IPC | 10 | 10,000 | 83,467 | 0.012 ms |
| TCP | 10 | 10,000 | 52,103 | 0.019 ms |
| UDP | 10 | 10,000 | 72,244 | 0.014 ms |

Fork/Exec 架构确保了网络传输的正确进程隔离和 libuv fork 兼容性。

查看 [性能测试报告](/en/) 了解更多详情。