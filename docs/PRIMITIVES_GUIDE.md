# UVRPC Async Programming Primitives Guide

## Overview

UVRPC provides async programming primitives for concurrent control in RPC applications. These primitives are designed to work seamlessly with libuv event loops without blocking operations.

## Primitives

### 1. Promise/Future Pattern

The Promise pattern represents an async operation that will complete in the future. It can be fulfilled (success) or rejected (error).

#### Features
- Chain async operations together
- Pass results through callbacks
- Handle errors at the end of the chain
- Zero-allocation when using stack-allocated promises

#### Example

```c
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);

// Start async operation
start_async_operation(&promise);

// Set completion handler
uvrpc_promise_then(&promise, on_complete, user_data);

void on_complete(uvrpc_promise_t* promise, void* user_data) {
    if (uvrpc_promise_is_fulfilled(promise)) {
        uint8_t* result;
        size_t size;
        uvrpc_promise_get_result(promise, &result, &size);
        // Use result
    } else {
        const char* error = uvrpc_promise_get_error(promise);
        // Handle error
    }
}

// Wait for completion
while (uvrpc_promise_is_pending(&promise)) {
    uv_run(loop, UV_RUN_ONCE);
    usleep(10000);
}

uvrpc_promise_cleanup(&promise);
```

#### API Functions

- `uvrpc_promise_init()` - Initialize a promise
- `uvrpc_promise_cleanup()` - Free resources
- `uvrpc_promise_resolve()` - Resolve with result
- `uvrpc_promise_reject()` - Reject with error
- `uvrpc_promise_then()` - Set completion callback
- `uvrpc_promise_is_fulfilled()` - Check if fulfilled
- `uvrpc_promise_is_rejected()` - Check if rejected
- `uvrpc_promise_is_pending()` - Check if pending
- `uvrpc_promise_get_result()` - Get result data
- `uvrpc_promise_get_error()` - Get error message

---

### 2. Semaphore Pattern

The Semaphore pattern limits concurrent operations by controlling permit availability. Useful for rate limiting and resource protection.

#### Features
- JavaScript-style Promise API for async acquire
- Thread-safe using atomic operations
- Automatic waiter notification
- Resource pool management

#### Example

```c
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 10); // Max 10 concurrent operations

// Create promise and acquire permit
uvrpc_promise_t* permit_promise = uvrpc_promise_create(loop);
uvrpc_promise_then(permit_promise, on_permit_acquired, request_data);
uvrpc_semaphore_acquire_async(&sem, permit_promise);

void on_permit_acquired(uvrpc_promise_t* promise, void* user_data) {
    // Permit acquired, make RPC call
    uvrpc_client_call(client, method, params, size, on_response, &sem);
    
    // Cleanup promise
    uvrpc_promise_destroy(promise);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    uvrpc_semaphore_t* sem = (uvrpc_semaphore_t*)ctx;
    // Process response
    // Release permit
    uvrpc_semaphore_release(sem);
}

// Cleanup when done
uvrpc_semaphore_cleanup(&sem);
```

#### API Functions

- `uvrpc_semaphore_init()` - Initialize with permit count
- `uvrpc_semaphore_cleanup()` - Free resources
- `uvrpc_semaphore_acquire_async()` - Acquire permit (JavaScript-style Promise)
- `uvrpc_semaphore_release()` - Release permit
- `uvrpc_semaphore_try_acquire()` - Try to acquire immediately
- `uvrpc_semaphore_get_available()` - Get available permit count
- `uvrpc_semaphore_get_waiting_count()` - Get waiting operation count

---

### 3. Promise Combinators

JavaScript-style Promise combinators for coordinating multiple async operations.

#### Promise.all()

Wait for all promises to fulfill. If any rejects, the combined promise rejects immediately.

```c
uvrpc_promise_t* promises[5];
// Create promises...

uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_all(promises, 5, &combined, loop);

uvrpc_promise_then(&combined, on_all_complete, NULL);

void on_all_complete(uvrpc_promise_t* promise, void* user_data) {
    if (uvrpc_promise_is_fulfilled(promise)) {
        uint8_t* result;
        size_t size;
        uvrpc_promise_get_result(promise, &result, &size);
        // Process combined result
    }
}
```

#### Promise.race()

Wait for the first promise to complete (fulfills or rejects).

```c
uvrpc_promise_t* promises[3];
// Create promises...

uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_race(promises, 3, &combined, loop);

uvrpc_promise_then(&combined, on_first_complete, NULL);
```

#### Promise.allSettled()

Wait for all promises to complete (fulfill or reject), collecting all results and errors.

