/**
 * UVRPC Async Scheduler Demo - Simplified Version
 * Demonstrates the uvasync async concurrency control abstraction
 */

#include "../include/uvasync.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Simple task */
void simple_task(void* data, uvrpc_promise_t* promise) {
    int* input = (int*)data;
    int result = *input * 2;
    
    printf("[Task] Processing %d -> %d\n", *input, result);
    
    /* Resolve with result */
    uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
}

/* Demo 1: Basic context creation */
void demo_basic_context(void) {
    printf("\n=== Demo 1: Basic Context Creation ===\n");
    
    /* Create context */
    printf("Creating context...\n");
    uvasync_context_t* ctx = uvasync_context_create_new();
    if (!ctx) {
        printf("Failed to create context\n");
        return;
    }
    
    printf("Context created successfully\n");
    
    /* Cleanup */
    printf("Destroying context...\n");
    uvasync_context_destroy(ctx);
    
    printf("Demo 1 completed!\n");
}

/* Demo 2: Simple scheduler test */
void demo_simple_scheduler(void) {
    printf("\n=== Demo 2: Simple Scheduler Test ===\n");
    
    /* Create context */
    uvasync_context_t* ctx = uvasync_context_create_new();
    if (!ctx) {
        printf("Failed to create context\n");
        return;
    }
    
    /* Create scheduler with low concurrency */
    printf("Creating scheduler (max 2 concurrent)...\n");
    uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 2);
    if (!scheduler) {
        printf("Failed to create scheduler\n");
        uvasync_context_destroy(ctx);
        return;
    }
    
    printf("Scheduler created successfully\n");
    
    /* Submit a single task */
    printf("Submitting single task...\n");
    int value = 42;
    uvrpc_promise_t* promise = uvrpc_promise_create(ctx->loop);
    
    int ret = uvasync_submit(scheduler, simple_task, &value, promise);
    if (ret == UVRPC_OK) {
        printf("Task submitted successfully\n");
        
        /* Wait for completion */
        printf("Waiting for task completion...\n");
        uvasync_scheduler_wait_all(scheduler, 5000);
        
        /* Check statistics */
        const uvasync_stats_t* stats = uvasync_scheduler_get_stats(scheduler);
        if (stats) {
            printf("Statistics:\n");
            printf("  Submitted: %lu\n", stats->total_submitted);
            printf("  Completed: %lu\n", stats->total_completed);
        }
    } else {
        printf("Task submission failed: %d\n", ret);
    }
    
    /* Cleanup */
    printf("Destroying scheduler...\n");
    uvasync_scheduler_destroy(scheduler);
    
    printf("Destroying context...\n");
    uvasync_context_destroy(ctx);
    
    printf("Demo 2 completed!\n");
}

/* Main */
int main(void) {
    printf("========================================\n");
    printf("  UVRPC Async Scheduler Demo\n");
    printf("========================================\n");
    
    /* Run demos */
    demo_basic_context();
    demo_simple_scheduler();
    
    printf("\n========================================\n");
    printf("  All demos completed!\n");
    printf("========================================\n");
    
    return 0;
}