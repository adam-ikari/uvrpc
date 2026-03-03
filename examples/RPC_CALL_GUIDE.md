# UVRPC 调用方式选择指南

## 快速决策树

```
需要发送响应吗？
├─ 否 → 使用 Oneway RPC (NULL callback)
│   - 日志记录
│   - 事件通知
│   - 心跳检测
│   └─ 示例: scenario_3_oneway_logging.c
│
└─ 是 → 需要多个响应吗？
    ├─ 否 → 使用 Request-Response (uvrpc_request_send_response)
    │   - 查询操作
    │   - 计算操作
    │   - 状态查询
    │   └─ 示例: scenario_1_simple_request_response.c
    │
    └─ 是 → 使用 Streaming (uvrpc_request_send_response_more + uvrpc_request_send_response)
        - 大文件传输
        - 实时数据流
        - 分页查询
        - └─ 示例: scenario_2_streaming_data_transfer.c
```

## 详细场景对比

### 1. 请求-响应 (Request-Response)

**使用场景**：
- ✅ 查询用户信息
- ✅ 执行计算操作
- ✅ 检查服务状态
- ✅ 获取配置数据

**API 使用**：
```c
// 服务器端
uvrpc_request_send_response(req, UVRPC_OK, result, result_size);

// 客户端
uvrpc_client_call(client, "Method", params, params_size, callback, ctx);
```

**特点**：
- 单个请求 → 单个响应
- 最常用模式
- 简单直接
- 同步语义（请求发送后等待响应）

---

### 2. 流式响应 (Streaming Response)

**使用场景**：
- ✅ 大文件传输（分块）
- ✅ 实时数据流（日志、传感器）
- ✅ 分页查询结果
- ✅ 批量数据处理

**API 使用**：
```c
// 服务器端
for (int i = 0; i < num_chunks - 1; i++) {
    uvrpc_request_send_response_more(req, chunk, chunk_size);  // type=2
}
uvrpc_request_send_response(req, UVRPC_OK, last_chunk, last_size);  // type=1

// 客户端
void callback(uvrpc_response_t* resp, void* ctx) {
    if (uvrpc_response_is_stream_more(resp)) {
        // 处理中间响应
    } else if (uvrpc_response_is_stream_end(resp)) {
        // 处理最后响应
    }
}
```

**特点**：
- 单个请求 → 多个响应
- 客户端需要检测流结束
- 适合大数据量传输
- 异步语义（请求发送后陆续接收多个响应）

**关键点**：
- 服务器端：前 N-1 个响应使用 `uvrpc_request_send_response_more()`
- 服务器端：最后一个响应使用 `uvpc_request_send_response()`
- 客户端：使用 `uvrpc_response_is_stream_more()` 检测中间响应
- 客户端：使用 `uvrpc_response_is_stream_end()` 检测最后响应

---

### 3. 单向 RPC (Oneway RPC)

**使用场景**：
- ✅ 日志记录
- ✅ 事件通知
- ✅ 心跳检测
- ✅ 指标上报
- ✅ 性能监控数据上报

**API 使用**：
```c
// 服务器端
void handler(uvrpc_request_t* req, void* ctx) {
    // 处理请求
    // 注意：不发送响应
}

// 客户端
uvrpc_client_call(client, "Method", params, params_size, NULL, NULL);  // NULL callback
```

**特点**：
- 只发送请求，不等待响应
- 最高吞吐量
- 最低延迟
- 不保证送达（UDP 特性）
- 适合高频率调用

**关键点**：
- 客户端：回调参数为 NULL 表示 oneway 模式
- 服务器端：不要调用任何 `uvrpc_request_send_response*()` 函数
- 推荐使用 UDP 传输层

---

### 4. 广播模式 (Broadcast Mode)

**使用场景**：
- ✅ 消息推送
- ✅ 事件广播
- ✅ 实时状态更新
- ✅ 多订阅者系统

**API 使用**：
```c
// 发布者
uvrpc_server_publish(server, "Broadcast", message, message_size);

// 订阅者
uvrpc_client_call(client, "Broadcast", params, params_size, callback, ctx);
```

**特点**：
- 一对多通信
- 发布者不关心订阅者数量
- 订阅者可以随时加入/离开
- 推荐使用 UDP 传输层

