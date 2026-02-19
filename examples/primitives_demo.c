/**
 * @file primitives_demo.c
 * @brief UVRPC Async Programming Primitives Demo
 * 
 * Demonstrates the use of Promise, Semaphore, and Barrier patterns
 * for concurrent control in async RPC programming.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
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

/* Forward declarations */
static void on_semaphore_response(uvrpc_response_t* resp, void* ctx);

/* ============================================================================
 * Semaphore Demo - Limit Concurrent RPC Calls
 * ============================================================================ */

typedef struct {
    uvrpc_client_t* client;
    int request_id;
    char method[32];
} semaphore_request_t;

/* Callback when semaphore permit is acquired */
static void on_semaphore_acquired(uvrpc_semaphore_t* sem, void* user_data) {
    semaphore_request_t* req = (semaphore_request_t*)user_data;
    
    printf("[Semaphore] Request %d acquired permit, making RPC call to %s\n", 
           req->request_id, req->method);
    
    /* Make RPC call */
    uint8_t params[4];
    memcpy(params, &req->request_id, sizeof(req->request_id));
    
    uvrpc_client_call(req->client, req->method, params, sizeof(params), 
                      on_semaphore_response, sem);
}

/* Callback for RPC response */
static void on_semaphore_response(uvrpc_response_t* resp, void* ctx) {
    uvrpc_semaphore_t* sem = (uvrpc_semaphore_t*)ctx;
    
    if (resp->error_code == 0) {
        requests_completed++;
        printf("[Semaphore] RPC call completed successfully\n");
    } else {
        requests_failed++;
        printf("[Semaphore] RPC call failed: %s\n", resp->error_message);
    }
    
    /* Release semaphore permit */
    uvrpc_semaphore_release(sem);
}

/* Run semaphore demo */
static void run_semaphore_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== Semaphore Demo ===\n");
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
        
        uvrpc_semaphore_acquire(&sem, on_semaphore_acquired, &requests[i]);
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
 * Barrier Demo - Wait for Multiple RPC Calls
 * ============================================================================ */

typedef struct {
    uvrpc_barrier_t* barrier;
    int call_id;
    int* results; /* Array to store results */
} barrier_request_t;

/* Callback for each RPC call */
static void on_barrier_response(uvrpc_response_t* resp, void* ctx) {
    barrier_request_t* req = (barrier_request_t*)ctx;
    int error = (resp->error_code != 0);
    
    if (!error) {
        /* Store result */
        if (resp->result && resp->result_size >= sizeof(int)) {
            req->results[req->call_id] = *(int*)resp->result;
            printf("[Barrier] Call %d completed with result: %d\n", 
                   req->call_id, req->results[req->call_id]);
        }
    } else {
        printf("[Barrier] Call %d failed: %s\n", req->call_id, resp->error_message);
    }
    
    /* Signal barrier that this call is done */
    uvrpc_barrier_wait(req->barrier, error);
}

/* Callback when all barrier calls complete */
static void on_barrier_complete(uvrpc_barrier_t* barrier, void* user_data) {
    printf("\n[Barrier] All calls completed!\n");
    printf("[Barrier] Errors: %d\n", uvrpc_barrier_get_error_count(barrier));
    
    /* Print results */
    int* results = (int*)user_data;
    printf("[Barrier] Results: ");
    for (int i = 0; i < 5; i++) {
        printf("%d ", results[i]);
    }
    printf("\n");
}

/* Run barrier demo */
static void run_barrier_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== Barrier Demo ===\n");
    printf("Making 5 concurrent RPC calls and waiting for all\n\n");
    
    int results[5] = {0};
    uvrpc_barrier_t barrier;
    uvrpc_barrier_init(&barrier, loop, 5, on_barrier_complete, results);
    
    barrier_request_t requests[5];
    
    /* Make 5 concurrent calls */
    for (int i = 0; i < 5; i++) {
        requests[i].barrier = &barrier;
        requests[i].call_id = i;
        requests[i].results = results;
        
        uint8_t params[4];
        memcpy(params, &i, sizeof(i));
        
        uvrpc_client_call(client, "compute", params, sizeof(params), 
                          on_barrier_response, &requests[i]);
    }
    
    /* Wait for barrier to complete */
    while (!uvrpc_barrier_is_complete(&barrier)) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    uvrpc_barrier_cleanup(&barrier);
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
 * WaitGroup Demo - Simplified Concurrent Operations
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
static void on_waitgroup_complete(uvrpc_waitgroup_t* wg, void* user_data) {
    printf("\n[WaitGroup] All %d tasks completed!\n", *(int*)user_data);
}

/* Run waitgroup demo */
static void run_waitgroup_demo(uv_loop_t* loop, uvrpc_client_t* client) {
    printf("\n=== WaitGroup Demo ===\n");
    printf("Running 10 concurrent tasks\n\n");
    
    int task_count = 10;
    uvrpc_waitgroup_t wg;
    uvrpc_waitgroup_init(&wg, loop, on_waitgroup_complete, &task_count);
    
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
    
    /* Wait for all tasks to complete */
    while (uvrpc_get_count(&wg) > 0) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
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
    run_barrier_demo(&loop, client);
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