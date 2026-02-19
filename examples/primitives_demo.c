/**
 * @file primitives_demo.c
 * @brief UVRPC Async Programming Primitives Demo
 * 
 * Demonstrates the use of Promise, Semaphore, WaitGroup, and Promise Combinators
 * for concurrent control in async RPC programming.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 2.0
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define SERVER_ADDRESS "tcp://127.0.0.1:5555"
#define MAX_CONCURRENT_CALLS 10
#define TOTAL_REQUESTS 50

/* ============================================================================
 * Global State
 * ============================================================================ */

static int requests_completed = 0;
static int requests_failed = 0;

/* ============================================================================
 * Semaphore Demo - Limit Concurrent RPC Calls (JavaScript-style)
 * ============================================================================ */

typedef struct {
    uvrpc_client_t* client;
    int request_id;
    char method[32];
    uvrpc_promise_t* acquire_promise;
} semaphore_request_t;

/* Callback when semaphore permit is acquired (via Promise) */
static void on_semaphore_acquired(uvrpc_promise_t* promise, void* user_data) {
    semaphore_request_t* req = (semaphore_request_t*)user_data;
    
    printf("[Semaphore] Request %d acquired permit, making RPC call to %s\n", 
           req->request_id, req->method);
    
    /* Make RPC call */
    uint8_t params[4];
    memcpy(params, &req->request_id, sizeof(req->request_id));
    
    /* Note: In a real app, you'd pass the semaphore to the RPC callback to release later */
    uvrpc_client_call(req->client, req->method, params, sizeof(params), 
                      NULL, NULL);
    
    /* For demo purposes, release immediately after call */
    /* In real code, release in the RPC response callback */
    uvrpc_semaphore_t* sem = (uvrpc_semaphore_t*)uvrpc_promise_get_error(promise);  /* Hack: pass sem through error */
    if (sem) {
        uvrpc_semaphore_release(sem);
        requests_completed++;
    }
    
    uvrpc_promise_cleanup(req->acquire_promise);
    free(req->acquire_promise);
}

/* Run semaphore demo */
static void run_semaphore_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== Semaphore Demo (JavaScript-style) ===\n");
    printf("Limiting concurrent RPC calls to %d\n", MAX_CONCURRENT_CALLS);
    printf("Making %d total requests\n\n", TOTAL_REQUESTS);
    
    uvrpc_semaphore_t sem;
    uvrpc_semaphore_init(&sem, loop, MAX_CONCURRENT_CALLS);
    
    /* Create and queue requests */
    semaphore_request_t* requests = malloc(sizeof(semaphore_request_t) * TOTAL_REQUESTS);
    
    for (int i = 0; i < TOTAL_REQUESTS; i++) {
        requests[i].client = client;
        requests[i].request_id = i;
        snprintf(requests[i].method, sizeof(requests[i].method), "echo_%d", i % 5);
        
        /* Create promise for acquiring permit */
        requests[i].acquire_promise = malloc(sizeof(uvrpc_promise_t));
        uvrpc_promise_init(requests[i].acquire_promise, loop);
        
        /* Hack: pass semaphore pointer through error field */
        requests[i].acquire_promise->error_code = (int32_t)(intptr_t)&sem;
        
        /* Set callback for when permit is acquired */
        uvrpc_promise_then(requests[i].acquire_promise, on_semaphore_acquired, &requests[i]);
        
        /* Acquire permit asynchronously (JavaScript-style) */
        uvrpc_semaphore_acquire_async(&sem, requests[i].acquire_promise);
    }
    
    /* Wait for all requests to complete */
    while (requests_completed + requests_failed < TOTAL_REQUESTS) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000); /* 10ms */
    }
    
    printf("\n[Semaphore] Demo completed: %d succeeded, %d failed\n", 
           requests_completed, requests_failed);
    
    uvrpc_semaphore_cleanup(&sem);
    free(requests);
}

/* ============================================================================
 * Promise.all() Demo - Wait for Multiple RPC Calls
 * ============================================================================ */

