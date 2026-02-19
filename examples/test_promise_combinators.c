/**
 * @file test_promise_combinators.c
 * @brief Test JavaScript-style Promise combinators (all, race, allSettled)
 */

#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test context */
typedef struct {
    uv_loop_t* loop;
    int test_count;
    uvrpc_promise_t* promises[10];
    uvrpc_promise_t combined;
    volatile int completed;
} test_context_t;

/* Callback for Promise.all() combined result */
static void on_all_promise_complete(uvrpc_promise_t* promise, void* user_data) {
    test_context_t* ctx = (test_context_t*)user_data;
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("[Promise.all] All promises fulfilled!\n");
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        uvrpc_promise_get_result(promise, &result, &result_size);
        
        if (result && result_size > 0) {
            int count = *(int*)result;
            printf("[Promise.all] Result count: %d\n", count);
            
            uint8_t* ptr = result + sizeof(int);
            for (int i = 0; i < count; i++) {
                size_t size = *(size_t*)ptr;
                ptr += sizeof(size_t);
                
                if (size > 0 && size == sizeof(int)) {
                    int value = *(int*)ptr;
                    printf("[Promise.all] Result %d: %d\n", i, value);
                    ptr += size;
                }
            }
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Promise.all] Rejected: %s\n", error);
    }
    
    ctx->completed = 1;
}

/* Callback for Promise.race() combined result */
static void on_race_promise_complete(uvrpc_promise_t* promise, void* user_data) {
    test_context_t* ctx = (test_context_t*)user_data;
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("[Promise.race] First promise fulfilled!\n");
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        uvrpc_promise_get_result(promise, &result, &result_size);
        
        if (result && result_size == sizeof(int)) {
            int value = *(int*)result;
            printf("[Promise.race] First result: %d\n", value);
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Promise.race] Rejected: %s\n", error);
    }
    
    ctx->completed = 1;
}

/* Callback for Promise.allSettled() combined result */
static void on_all_settled_promise_complete(uvrpc_promise_t* promise, void* user_data) {
    test_context_t* ctx = (test_context_t*)user_data;
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("[Promise.allSettled] All promises settled!\n");
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        uvrpc_promise_get_result(promise, &result, &result_size);
        
        if (result && result_size > 0) {
            int count = *(int*)result;
            printf("[Promise.allSettled] Result count: %d\n", count);
            
            uint8_t* ptr = result + sizeof(int);
            for (int i = 0; i < count; i++) {
                uint8_t status = *(uint8_t*)ptr;
                ptr += sizeof(uint8_t);
                
                if (status == 1) { /* fulfilled */
                    size_t size = *(size_t*)ptr;
                    ptr += sizeof(size_t);
                    
                    if (size > 0 && size == sizeof(int)) {
                        int value = *(int*)ptr;
                        printf("[Promise.allSettled] Promise %d: fulfilled with value %d\n", i, value);
                        ptr += size;
                    }
                } else if (status == 2) { /* rejected */
                    int32_t error_code = *(int32_t*)ptr;
                    ptr += sizeof(int32_t);
                    
                    const char* error = (const char*)ptr;
                    printf("[Promise.allSettled] Promise %d: rejected with code %d, error: %s\n", 
                           i, error_code, error);
                    ptr += strlen(error) + 1;
                } else {
                    printf("[Promise.allSettled] Promise %d: pending\n", i);
                }
            }
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Promise.allSettled] Rejected: %s\n", error);
    }
    
    ctx->completed = 1;
}

