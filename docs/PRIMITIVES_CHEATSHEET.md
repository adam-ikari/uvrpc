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

// Acquire before operation (JavaScript-style)
uvrpc_promise_t* permit_promise = uvrpc_promise_create(loop);
uvrpc_promise_then(permit_promise, on_acquired, request_data);
uvrpc_semaphore_acquire_async(&sem, permit_promise);

void on_acquired(uvrpc_promise_t* promise, void* ctx) {
    // Permit acquired, do work
    make_rpc_call(..., &sem);
    uvrpc_promise_destroy(promise);
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

## Promise.all() - Wait for All Promises

```c
// Create promises
uvrpc_promise_t* promises[5];
for (int i = 0; i < 5; i++) {
    promises[i] = uvrpc_promise_create(loop);
    // ... async operations that resolve/reject promises[i] ...
}

// Combine and wait for all
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
        free(result);
    }
}
```

## Promise.race() - First to Complete

```c
// Create promises
uvrpc_promise_t* promises[3];
// ... create promises ...

// Wait for first to complete
uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_race(promises, 3, &combined, loop);
uvrpc_promise_then(&combined, on_first_complete, NULL);
```

## Promise.allSettled() - All Complete

```c
// Create promises
uvrpc_promise_t* promises[10];
// ... create promises ...

// Wait for all to complete (fulfill or reject)
uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_all_settled(promises, 10, &combined, loop);
uvrpc_promise_then(&combined, on_all_settled, NULL);
```

## WaitGroup

```c
// Initialize
uvrpc_waitgroup_t wg;
uvrpc_waitgroup_init(&wg, loop);

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

// Get completion promise
uvrpc_promise_t* done_promise = uvrpc_promise_create(loop);
uvrpc_promise_then(done_promise, on_complete, NULL);
uvrpc_waitgroup_get_promise(&wg, done_promise);

// Get current count
int count = uvrpc_get_count(&wg);

// Cleanup
uvrpc_waitgroup_cleanup(&wg);
```

## Common Patterns

### Limit Concurrent RPC Calls

```c
uvrpc_semaphore_t sem;
uvrpc_semaphore_init(&sem, loop, 50); // Max 50 concurrent

for each request {
    uvrpc_promise_t* permit_promise = uvrpc_promise_create(loop);
    uvrpc_promise_then(permit_promise, on_acquired, req);
    uvrpc_semaphore_acquire_async(&sem, permit_promise);
}

void on_acquired(uvrpc_promise_t* promise, void* ctx) {
    make_rpc_call(..., &sem);
    uvrpc_promise_destroy(promise);
}

void on_response(...) {
    uvrpc_semaphore_release((uvrpc_semaphore_t*)ctx);
}
```

### Wait for Multiple Calls

```c
uvrpc_promise_t* promises[count];
for each call {
    promises[i] = uvrpc_promise_create(loop);
    make_rpc_call(..., promises[i]);
}

uvrpc_promise_t combined;
uvrpc_promise_init(&combined, loop);
uvrpc_promise_all(promises, count, &combined, loop);
uvrpc_promise_then(&combined, on_complete, NULL);

void on_complete(...) {
    // Aggregate results
}
```

### Convenience Functions

```c
// Create and destroy promises
uvrpc_promise_t* p = uvrpc_promise_create(loop);
// ... use promise ...
uvrpc_promise_destroy(p);

// Synchronous Promise.all()
uint8_t* result = NULL;
size_t result_size = 0;
int ret = uvrpc_promise_all_sync(promises, count, &result, &result_size, loop);
if (ret == UVRPC_OK) {
    // Use result
    free(result);
}

// Synchronous wait
int ret = uvrpc_promise_wait(promise);
if (ret == UVRPC_OK) {
    // Promise fulfilled
}
```

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