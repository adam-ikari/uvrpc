# UVRPC

Ultra-Fast RPC Framework - 零线程、零锁、零全局变量

## 快速开始

```bash
# 一键构建
./quickstart.sh

# 生成代码
./tools/dist/uvrpc-gen schema/rpc_api.fbs -o generated

# 查看示例
cat examples/simple_server.c
cat examples/simple_client.c
```

## 特性

- ✅ 极致性能：单线程事件循环，无锁设计
- ✅ 零拷贝：进程内通信直接内存传递
- ✅ 多传输：TCP、UDP、IPC、INPROC
- ✅ 双模式：Server/Client 和 Broadcast
- ✅ 自动生成：FlatBuffers DSL 生成代码
- ✅ 循环注入：支持多实例和单元测试

## 项目结构

```
uvrpc/
├── include/          # 公共头文件
├── src/              # 核心实现
├── schema/           # FlatBuffers schema
├── generated/        # 生成的代码
├── examples/         # 示例程序
├── benchmark/        # 性能测试
└── tools/            # 代码生成器
```

## 编译

```bash
make build            # 构建项目
make test             # 运行测试
make benchmark        # 性能测试
make generate         # 生成代码
```

## 代码生成

### 定义 Schema

```flatbuffers
namespace myapp;

table AddRequest {
    a: int32;
    b: int32;
}

table AddResponse {
    result: int32;
}

rpc_service MathService {
    Add(AddRequest):AddResponse;
}
```

### 生成代码

```bash
./tools/dist/uvrpc-gen schema/your_service.fbs -o generated
```

### 使用生成的 API

```c
#include "myapp_math_api.h"

// 服务端
uvrpc_server_t* server = myapp_math_create_server(&loop, "tcp://0.0.0.0:5555");
myapp_math_start_server(server);

// 客户端
uvrpc_client_t* client = myapp_math_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);
myapp_math_Add(client, callback, NULL, 10, 20);
```

## 文档

- [快速开始](docs/QUICK_START.md)
- [API 指南](docs/API_GUIDE.md)
- [设计哲学](docs/DESIGN_PHILOSOPHY.md)
- [架构文档](docs/architecture/)

## 性能

| 模式 | 吞吐量 | 延迟 |
|------|--------|------|
| INPROC | 1M+ ops/s | <1μs |
| TCP | 500K+ ops/s | ~10μs |
| UDP | 1M+ ops/s | ~5μs |
| IPC | 800K+ ops/s | ~8μs |

## 许可证

MIT License