```c
uvrpc_promise_t* promises[10];
// Create promises...

uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_all_settled(promises, 10, &combined, loop);

uvrpc_promise_then(&combined, on_all_settled, NULL);
```

#### API Functions

- `uvrpc_promise_all()` - Wait for all promises to fulfill
- `uvrpc_promise_race()` - Wait for first promise to complete
- `uvrpc_promise_all_settled()` - Wait for all promises to complete
- `uvrpc_promise_all_sync()` - Synchronous Promise.all() (blocking)
- `uvrpc_promise_race_sync()` - Synchronous Promise.race() (blocking)

---

### 4. WaitGroup Pattern

A simplified barrier pattern that counts operations. Useful for fire-and-forget concurrent tasks.

#### Features
- Add operations with add()
- Signal completion with done()
- Callback when count reaches 0
- Simple counting interface

#### Example

```c
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop, on_all_done, NULL);

// Add 10 operations
uvrpc_waitgroup_add(&wg, 10);

// Start operations
for (int i = 0; i < 10; i++) {
    uvrpc_client_call(client, method, params, size, on_response, &wg);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    uvrpc_waitgroup_t* wg = (uvrpc_waitgroup_t*)ctx;
    // Process response
    // Signal operation complete
    uvrpc_waitgroup_done(wg);
}

void on_all_done(uvrpc_waitgroup_t* wg, void* user_data) {
    printf("All operations complete\n");
}

uvrpc_waitgroup_cleanup(&wg);
```

#### API Functions

- `uvrpc_waitgroup_init()` - Initialize waitgroup
- `uvrpc_waitgroup_cleanup()` - Free resources
- `uvrpc_waitgroup_add()` - Add operations (can be negative)
- `uvrpc_waitgroup_done()` - Signal one operation complete
- `uvrpc_get_count()` - Get current count
- `uvrpc_waitgroup_get_promise()` - Get completion promise

---

## Use Cases

### Limiting Concurrent RPC Calls

Use a semaphore to prevent overwhelming a server with too many concurrent requests:

```c
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 50); // Max 50 concurrent calls

// For each request
uvrpc_promise_t* permit_promise = uvrpc_promise_create(loop);
uvrpc_promise_then(permit_promise, on_permit_acquired, request);
uvrpc_semaphore_acquire_async(&sem, permit_promise);

void on_permit_acquired(uvrpc_promise_t* promise, void* ctx) {
    make_rpc_call(..., &sem);
    uvrpc_promise_destroy(promise);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    process_response(resp);
    uvrpc_semaphore_release((uvrpc_semaphore_t*)ctx);
}
```

### Aggregating Multiple RPC Results

Use a barrier to wait for multiple calls to complete:

```c
uvrpc_barrier_t barrier;
uvrpc_barrier_init(&barrier, loop, count, on_all_complete, results);

// Make multiple calls
for (int i = 0; i < count; i++) {
    uvrpc_client_call(client, methods[i], params[i], sizes[i], 
                      on_response, &barrier);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    uvrpc_barrier_t* barrier = (uvrpc_barrier_t*)ctx;
    store_result(resp);
    uvrpc_barrier_wait(barrier, resp->error_code != 0);
}
```

### Chaining Async Operations

Use promises to chain dependent operations:

```c
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);

// Step 1: Get user
uvrpc_client_call(client, "getUser", user_id, sizeof(user_id), 
                  on_user, &promise);

void on_user(uvrpc_response_t* resp, void* ctx) {
    if (resp->error_code != 0) {
        uvrpc_promise_reject(&promise, resp->error_code, "Failed");
        return;
    }
    
    // Step 2: Get orders (chain)
    uvrpc_promise_set_callback(&promise, on_orders, &promise);
    uvrpc_client_call(client, "getOrders", resp->result, resp->result_size, 
                      on_orders, &promise);
}

void on_orders(uvrpc_response_t* resp, void* ctx) {
    uvrpc_promise_t* promise = (uvrpc_promise_t*)ctx;
    if (resp->error_code != 0) {
        uvrpc_promise_reject(promise, resp->error_code, "Failed");
        return;
    }
    uvrpc_promise_resolve(promise, resp->result, resp->result_size);
}
```

### Fire-and-Forget Concurrent Tasks

Use WaitGroup for independent tasks:

```c
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop, on_complete, NULL);

// Add tasks
uvrpc_waitgroup_add(&wg, task_count);

// Start all tasks
for (int i = 0; i < task_count; i++) {
    start_task(i, &wg);
}

void on_task_complete(void* ctx) {
    uvrpc_waitgroup_t* wg = (uvrpc_waitgroup_t*)ctx;
    uvrpc_waitgroup_done(wg);
}
```