typedef struct {
    uvrpc_client_t* client;
    int call_id;
    uvrpc_promise_t* promise;
} all_request_t;

/* Callback for each RPC call */
static void on_all_response(uvrpc_response_t* resp, void* ctx) {
    all_request_t* req = (all_request_t*)ctx;
    
    if (resp->error_code == 0) {
        printf("[Promise.all] Call %d completed\n", req->call_id);
        uvrpc_promise_resolve(req->promise, resp->result, resp->result_size);
    } else {
        printf("[Promise.all] Call %d failed: %s\n", req->call_id, resp->error_message);
        uvrpc_promise_reject(req->promise, resp->error_code, resp->error_message);
    }
}

/* Callback when all calls complete */
static void on_all_complete(uvrpc_promise_t* promise, void* user_data) {
    (void)user_data;
    
    printf("\n[Promise.all] All calls completed!\n");
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        uint8_t* result;
        size_t size;
        uvrpc_promise_get_result(promise, &result, &size);
        
        /* Result is concatenated array of individual results */
        int* int_result = (int*)result;
        int count = size / sizeof(int);
        
        printf("[Promise.all] Combined results (%d items): ", count);
        for (int i = 0; i < count && i < 10; i++) {
            printf("%d ", int_result[i]);
        }
        if (count > 10) printf("...");
        printf("\n");
        
        free(result);
    } else {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Promise.all] Failed: %s (code: %d)\n", error, uvrpc_promise_get_error_code(promise));
    }
}

/* Run Promise.all() demo */
static void run_promise_all_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== Promise.all() Demo ===\n");
    printf("Making 5 concurrent RPC calls and waiting for all\n\n");
    
    uvrpc_promise_t* promises[5];
    all_request_t requests[5];
    
    /* Create promises for each call */
    for (int i = 0; i < 5; i++) {
        promises[i] = malloc(sizeof(uvrpc_promise_t));
        uvrpc_promise_init(promises[i], loop);
        
        requests[i].client = client;
        requests[i].call_id = i;
        requests[i].promise = promises[i];
        
        uint8_t params[4];
        memcpy(params, &i, sizeof(i));
        
        uvrpc_client_call(client, "compute", params, sizeof(params), 
                          on_all_response, &requests[i]);
    }
    
    /* Create combined promise for Promise.all() */
    uvrpc_promise_t combined;
    uvrpc_promise_init(&combined, loop);
    
    /* Wait for all promises */
    uvrpc_promise_all(promises, 5, &combined, loop);
    
    /* Set completion callback */
    uvrpc_promise_then(&combined, on_all_complete, NULL);
    
    /* Wait for all to complete */
    while (uvrpc_promise_is_pending(&combined)) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    /* Cleanup */
    for (int i = 0; i < 5; i++) {
        uvrpc_promise_cleanup(promises[i]);
        free(promises[i]);
    }
    uvrpc_promise_cleanup(&combined);
}

/* ============================================================================
 * Promise Demo - Simple Async Operation
 * ============================================================================ */

typedef struct {
    uvrpc_promise_t* promise;
    int request_id;
} promise_context_t;

/* Callback for RPC response */
static void on_promise_response(uvrpc_response_t* resp, void* ctx) {
    promise_context_t* pctx = (promise_context_t*)ctx;
    
    if (resp->error_code != 0) {
        printf("[Promise] Request %d failed: %s\n", pctx->request_id, resp->error_message);
        uvrpc_promise_reject(pctx->promise, resp->error_code, "RPC call failed");
        return;
    }
    
    printf("[Promise] Request %d succeeded\n", pctx->request_id);
    
    /* Resolve promise with result */
    uvrpc_promise_resolve(pctx->promise, resp->result, resp->result_size);
}

/* Final callback when promise completes */
static void on_promise_complete(uvrpc_promise_t* promise, void* user_data) {
    printf("\n[Promise] Operation completed!\n");
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        uint8_t* result;
        size_t size;
        uvrpc_promise_get_result(promise, &result, &size);
        printf("[Promise] Success! Result size: %zu bytes\n", size);
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Promise] Failed! Error: %s (code: %d)\n", error, uvrpc_promise_get_error_code(promise));
    }
}

