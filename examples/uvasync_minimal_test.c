/**
 * Minimal uvasync Test
 */

#include "../include/uvasync.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>

void simple_task(void* data, uvrpc_promise_t* promise) {
    int* input = (int*)data;
    int result = *input * 2;
    uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
}

int main(void) {
    printf("Testing uvasync...\n");
    
    /* Create context */
    uvasync_context_t* ctx = uvasync_context_create_new();
    if (!ctx) {
        printf("Failed to create context\n");
        return 1;
    }
    printf("Context created\n");
    
    /* Create scheduler */
    uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 2);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return 1;
    }
    printf("Scheduler created\n");
    
    /* Submit task */
    int value = 42;
    uvrpc_promise_t* promise = uvrpc_promise_create(ctx->loop);

    int ret = uvasync_submit(scheduler, simple_task, &value, promise);
    if (ret == UVASYNC_OK) {
        printf("Task submitted\n");

        ret = uvasync_scheduler_wait_all(scheduler, 5000);
        printf("Wait result: %d\n", ret);
    }
    
    /* Cleanup */
    printf("Cleaning up...\n");
    uvrpc_promise_destroy(promise);
    uvasync_scheduler_destroy(scheduler);
    uvasync_context_destroy(ctx);
    
    printf("Test completed\n");
    return 0;
}