**关键点**：
- 使用地址格式：`udp://224.0.0.1:6000`（组播地址）
- 所有订阅者监听同一个地址
- 发布者发送一次，所有订阅者都能收到

---

## 传输层选择建议

### TCP 传输
- **适用场景**：请求-响应、流式响应
- **特点**：可靠、有序、面向连接
- **性能**：中等

### UDP 传输
- **适用场景**：单向 RPC、广播模式
- **特点**：不可靠、无序、无连接
- **性能**：最高

### IPC 传输
- **适用场景**：本地进程间通信
- **特点**：高效、本地优化
- **性能**：高

### INPROC 传输
- **适用场景**：同进程内模块通信
- **特点**：最快、零拷贝
- **性能**：最高

---

## 性能对比

| 模式 | 吞吐量 | 延迟 | 可靠性 | 适用场景 |
|------|--------|------|--------|----------|
| Request-Response | 高 | 中等 | 高 | 查询、计算 |
| Streaming | 中高 | 低 | 高 | 文件传输、数据流 |
| Oneway | 极高 | 极低 | 低 | 日志、监控 |
| Broadcast | 高 | 低 | 低 | 推送、通知 |

---

## 常见错误

### 错误 1：在流式场景中使用普通回调
```c
// ❌ 错误：无法接收多个响应
void callback(uvrpc_response_t* resp, void* ctx) {
    // 只会收到第一个响应
}

// ✅ 正确：检测流结束
void callback(uvrpc_response_t* resp, void* ctx) {
    if (uvrpc_response_is_stream_more(resp)) {
        // 处理中间响应
    } else if (uvrpc_response_is_stream_end(resp)) {
        // 处理最后响应
    }
}
```

### 错误 2：在 Oneway 场景发送响应
```c
// ❌ 错误：Oneway 不应该发送响应
void handler(uvrpc_request_t* req, void* ctx) {
    process_request(req);
    uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);  // 错误！
}

// ✅ 正确：只处理，不发送响应
void handler(uvrpc_request_t* req, void* ctx) {
    process_request(req);
    // 不发送任何响应
}
```

### 错误 3：忘记发送流结束标记
```c
// ❌ 错误：客户端永远不会收到流结束
void stream_handler(uvrpc_request_t* req, void* ctx) {
    for (int i = 0; i < 5; i++) {
        uvrpc_request_send_response_more(req, chunk, size);  // 全部是 type=2
    }
    // 忘记发送最后一个响应
}

// ✅ 正确：发送流结束标记
void stream_handler(uvrpc_request_t* req, void* ctx) {
    for (int  i= 0; i < 4; i++) {
        uvrpc_request_send_response_more(req, chunk, size);  // type=2
    }
    uvrpc_request_send_response(req, UVRPC_OK, last_chunk, size);  // type=1
}
```

---

## 示例程序索引

| 示例 | 文件名 | 场景 | 模式 |
|------|--------|------|------|
| 1 | scenario_1_simple_request_response.c | 加法运算 | Request-Response |
| 2 | scenario_2_streaming_data_transfer.c | 文件传输 | Streaming |
| 3 | scenario_3_oneway_logging.c | 日志记录 | Oneway |
| 4 | scenario_4_broadcast_mode.c | 消息推送 | Broadcast |
| 5 | scenario_5_mixed_mode_api.c | RESTful API | Mixed |

---

## 选择建议

根据业务需求选择：

**需要可靠通信？**
- 是 → 使用 Request-Response 或 Streaming
- 否 → 考虑 Oneway 或 Broadcast

**需要高吞吐量？**
- 是 → 使用 Oneway 或 Broadcast
- 否 → 使用 Request-Response 或 Streaming

**需要多个响应？**
- 是 → 使用 Streaming
- 否 → 使用 Request-Response

**一对多通信？**
- 是 → 使用 Broadcast
- 否 → 使用 Request-Response 或 Streaming 或 Oneway

**本地通信？**
- 同进程 → 使用 INPROC
- 不同进程 → 使用 IPC
- 远程 → 使用 TCP 或 UDP

**需要保证送达？**
- 是 → 使用 TCP
- 否 → 可以使用 UDP