/* Run promise demo */
static void run_promise_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== Promise Demo ===\n");
    printf("Making async RPC call with promise\n\n");
    
    uvrpc_promise_t promise;
    uvrpc_promise_init(&promise, loop);
    
    promise_context_t pctx;
    pctx.promise = &promise;
    pctx.request_id = 1;
    
    /* Set completion callback */
    uvrpc_promise_then(&promise, on_promise_complete, NULL);
    
    /* Make RPC call */
    uint8_t params[4] = {1, 2, 3, 4};
    uvrpc_client_call(client, "echo", params, sizeof(params),
                      on_promise_response, &pctx);
    
    /* Wait for promise to complete */
    while (uvrpc_promise_is_pending(&promise)) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    uvrpc_promise_cleanup(&promise);
}

/* ============================================================================
 * WaitGroup Demo - Simplified Concurrent Operations (JavaScript-style)
 * ============================================================================ */

typedef struct {
    uvrpc_waitgroup_t* wg;
    uvrpc_client_t* client;
    int task_id;
} waitgroup_task_t;

/* Callback for each task */
static void on_waitgroup_task(uvrpc_response_t* resp, void* ctx) {
    waitgroup_task_t* task = (waitgroup_task_t*)ctx;
    
    if (resp->error_code == 0) {
        printf("[WaitGroup] Task %d completed\n", task->task_id);
    } else {
        printf("[WaitGroup] Task %d failed: %s\n", task->task_id, resp->error_message);
    }
    
    /* Signal task complete */
    uvrpc_waitgroup_done(task->wg);
}

/* Callback when all tasks complete */
static void on_waitgroup_complete(uvrpc_promise_t* promise, void* user_data) {
    (void)promise;  /* Promise is always fulfilled */
    int* task_count = (int*)user_data;
    printf("\n[WaitGroup] All %d tasks completed!\n", *task_count);
}

/* Run waitgroup demo */
static void run_waitgroup_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== WaitGroup Demo (JavaScript-style) ===\n");
    printf("Running 10 concurrent tasks\n\n");
    
    int task_count = 10;
    uvrpc_waitgroup_t wg;
    uvrpc_waitgroup_init(&wg, loop);
    
    waitgroup_task_t tasks[10];
    
    /* Add all tasks to waitgroup */
    uvrpc_waitgroup_add(&wg, 10);
    
    /* Start all tasks */
    for (int i = 0; i < 10; i++) {
        tasks[i].wg = &wg;
        tasks[i].client = client;
        tasks[i].task_id = i;
        
        uint8_t params[4];
        memcpy(params, &i, sizeof(i));
        
        uvrpc_client_call(client, "processTask", params, sizeof(params), 
                          on_waitgroup_task, &tasks[i]);
    }
    
    /* Get completion promise and set callback */
    uvrpc_promise_t wg_promise;
    uvrpc_promise_init(&wg_promise, loop);
    uvrpc_waitgroup_get_promise(&wg, &wg_promise);
    uvrpc_promise_then(&wg_promise, on_waitgroup_complete, &task_count);
    
    /* Wait for all tasks to complete */
    while (uvrpc_get_count(&wg) > 0) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    uvrpc_promise_cleanup(&wg_promise);
    uvrpc_waitgroup_cleanup(&wg);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create client */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, SERVER_ADDRESS);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    
    printf("Connecting to server at %s...\n", SERVER_ADDRESS);
    int ret = uvrpc_client_connect(client);
    if (ret != UVRPC_OK) {
        printf("Failed to connect: %s\n", uvrpc_strerror(ret));
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    printf("Connected!\n");
    
    /* Run demos */
    run_promise_demo(&loop, client);
    run_promise_all_demo(&loop, client);  /* Replaces Barrier demo */
    run_semaphore_demo(&loop, client);
    run_waitgroup_demo(&loop, client);
    
    /* Cleanup */
    printf("\n=== All Demos Complete ===\n");
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    
    uv_loop_close(&loop);
    
    return 0;
}