int main() {
    /* Disable output buffering */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("=== Testing JavaScript-style Promise Combinators ===\n\n");
    
    /* Test 1: Promise.all() - all fulfill */
    printf("Test 1: Promise.all() - All promises fulfill\n");
    {
        test_context_t ctx;
        ctx.loop = &loop;
        ctx.test_count = 3;
        ctx.completed = 0;
        
        /* Initialize promises */
        for (int i = 0; i < 3; i++) {
            ctx.promises[i] = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
            uvrpc_promise_init(ctx.promises[i], &loop);
        }
        uvrpc_promise_init(&ctx.combined, &loop);
        
        /* Set callback for combined promise */
        uvrpc_promise_set_callback(&ctx.combined, on_all_promise_complete, &ctx);
        
        /* Set up Promise.all() */
        uvrpc_promise_all(ctx.promises, 3, &ctx.combined, &loop);
        
        /* Resolve all promises */
        for (int i = 0; i < 3; i++) {
            int value = (i + 1) * 10;
            uvrpc_promise_resolve(ctx.promises[i], (uint8_t*)&value, sizeof(int));
        }
        
        /* Run event loop until completion */
        while (!ctx.completed) {
            uv_run(&loop, UV_RUN_NOWAIT);
            usleep(1000);
        }
        
        /* Cleanup */
        for (int i = 0; i < 3; i++) {
            uvrpc_promise_cleanup(ctx.promises[i]);
            free(ctx.promises[i]);
        }
        uvrpc_promise_cleanup(&ctx.combined);
    }
    printf("\n");
    
    /* Test 2: Promise.all() - one rejects */
    printf("Test 2: Promise.all() - One promise rejects\n");
    {
        test_context_t ctx;
        ctx.loop = &loop;
        ctx.test_count = 3;
        ctx.completed = 0;
        
        /* Initialize promises */
        for (int i = 0; i < 3; i++) {
            ctx.promises[i] = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
            uvrpc_promise_init(ctx.promises[i], &loop);
        }
        uvrpc_promise_init(&ctx.combined, &loop);
        
        /* Set callback for combined promise */
        uvrpc_promise_set_callback(&ctx.combined, on_all_promise_complete, &ctx);
        
        /* Set up Promise.all() */
        uvrpc_promise_all(ctx.promises, 3, &ctx.combined, &loop);
        
        /* Resolve first two, reject third */
        int value1 = 100;
        int value2 = 200;
        uvrpc_promise_resolve(ctx.promises[0], (uint8_t*)&value1, sizeof(int));
        uvrpc_promise_resolve(ctx.promises[1], (uint8_t*)&value2, sizeof(int));
        uvrpc_promise_reject(ctx.promises[2], -1, "Simulated error");
        
        /* Run event loop until completion */
        while (!ctx.completed) {
            uv_run(&loop, UV_RUN_NOWAIT);
            usleep(1000);
        }
        
        /* Cleanup */
        for (int i = 0; i < 3; i++) {
            uvrpc_promise_cleanup(ctx.promises[i]);
            free(ctx.promises[i]);
        }
        uvrpc_promise_cleanup(&ctx.combined);
    }
    printf("\n");
    
    /* Test 3: Promise.race() - first fulfills */
    printf("Test 3: Promise.race() - First promise fulfills\n");
    {
        /* Run event loop multiple times to clean up any pending handles from previous tests */
        for (int i = 0; i < 50; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        
        test_context_t ctx;
        ctx.loop = &loop;
        ctx.test_count = 3;
        ctx.completed = 0;
        
        /* Initialize promises */
        for (int i = 0; i < 3; i++) {
            ctx.promises[i] = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
            uvrpc_promise_init(ctx.promises[i], &loop);
        }
        uvrpc_promise_init(&ctx.combined, &loop);
        
        /* Set callback for combined promise */
        uvrpc_promise_set_callback(&ctx.combined, on_race_promise_complete, &ctx);
        
        /* Set up Promise.race() */
        uvrpc_promise_race(ctx.promises, 3, &ctx.combined, &loop);
        
        /* Resolve first promise (others remain pending) */
        int value = 999;
        uvrpc_promise_resolve(ctx.promises[0], (uint8_t*)&value, sizeof(int));
        
        /* Run event loop until completion or timeout */
        int iterations = 0;
        while (!ctx.completed && iterations < 100) {
            uv_run(&loop, UV_RUN_NOWAIT);
            iterations++;
        }
        
        if (!ctx.completed) {
            printf("[Promise.race] Timeout - callback not called\n");
        }
        
        /* Cleanup - resolve remaining promises to avoid blocking event loop */
        for (int i = 1; i < 3; i++) {
            if (uvrpc_promise_is_pending(ctx.promises[i])) {
                int dummy = 0;
                uvrpc_promise_resolve(ctx.promises[i], (uint8_t*)&dummy, sizeof(int));
            }
        }
        
        /* Run event loop to clean up pending callbacks */
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        
        /* Cleanup */
        for (int i = 0; i < 3; i++) {
            uvrpc_promise_cleanup(ctx.promises[i]);
            free(ctx.promises[i]);
        }
        uvrpc_promise_cleanup(&ctx.combined);
    }
    printf("\n");
    
    /* Test 4: Promise.allSettled() - mix of fulfill and reject */
    printf("Test 4: Promise.allSettled() - Mix of fulfill and reject\n");
    {
        test_context_t ctx;
        ctx.loop = &loop;
        ctx.test_count = 4;
        ctx.completed = 0;
        
        /* Initialize promises */
        for (int i = 0; i < 4; i++) {
            ctx.promises[i] = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
            uvrpc_promise_init(ctx.promises[i], &loop);
        }
        uvrpc_promise_init(&ctx.combined, &loop);
        
        /* Set callback for combined promise */
        uvrpc_promise_set_callback(&ctx.combined, on_all_settled_promise_complete, &ctx);
        
        /* Set up Promise.allSettled() */
        uvrpc_promise_all_settled(ctx.promises, 4, &ctx.combined, &loop);
        
        /* Mix of fulfill and reject */
        int value1 = 111;
        int value3 = 333;
        uvrpc_promise_resolve(ctx.promises[0], (uint8_t*)&value1, sizeof(int));
        uvrpc_promise_reject(ctx.promises[1], -2, "Error 1");
        uvrpc_promise_resolve(ctx.promises[2], (uint8_t*)&value3, sizeof(int));
        uvrpc_promise_reject(ctx.promises[3], -3, "Error 2");
        
        /* Run event loop until completion */
        while (!ctx.completed) {
            uv_run(&loop, UV_RUN_NOWAIT);
            usleep(1000);
        }
        
        /* Cleanup */
        for (int i = 0; i < 4; i++) {
            uvrpc_promise_cleanup(ctx.promises[i]);
            free(ctx.promises[i]);
        }
        uvrpc_promise_cleanup(&ctx.combined);
    }
    printf("\n");
    
    printf("=== All tests completed ===\n");
    
    uv_loop_close(&loop);
    return 0;
}