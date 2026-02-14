# UVRPC 迁移指南

## 从旧版本迁移

### 架构变更

旧版本使用以下技术栈：
- ZeroMQ (via uvzmq)
- msgpack (via mpack)
- 自定义 DSL 代码生成

新版本使用：
- libuv 原生传输
- FlatBuffers (via FlatCC)
- 简化的 API

### API 变更

#### 配置

**旧版本**：
```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
```

**新版本**（需要指定传输类型）：
```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
```

#### 服务端创建

**旧版本**：
```c
uvrpc_server_t* server = uvrpc_server_create(config);
```

**新版本**（相同）：
```c
uvrpc_server_t* server = uvrpc_server_create(config);
```

#### 客户端连接

**旧版本**：
```c
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
```

**新版本**（相同）：
```c
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
```

#### 处理器函数

**旧版本**：
```c
void handler(uvrpc_request_t* req, void* ctx) {
    // req->params 是 msgpack 格式
}
```

**新版本**：
```c
void handler(uvrpc_request_t* req, void* ctx) {
    // req->params 是原始字节数据（FlatBuffers）
    // 可以直接访问，无需额外解析
}
```

#### 回调函数

**旧版本**：
```c
void callback(uvrpc_response_t* resp, void* ctx) {
    // resp->result 是 msgpack 格式
}
```

**新版本**：
```c
void callback(uvrpc_response_t* resp, void* ctx) {
    // resp->result 是原始字节数据（FlatBuffers）
    // 可以直接访问，无需额外解析
}
```

### 序列化变更

#### msgpack → FlatBuffers

**旧版本（msgpack）**：
```c
// 编码
mpack_writer_t writer;
mpack_writer_init(&writer, buffer, buffer_size);
mpack_write_str(&writer, "method");
mpack_start_array(&writer);
mpack_write_bin(&writer, data, data_size);
mpack_finish_array(&writer);

// 解码
mpack_reader_t reader;
mpack_reader_init(&reader, data, size);
char* method = mpack_read_str(&reader, &len);
```

**新版本（FlatBuffers）**：
```c
// 编码
flatcc_builder_t builder;
flatcc_builder_init(&builder);
flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
flatbuffers_uint8_vec_ref_t data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
uvrpc_RpcFrame_start_as_root(&builder);
uvrpc_RpcFrame_method_add(&builder, method_ref);
uvrpc_RpcFrame_params_add(&builder, data_ref);
uvrpc_RpcFrame_end_as_root(&builder);

// 解码
uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
const char* method = uvrpc_RpcFrame_method(frame);
flatbuffers_uint8_vec_t data = uvrpc_RpcFrame_params(frame);
```

### 传输层变更

#### ZeroMQ → libuv

**旧版本（ZeroMQ）**：
- 使用 uvzmq 封装 ZeroMQ
- 自动处理连接管理
- 内置消息队列

**新版本（libuv）**：
- 直接使用 libuv 原生传输
- 需要显式指定传输类型
- 更低的延迟，更高的性能

### 内存分配变更

#### 新增内存分配器支持

**新版本**：
```c
// 使用 mimalloc（默认）
uvrpc_allocator_init(UVRPC_ALLOCATOR_MIMALLOC, NULL);

// 使用系统分配器
uvrpc_allocator_init(UVRPC_ALLOCATOR_SYSTEM, NULL);

// 使用自定义分配器
uvrpc_custom_allocator_t custom = {
    .alloc = my_alloc,
    .free = my_free,
    // ...
};
uvrpc_allocator_init(UVRPC_ALLOCATOR_CUSTOM, &custom);
```

### 迁移步骤

1. **更新依赖**
   - 移除 ZeroMQ 和 msgpack
   - 添加 FlatCC 和 mimalloc
   - 运行 `./scripts/setup_deps.sh`

2. **更新配置**
   - 为所有配置添加传输类型
   - 为所有配置添加通信类型

3. **更新序列化**
   - 将 msgpack 编码/解码替换为 FlatBuffers
   - 更新数据访问模式

4. **更新测试**
   - 更新单元测试以使用新的 API
   - 更新集成测试以使用新的传输层

5. **测试验证**
   - 运行单元测试：`./dist/bin/uvrpc_tests`
   - 运行集成测试：`./dist/bin/test_tcp`
   - 运行性能测试：`./dist/bin/uvrpc_benchmark`

### 兼容性说明

- **不兼容**：新版本与旧版本不兼容
- **数据格式**：FlatBuffers 与 msgpack 不兼容
- **网络协议**：libuv 原生传输与 ZeroMQ 不兼容

### 性能对比

| 指标 | 旧版本 (ZeroMQ + msgpack) | 新版本 (libuv + FlatBuffers) |
|-----|--------------------------|------------------------------|
| 吞吐量 | ~80k ops/s | ~100k+ ops/s |
| 平均延迟 | ~3ms | ~2ms |
| 内存使用 | 较高 | 较低 |
| 序列化开销 | 中等 | 较低 |

### 示例对比

#### 服务端

**旧版本**：
```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);

uv_run(&loop, UV_RUN_DEFAULT);
```

**新版本**：
```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);

uv_run(&loop, UV_RUN_DEFAULT);
```

#### 客户端

**旧版本**：
```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);

uint8_t data[] = {0x01, 0x02, 0x03};
uvrpc_client_call(client, "method", data, sizeof(data), callback, NULL);

uv_run(&loop, UV_RUN_DEFAULT);
```

**新版本**：
```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);

uint8_t data[] = {0x01, 0x02, 0x03};
uvrpc_client_call(client, "method", data, sizeof(data), callback, NULL);

uv_run(&loop, UV_RUN_DEFAULT);
```

### 常见问题

#### Q: 为什么移除 ZeroMQ？

A: libuv 原生传输提供更低的延迟和更好的性能，同时减少了依赖。

#### Q: 为什么使用 FlatBuffers 而不是 msgpack？

A: FlatBuffers 提供更好的性能和更强的类型安全，同时支持零拷贝访问。

#### Q: 我需要重写所有代码吗？

A: 大部分 API 保持不变，主要是配置和序列化部分需要更新。

#### Q: 性能提升了多少？

A: 新版本吞吐量提升约 25%，延迟降低约 33%。

#### Q: 支持哪些传输协议？

A: 支持 TCP、UDP、IPC 和 INPROC，比旧版本更多。

### 获取帮助

如果您在迁移过程中遇到问题：

1. 查看 [API 参考文档](API_REFERENCE.md)
2. 查看 [设计哲学文档](DESIGN_PHILOSOPHY.md)
3. 运行示例程序：`./dist/bin/simple_server` 和 `./dist/bin/simple_client`
4. 提交 Issue：https://github.com/your-org/uvrpc/issues