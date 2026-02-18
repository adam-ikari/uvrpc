# UVRPC 文档索引

UVRPC 文档中心，包含中英文版本的完整文档。

## 文档结构

```
docs/
├── en/                    # 英文文档
│   ├── README.md          # 项目介绍（英文）
│   ├── README_EN.md       # 项目介绍（中文）
│   ├── QUICK_START.md     # 快速入门
│   ├── API_GUIDE.md       # API 指南
│   ├── API_REFERENCE.md   # API 参考
│   ├── BUILD_AND_INSTALL.md # 构建与安装
│   ├── DESIGN_PHILOSOPHY.md # 设计哲学
│   ├── MIGRATION_GUIDE.md # 迁移指南
│   ├── CODING_STANDARDS.md # 编码规范
│   ├── DOXYGEN_EXAMPLES.md # Doxygen 示例
│   └── doxygen/           # Doxygen 生成的 HTML 文档
├── zh/                    # 中文文档
│   └── README.md          # 本文件
└── README.md              # 文档索引
```

## 英文文档 (English Documentation)

所有英文文档位于 [en/](../en/) 目录：

### 核心文档
- [README.md](../en/README.md) - 项目介绍和概述
- [README_EN.md](../en/README_EN.md) - 项目介绍（英文版）
- [QUICK_START.md](../en/QUICK_START.md) - 5分钟快速入门教程
- [API_GUIDE.md](../en/API_GUIDE.md) - 完整的 API 使用指南
- [API_REFERENCE.md](../en/API_REFERENCE.md) - API 函数参考手册

### 开发文档
- [BUILD_AND_INSTALL.md](../en/BUILD_AND_INSTALL.md) - 构建和安装说明
- [DESIGN_PHILOSOPHY.md](../en/DESIGN_PHILOSOPHY.md) - 架构设计和设计哲学
- [MIGRATION_GUIDE.md](../en/MIGRATION_GUIDE.md) - 版本迁移指南
- [SINGLE_THREAD_MODEL.md](../en/SINGLE_THREAD_MODEL.md) - 单线程模型说明

### 代码规范
- [CODING_STANDARDS.md](../en/CODING_STANDARDS.md) - 编码规范和注释标准
- [DOXYGEN_EXAMPLES.md](../en/DOXYGEN_EXAMPLES.md) - Doxygen 注释示例

### 架构文档
- [UVBUS_UVRPC_ARCHITECTURE.md](../en/UVBUS_UVRPC_ARCHITECTURE.md) - UVBus 和 UVRPC 架构
- [UVRPC_UVBUS_INTEGRATION.md](../en/UVRPC_UVBUS_INTEGRATION.md) - UVRPC 与 UVBus 集成
- [GENERATED_API_DESIGN.md](../en/GENERATED_API_DESIGN.md) - 代码生成 API 设计

### 生成的文档
- [Doxygen HTML](../doxygen/html/index.html) - 自动生成的 API 文档

## 中文文档 (Chinese Documentation)

中文文档正在开发中。目前提供以下资源：

### 快速开始
- 英文版：[QUICK_START.md](../en/QUICK_START.md)
- 包含完整的安装、配置和使用示例

### API 文档
- 英文版：[API_GUIDE.md](../en/API_GUIDE.md)
- 包含所有 API 的详细说明和示例

### 构建指南
- 英文版：[BUILD_AND_INSTALL.md](../en/BUILD_AND_INSTALL.md)
- 包含详细的构建步骤和依赖说明

## 示例代码

查看 [examples/](../../examples/) 目录获取完整的示例程序：

### 基础示例
- `simple_server.c` - 简单的 echo 服务器
- `simple_client.c` - 简单的 echo 客户端

### 高级示例
- `async_await_demo.c` - Async/Await 模式示例
- `broadcast_publisher.c` - 广播发布者
- `broadcast_subscriber.c` - 广播订阅者
- `complete_example.c` - 完整功能示例

### 传输协议示例
- `tcp_*` - TCP 传输示例
- `udp_*` - UDP 传输示例
- `ipc_*` - IPC 传输示例
- `inproc_*` - INPROC 传输示例

## 性能测试

查看 [benchmark/](../../benchmark/) 目录获取性能测试结果：

- [PERFORMANCE_TEST_FINAL_REPORT.md](../../benchmark/PERFORMANCE_TEST_FINAL_REPORT.md) - 性能测试报告
- [PERFORMANCE_ANALYSIS.md](../../benchmark/results/PERFORMANCE_ANALYSIS.md) - 性能分析

## 贡献指南

欢迎贡献代码！请阅读 [CODING_STANDARDS.md](../en/CODING_STANDARDS.md) 了解编码规范。

## 许可证

MIT License - 详见 [LICENSE](../../LICENSE)

## 联系方式

- GitHub: [https://github.com/adam-ikari/uvrpc](https://github.com/adam-ikari/uvrpc)
- Issues: [https://github.com/adam-ikari/uvrpc/issues](https://github.com/adam-ikari/uvrpc/issues)

---

**最后更新**: 2026-02-18