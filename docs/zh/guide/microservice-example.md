# 两个服务、两个进程：完整示例

uVRPC 是低层 RPC 库，不是微服务框架。它不做服务发现、负载均衡、TLS 或跨服务重试——那是网关或应用的职责。它提供的是每个服务都需要的那一块：在用户自己拥有的单线程事件循环上，按选定的传输做类型化调用。

这一页用这一块搭一个完整的双服务系统：库存服务与审计服务，作为独立进程监听不同端口，共享一份 schema。

| 文件 | 作用 |
| --- | --- |
| `schema/shop.fbs` | 在同一份 schema 里声明两个服务 |
| `examples/shop_inventory_server.c` | 管理库存，实现 `Stock` / `Reserve` |
| `examples/shop_audit_server.c` | 记录事件，实现 `Record` / `History` |
| `examples/shop_client.c` | 编排两个服务并逐条断言 |

## 构建与运行

```bash
cmake --build build --target shop_inventory_server shop_audit_server shop_client

./dist/bin/shop_inventory_server tcp://127.0.0.1:7101 &
./dist/bin/shop_audit_server    tcp://127.0.0.1:7102 &
./dist/bin/shop_client
```

`shop_client` 每项检查打印一行，任一条不符即以非零退出，所以它同时是端到端冒烟测试。两个 server 会继续运行；完成后用 `fg` 或 kill 结束。

## 一份 schema，两个服务

两个服务都写在 `schema/shop.fbs` 里，但构建成独立二进制。schema 描述的是系统，不是碰巧托管它的进程：

```flatbuffers
namespace shop;

table ReserveRequest  { sku: string; qty: uint32; }
table ReserveResponse { ok: bool; available: uint32; }

rpc_service InventoryService {
    Stock(StockRequest):StockResponse;
    Reserve(ReserveRequest):ReserveResponse;
}

rpc_service ShopAuditService {
    Record(AuditRequest):EmptyResponse (oneway);
    History(HistoryRequest):HistoryResponse;
}
```

`Record` 声明为 `(oneway)`：客户端不挂回调，服务端 stub 不写响应。空的响应表是约定，不是触发器——方法声明才决定形状。

## 这个示例做对的三件事

### 1. 业务拒绝是一次成功的调用

库存不足时 `Reserve` 返回 `UVRPC_OK` 且 `ok=false`，库存不变。把"没库存"编码成传输层错误，调用方就分不清"服务不可达"和"服务说不"。

```c
shop_ReserveRequest_t req  = { .sku = "widget", .qty = 9 };
shop_ReserveResponse_t resp = {0};
int ret = uvrpc_inventoryservice_Reserve_sync(inventory, &resp, &req, 5000);
check("被拒，库存仍为 2",
      ret == UVRPC_OK && !resp.ok && resp.available == 2);
```

`StockResponse.found` 用途相同：未知 SKU 与售罄是两种答案，所以分两个字段。

### 2. 编排放在调用方

处理器运行在服务端自己的事件循环上，请求对象还活在分发栈里。它既不能挂起等另一个服务，也不能在返回后补发响应。所以"先预留、再审计"是调用方的顺序调用，不是处理器里的等待：

```c
/* shop_client.c，节选 */
reserve_on_inventory(...);        /* 第一个服务 */
record_on_audit(actor, action);   /* 第二个服务，oneway */
```

如果需要在进程重启后仍能继续的工作流，那是本库之外的 workflow engine 的事，不是让处理器一直握着请求。

### 3. 查询结果被复制出来

`HistoryResponse.items` 是表的向量。生成的 `_sync()` 绑定逐元素复制，字符串也一起复制，所以响应缓冲释放后答案仍可读。用完显式释放：

```c
shop_HistoryResponse_t history = {0};
int ret = uvrpc_shopauditservice_History_sync(audit, &history, &req, 5000);
/* ... 读取 history.items[i].actor / .action / .sku / .qty ... */
uvrpc_shopauditservice_History_response_free(&history);
```

`limit=0` 表示"全部保留"，`limit=1` 只返回最新一条。

## 这个示例刻意不做的事

- **服务发现 / 负载均衡**：地址在客户端显式写出。实例会变动时，在前面放解析器或 sidecar。
- **TLS / 认证**：帧是明文。跑在可信网络内，或让网关终结 TLS。见项目的 Non-goals。
- **跨服务重试 / 熔断**：`uvrpc_client_set_max_retries()` 重试的是同步发送失败；等不到响应是超时，不是跨服务策略的触发点。策略放在编排层。
- **多线程扩展**：靠多跑几个单线程进程扩展，不给一个 loop 加线程。

## 验证

`tests/generated_table_array_test.c` 在同一 schema 上以 SAMELOOP 覆盖 `_sync` / `_all` / `_any` 三条路径及各自的释放函数。生成的绑定本身不入库——`tools/uvrpcc.py` 在构建时生成。

单服务路径见[一个完整的应用：KV 服务](/zh/guide/kv-example)，为何库不拥有线程、锁或可变全局见[设计哲学](/zh/guide/design-philosophy)。
