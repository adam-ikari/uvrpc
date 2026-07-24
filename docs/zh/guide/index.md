# 指南

UVRPC 是一个极简、高性能的 C99 RPC 框架，基于 libuv 和 FlatBuffers。本指南将带你完整使用它。

## 章节

- [快速开始](/zh/guide/quick-start) — 5 分钟构建并运行你的第一个 RPC。
- [构建安装](/zh/build-install) — 依赖、CMake 选项与安装。
- [API 指南](/zh/guide/api-guide) — 服务端/客户端 API、传输层与 RPC 模式。
- [性能测试](/zh/guide/benchmark) — 性能方法论与实测数据。
- [设计哲学](/zh/guide/design-philosophy) — 零线程、零锁、零全局变量。
- [单线程模型](/zh/guide/single-thread-model) — 为什么单线程事件循环不需要锁。

## 30 秒速览

```c
/* 服务端：注册 handler 并运行循环 */
uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);

/* 客户端：连接并调用 */
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
uvrpc_client_call(client, "echo", data, size, on_response, NULL);
```

传输层通过地址前缀选择 —— `tcp://`、`udp://`、`ipc://`、`inproc://`、`sameloop://` —— 全部传输使用相同 API。

## UVRPC 是什么（不是什么）

UVRPC 面向**调用语义**：低延迟、小 payload 的请求/响应。
它不是大数据传输管道——大数据请用对象存储、流式管道或共享内存。
进程内传输已是零拷贝指针传递，对小 payload 已达最优。
