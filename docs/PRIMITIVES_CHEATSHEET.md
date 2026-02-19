# UVRPC Primitives - Quick Reference Cheat Sheet

## Promise

```c
// Initialize
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);

// Set completion handler
uvrpc_promise_then(&promise, callback, user_data);

// In async callback, resolve or reject
if (success) {
    uvrpc_promise_resolve(&promise, result_data, result_size);
} else {
    uvrpc_promise_reject(&promise, error_code, error_message);
}

// Check state
if (uvrpc_promise_is_fulfilled(&promise)) { ... }
if (uvrpc_promise_is_rejected(&promise)) { ... }
if (uvrpc_promise_is_pending(&promise)) { ... }

// Get result
uint8_t* result;
size_t size;
uvrpc_promise_get_result(&promise, &result, &size);

// Get error
const char* error = uvrpc_promise_get_error(&promise);
int32_t code = uvrpc_promise_get_error_code(&promise);

// Cleanup
uvrpc_promise_cleanup(&promise);
```

## Semaphore

```c
// Initialize with max concurrent operations
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 10); // Max 10 concurrent

// Acquire before operation
uvrpc_semaphore_acquire(&sem, on_acquired, request_data);

void on_acquired(uvrpc_semaphore_t* sem, void* ctx) {
    // Permit acquired, do work
    make_rpc_call(..., sem);
}

// Release after operation completes
void on_response(...) {
    process_response();
    uvrpc_semaphore_release((uvrpc_semaphore_t*)ctx);
}

// Try immediate acquire (non-blocking)
if (uvrpc_semaphore_try_acquire(&sem)) {
    // Got permit immediately
}

// Get stats
int available = uvrpc_semaphore_get_available(&sem);
int waiting = uvrpc_semaphore_get_waiting_count(&sem);

// Cleanup
uvrpc_semaphore_cleanup(&sem);
```

## Barrier

```c
// Initialize with count and callback
uvrpc_barrier_t barrier;
uvrpc_barrier_init(&barrier, loop, 5, on_complete, results);

// Start operations
for (int i = 0; i < 5; i++) {
    make_rpc_call(..., &barrier);
}

// In each operation callback
void on_response(uvrpc_response_t* resp, void* ctx) {
    process_result(resp);
    int error = (resp->error_code != 0);
    uvrpc_barrier_wait((uvrpc_barrier_t*)ctx, error);
}

// Callback when all complete
void on_complete(uvrpc_barrier_t* barrier, void* user_data) {
    int errors = uvrpc_barrier_get_error_count(barrier);
    int completed = uvrpc_barrier_get_completed(barrier);
    printf("Complete: %d/%d, Errors: %d\n", completed, barrier->total, errors);
}

// Check state
if (uvrpc_barrier_is_complete(&barrier)) { ... }

// Reset for reuse
uvrpc_barrier_reset(&barrier);

// Cleanup
uvrpc_barrier_cleanup(&barrier);
```

## WaitGroup

```c
// Initialize
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop, on_complete, NULL);

// Add operations
uvrpc_waitgroup_add(&wg, task_count);

// Start operations
for (int i = 0; i < task_count; i++) {
    start_task(..., &wg);
}

// In each task callback
void on_task_complete(...) {
    process_result();
    uvrpc_waitgroup_done((uvrpc_waitgroup_t*)ctx);
}

// Callback when count reaches 0
void on_complete(uvrpc_waitgroup_t* wg, void* user_data) {
    printf("All tasks done!\n");
}

// Get current count
int count = uvrpc_waitgroup_get_count(&wg);

// Cleanup
uvrpc_waitgroup_cleanup(&wg);
```

## Common Patterns

### Limit Concurrent RPC Calls

```c
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 50); // Max 50 concurrent

for each request {
    uvrpc_semaphore_acquire(&sem, on_acquired, req);
}

void on_acquired(uvrpc_semaphore_t* sem, void* ctx) {
    make_rpc_call(..., sem);
}

void on_response(...) {
    uvrpc_semaphore_release((uvrpc_semaphore_t*)ctx);
}
```

### Wait for Multiple Calls

```c
uvrpc_barrier_t barrier;
uvrpc_barrier_init(&barrier, loop, count, on_complete, results);

for each call {
    make_rpc_call(..., &barrier);
}

void on_response(...) {
    store_result();
    uvrpc_barrier_wait(&barrier, error);
}

void on_complete(...) {
    // Aggregate results
}
```

### Track Async Operation

```c
uvrpc_promise_t promise;
uvrpc_promise_init(&promise, loop);
uvrpc_promise_then(&promise, callback, user_data);

make_async_call(..., &promise);

void callback(uvrpc_promise_t* promise, void* ctx) {
    if (uvrpc_promise_is_fulfilled(promise)) {
        uint8_t* result;
        size_t size;
        uvrpc_promise_get_result(promise, &result, &size);
        // Use result
    }
}
```

### Fire-and-Forget Tasks

```c
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop, on_complete, NULL);
uvrpc_waitgroup_add(&wg, task_count);

for each task {
    start_task(..., &wg);
}

void on_task_complete(...) {
    uvrpc_waitgroup_done(&wg);
}
```

## Best Practices

1. **Stack allocation**: Allocate primitives on stack when possible
2. **Always cleanup**: Call cleanup function when done
3. **Check return values**: Always check init function returns
4. **Never block**: Don't block in event loop callbacks
5. **User data**: Pass primitive pointer as user_data for cleanup

## Error Handling

```c
int ret = uvrpc_semaphore_init(&sem, loop, 10);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Error: %s\n", uvrpc_strerror(ret));
    // Handle error
    return ret;
}
```

## Memory Footprint

- Promise: ~200 bytes
- Semaphore: ~200 bytes + waiter queue
- Barrier: ~200 bytes
- WaitGroup: ~200 bytes

## Thread Safety

All primitives are thread-safe using atomic operations. Use separate event loops per thread.