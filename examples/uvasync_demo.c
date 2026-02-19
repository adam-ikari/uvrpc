/**
 * uvasync Demo - Step by Step Version
 */

#include "../include/uvasync.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void simple_task(void* data, uvrpc_promise_t* promise) {
    int* input = (int*)data;
    int result = *input * 2;
    printf("[Task] %d -> %d\n", *input, result);
    uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
}

void io_task(void* data, uvrpc_promise_t* promise) {
    int* input = (int*)data;
    printf("[Task] I/O for item %d\n", *input);
    usleep(5000);
    int success = 1;
    uvrpc_promise_resolve(promise, (uint8_t*)&success, sizeof(int));
}

void process_item(void* data, uvrpc_promise_t* promise) {
    int* item = (int*)data;
    printf("[Task] Processing item: %d\n", *item);
    usleep(2000);
    int processed = *item + 1000;
    uvrpc_promise_resolve(promise, (uint8_t*)&processed, sizeof(int));
}

int main(void) {
    printf("========================================\n");
    printf("  UVRPC Async Scheduler Demo\n");
    printf("========================================\n");
    
    uvasync_context_t* ctx = uvasync_context_create_new();
    if (!ctx) {
        printf("Failed to create context\n");
        return 1;
    }
    
    printf("\n=== Demo 1: Basic Scheduler ===\n");
    uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 2);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return 1;
    }
    
    /* Single task */
    int value = 42;
    uvrpc_promise_t* promise = uvrpc_promise_create(ctx->loop);
    uvasync_submit(scheduler, simple_task, &value, promise);
    uvasync_scheduler_wait_all(scheduler, 5000);
    uvrpc_promise_destroy(promise);
    
    /* Show stats */
    const uvasync_stats_t* stats = uvasync_scheduler_get_stats(scheduler);
    if (stats) {
        printf("  Submitted: %lu, Completed: %lu\n", stats->total_submitted, stats->total_completed);
    }
    
    uvasync_scheduler_destroy(scheduler);
    printf("Demo 1 completed!\n");
    
    printf("\n=== Demo 2: Multiple Tasks ===\n");
    scheduler = uvasync_scheduler_create(ctx, 5);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return 1;
    }
    
    /* 10 tasks */
    int values[10];
    uvrpc_promise_t* promises[10];
    for (int i = 0; i < 10; i++) {
        values[i] = i;
        promises[i] = uvrpc_promise_create(ctx->loop);
        uvasync_submit(scheduler, simple_task, &values[i], promises[i]);
    }
    
    uvasync_scheduler_wait_all(scheduler, 10000);
    
    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        uvrpc_promise_destroy(promises[i]);
    }
    uvasync_scheduler_destroy(scheduler);
    printf("Demo 2 completed!\n");
    
    printf("\n=== Demo 3: Batch Processing ===\n");
    scheduler = uvasync_scheduler_create(ctx, 10);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return 1;
    }
    
    /* Batch of 20 tasks */
    uvasync_task_t tasks[20];
    uvrpc_promise_t* batch_promises[20];
    int items[20];
    
    for (int i = 0; i < 20; i++) {
        items[i] = i;
        tasks[i].fn = process_item;
        tasks[i].data = &items[i];
        batch_promises[i] = uvrpc_promise_create(ctx->loop);
    }
    
    uvasync_submit_batch(scheduler, tasks, 20, batch_promises);
    uvasync_scheduler_wait_all(scheduler, 15000);
    
    /* Show results */
    int success = 0;
    for (int i = 0; i < 20; i++) {
        if (uvrpc_promise_is_fulfilled(batch_promises[i])) {
            success++;
        }
        uvrpc_promise_destroy(batch_promises[i]);
    }
    printf("Success: %d/20\n", success);
    
    uvasync_scheduler_destroy(scheduler);
    printf("Demo 3 completed!\n");
    
    printf("\n=== Demo 4: Convenience Functions ===\n");
    scheduler = uvasync_scheduler_create(ctx, 3);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return 1;
    }
    
    /* Blocking submit and wait */
    int input = 99;
    uint8_t* result = NULL;
    size_t result_size = 0;
    
    int ret = uvasync_submit_and_wait(scheduler, simple_task, &input,
                                         &result, &result_size, 5000);

    if (ret == UVASYNC_OK && result) {
        int value = *(int*)result;
        printf("Result: %d\n", value);
        uvrpc_free(result);
    }
    
    uvasync_scheduler_destroy(scheduler);
    printf("Demo 4 completed!\n");
    
    uvasync_context_destroy(ctx);
    
    printf("\n========================================\n");
    printf("  All demos completed!\n");
    printf("========================================\n");
    
    return 0;
}
