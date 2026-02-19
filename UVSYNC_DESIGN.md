# UVRPC 异步并发控制抽象设计文档

## 概述

将 libuv 的并发控制功能抽象为独立的模块，提供统一、简洁的异步编程接口。

## 命名

### 选择：`uvasync`

**命名理由**：
- ✅ 简洁易记（5 个字母）
- ✅ 语义清晰（uv + async）
- ✅ 符合命名规范（uv 前缀）
- ✅ 表达核心功能（基于 libuv 的异步控制）

### 其他候选

| 名称 | 优点 | 缺点 |
|------|------|------|
| `uvflow` | 体现数据流 | 概念不够明确 |
| `uvconcurrency` | 语义准确 | 过长（13 字母） |
| `uv_executor` | 执行器概念 | 有点过于底层 |
| `loop_scheduler` | 调度器概念 | 过长，不够直观 |

## 设计目标

1. **统一接口**：封装 libuv 事件循环操作
2. **简化使用**：降低并发原语的使用复杂度
3. **类型安全**：提供类型化的异步操作接口
4. **性能优先**：零或最小化抽象开销
5. **易于测试**：支持依赖注入和模拟

## 核心概念

### 1. Async Context（异步上下文）

封装事件循环和异步状态：

```c
typedef struct uvasync_context {
    uv_loop_t* loop;           // libuv 事件循环
    int owns_loop;             // 是否拥有 loop（需要清理）
    void* user_data;           // 用户数据
    uvasync_allocator_t* allocator;  // 分配器
} uvasync_context_t;
```

### 2. Async Task（异步任务）

表示一个异步操作：

```c
typedef struct uvasync_task {
    uvasync_task_fn_t fn;      // 任务函数
    void* data;                // 任务数据
    uvasync_promise_t* promise; // 结果 Promise
    uvasync_context_t* ctx;    // 所属上下文
} uvasync_task_t;
```

### 3. Async Scheduler（异步调度器）

任务调度和执行：

```c
typedef struct uvasync_scheduler {
    uvasync_context_t* ctx;    // 上下文
    uvasync_task_queue_t* queue;  // 任务队列
    uvasync_semaphore_t* concurrency_limit;  // 并发限制
    uvasync_stats_t stats;     // 统计信息
} uvasync_scheduler_t;
```

## API 设计

### 初始化和清理

```c
// 创建上下文
uvasync_context_t* uvasync_context_create(uv_loop_t* loop);
void uvasync_context_destroy(uvasync_context_t* ctx);

// 创建独立的上下文（自动创建 loop）
uvasync_context_t* uvasync_context_create_new(void);

// 创建调度器
uvasync_scheduler_t* uvasync_scheduler_create(
    uvasync_context_t* ctx,
    int max_concurrency
);
void uvasync_scheduler_destroy(uvasync_scheduler_t* scheduler);
```

### 任务调度

```c
// 提交任务
int uvasync_submit(
    uvasync_scheduler_t* scheduler,
    uvasync_task_fn_t fn,
    void* data,
    uvasync_promise_t* result
);

// 提交批量任务
int uvasync_submit_batch(
    uvasync_scheduler_t* scheduler,
    uvasync_task_t* tasks,
    int count,
    uvasync_promise_t* results
);

// 取消任务
int uvasync_cancel(uvasync_task_id_t task_id);
```

### 并发控制

```c
// 设置并发限制
int uvasync_scheduler_set_concurrency(
    uvasync_scheduler_t* scheduler,
    int max_concurrency
);

// 获取当前并发数
int uvasync_scheduler_get_concurrent_count(
    uvasync_scheduler_t* scheduler
);

// 等待所有任务完成
int uvasync_scheduler_wait_all(
    uvasync_scheduler_t* scheduler,
    uint64_t timeout_ms
);
```

### 统计和监控

```c
// 获取统计信息
uvasync_stats_t* uvasync_scheduler_get_stats(
    uvasync_scheduler_t* scheduler
);

// 重置统计信息
void uvasync_scheduler_reset_stats(
    uvasync_scheduler_t* scheduler
);
```

## 使用示例

### 基本使用

```c
// 创建上下文
uvasync_context_t* ctx = uvasync_context_create_new();

// 创建调度器（最多 10 个并发任务）
uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 10);

// 提交任务
uvasync_promise_t* promise = uvrpc_promise_create(ctx->loop);
uvrpc_promise_then(promise, on_complete, NULL);

uvasync_submit(scheduler, my_task_function, task_data, promise);

// 等待所有任务完成
uvasync_scheduler_wait_all(scheduler, 5000);

// 清理
uvasync_scheduler_destroy(scheduler);
uvasync_context_destroy(ctx);
```

### 批量任务

```c
// 创建任务数组
uvasync_task_t tasks[100];
uvasync_promise_t* promises[100];

for (int i = 0; i < 100; i++) {
    tasks[i].fn = process_item;
    tasks[i].data = &items[i];
    promises[i] = uvrpc_promise_create(ctx->loop);
}

// 提交批量任务
uvasync_submit_batch(scheduler, tasks, 100, promises);

// 等待所有完成
uvasync_scheduler_wait_all(scheduler, 0);  // 无限等待
```

### 动态并发控制

```c
// 初始：5 个并发
uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 5);

// 系统负载低时：增加到 20
if (system_load < 0.3) {
    uvasync_scheduler_set_concurrency(scheduler, 20);
}

// 系统负载高时：降低到 3
if (system_load > 0.8) {
    uvasync_scheduler_set_concurrency(scheduler, 3);
}
```

## 与现有原语的关系

### Promise

```c
// 之前
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);

// 之后（可选）
uvasync_promise_t* promise = uvasync_promise_create(ctx);
```

### Semaphore

```c
// 之前
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 10);
uvrpc_semaphore_acquire_async(&sem, promise);

// 之后（可选）
uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 10);
uvasync_submit(scheduler, task_fn, data, promise);
```

### WaitGroup

```c
// 之前
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop);
uvrpc_waitgroup_add(&wg, count);
// ... 任务 ...
uvrpc_waitgroup_done(&wg);

// 之后（可选）
uvasync_scheduler_wait_all(scheduler, timeout);
```

## 实现计划

### Phase 1: 核心抽象
- [ ] 创建 `uvasync.h` 头文件
- [ ] 创建 `uvasync.c` 实现文件
- [ ] 实现上下文管理
- [ ] 实现任务队列
- [ ] 实现基本调度器

### Phase 2: 并发控制
- [ ] 集成 Semaphore
- [ ] 实现并发限制
- [ ] 实现任务取消
- [ ] 实现超时控制

### Phase 3: 高级功能
- [ ] 任务优先级
- [ ] 任务依赖
- [ ] 批量操作
- [ ] 统计监控

### Phase 4: 集成和测试
- [ ] 与现有原语集成
- [ ] 编写单元测试
- [ ] 性能基准测试
- [ ] 文档完善

## 性能考虑

1. **零抽象开销**：关键路径无额外函数调用
2. **栈分配优先**：上下文和任务结构体可栈分配
3. **原子操作**：并发控制使用原子操作，无锁设计
4. **批量优化**：批量任务提交减少锁竞争

## 命名总结

**最终选择：`uvasync`**

- 简洁：5 个字母，易读易记
- 语义：uv（libuv）+ async（异步）
- 一致：与 uvrpc、uvbus 等命名保持一致
- 专业：体现技术特性（异步并发控制）

---

**设计版本**: 1.0  
**设计日期**: 2026-02-19  
**状态**: 设计阶段