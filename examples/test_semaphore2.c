#include <stdio.h>
#include <stdlib.h>
#include "../include/uvrpc.h"
#include "../include/uvrpc_primitives.h"

static int callback_count = 0;

void test_semaphore_callback(uvrpc_promise_t* promise, void* data) {
    (void)promise;
    (void)data;
    callback_count++;
    printf("Callback #%d called\n", callback_count);
    fflush(stdout);
}

int main(void) {
    printf("=== Semaphore Callback Test ===\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create semaphore with 10 permits */
    uvrpc_semaphore_t sem;
    int ret = uvrpc_semaphore_init(&sem, &loop, 10);
    if (ret != UVRPC_OK) {
        printf("Failed to init semaphore: %d\n", ret);
        return 1;
    }
    
    printf("Semaphore initialized with 10 permits\n");
    
    /* Try to acquire 10 permits */
    for (int i = 0; i < 10; i++) {
        uvrpc_promise_t* promise = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
        uvrpc_promise_init(promise, &loop);
        uvrpc_promise_set_callback(promise, test_semaphore_callback, NULL);
        
        int result = uvrpc_semaphore_acquire_async(&sem, promise);
        printf("Acquire #%d: ret=%d\n", i, result);
        fflush(stdout);
    }
    
    printf("Submitted 10 acquire requests\n");
    printf("Callbacks called so far: %d\n", callback_count);
    fflush(stdout);
    
    /* Run event loop a few times */
    printf("Running event loop (5 iterations)...\n");
    for (int i = 0; i < 5; i++) {
        printf("Iteration %d...\n", i);
        fflush(stdout);
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    printf("Event loop finished\n");
    printf("Total callbacks called: %d\n", callback_count);
    fflush(stdout);
    
    /* Cleanup */
    uvrpc_semaphore_cleanup(&sem);
    uv_loop_close(&loop);
    
    printf("=== Test Complete ===\n");
    printf("Expected: 10 callbacks\n");
    printf("Actual: %d callbacks\n", callback_count);
    printf("Result: %s\n", (callback_count == 10) ? "PASS" : "FAIL");
    
    return (callback_count == 10) ? 0 : 1;
}