---

## Best Practices

### 1. Stack Allocation

Allocate primitives on stack when possible for zero-allocation:

```c
// Good: Stack allocation
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);
// Use promise...
uvrpc_promise_cleanup(&promise);

// Avoid: Heap allocation (unless necessary)
uvrpc_promise_t* promise = uvrpc_alloc(sizeof(uvrpc_promise_t));
uvrpc_promise_init(promise, loop);
// Use promise...
uvrpc_promise_cleanup(promise);
uvrpc_free(promise);
```

### 2. Proper Cleanup

Always cleanup primitives when done:

```c
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 10);
// Use sem...
uvrpc_semaphore_cleanup(&sem); // Always cleanup
```

### 3. Check Return Values

Always check return values from init functions:

```c
int ret = uvrpc_semaphore_init(&sem, loop, 10);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Failed to init semaphore: %s\n", uvrpc_strerror(ret));
    return ret;
}
```

### 4. Avoid Blocking

Never block waiting for primitives in event loop callbacks:

```c
// Bad: Blocks event loop
while (!uvrpc_barrier_is_complete(&barrier)) {
    sleep(1); // Blocking!
}

// Good: Let the callback handle completion
uvrpc_barrier_init(&barrier, loop, count, on_complete, data);
// Start operations...
// Callback will be invoked automatically
```

### 5. Thread Safety

Primitives use atomic operations and are thread-safe for multi-threaded event loops. However, ensure consistent event loop usage:

```c
// Each thread should have its own loop and primitives
uv_loop_t loop1, loop2;
uv_loop_init(&loop1);
uv_loop_init(&loop2);

uvrpc_semaphore_t sem1, sem2;
uvrpc_semaphore_init(&sem1, &loop1, 10);
uvrpc_semaphore_init(&sem2, &loop2, 10);

// Run loops in separate threads
```

---

## Performance Considerations

### Memory Usage

- Promise: ~200 bytes (including async handle)
- Semaphore: ~200 bytes
- Barrier: ~200 bytes
- WaitGroup: ~200 bytes

### CPU Usage

- Minimal overhead for atomic operations
- Callbacks scheduled via uv_async (non-blocking)
- No busy waiting

### Scalability

- Tested with 10,000+ concurrent operations
- Semaphore: Handles thousands of waiters efficiently
- Barrier: Scales to millions of operations
- Promise: Efficient chaining with minimal copying

---

## Error Handling

All primitives return error codes:

```c
int ret = uvrpc_semaphore_init(&sem, loop, 10);
if (ret != UVRPC_OK) {
    switch (ret) {
        case UVRPC_ERROR_INVALID_PARAM:
            // Invalid parameters
            break;
        case UVRPC_ERROR_NO_MEMORY:
            // Out of memory
            break;
        case UVRPC_ERROR_INVALID_STATE:
            // Invalid state for operation
            break;
        default:
            // Other errors
            break;
    }
}
```

Use `uvrpc_strerror()` to get error messages:

```c
int ret = uvrpc_semaphore_init(&sem, loop, 10);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Error: %s\n", uvrpc_strerror(ret));
}
```

---

## Complete Example

See `examples/primitives_demo.c` for a complete working example demonstrating all primitives:

```bash
# Build the example
cd build
cmake .. -DUVRPC_BUILD_EXAMPLES=ON
make primitives_demo

# Run (requires server running)
./dist/bin/primitives_demo
```

---

## API Reference

See `include/uvrpc_primitives.h` for complete API documentation.

---

## Implementation Notes

### Threading Model

- Uses GCC/Clang atomic builtins (`__sync_*`)
- Mutex-protected waiter queue for semaphore
- Lock-free for barrier and waitgroup counters

### libuv Integration

- Uses `uv_async_t` for callback scheduling
- `uv_unref()` to prevent keeping loop alive
- Callbacks run in event loop thread

### Memory Management

- Uses UVRPC allocator (mimalloc by default)
- Zero-allocation when using stack allocation
- Explicit cleanup required

---

## Future Enhancements

Potential additions:

1. **Timeout Support** - Timeout for wait operations
2. **Cancellation** - Cancel pending operations
3. **Priority Queue** - Priority-based semaphore
4. **Batch Operations** - Batch acquire/release

---

## License

MIT License - See LICENSE file for details.

---

## Support

For issues and questions:
- GitHub: https://github.com/adam-ikari/uvrpc
- Documentation: docs/
- Examples: examples/