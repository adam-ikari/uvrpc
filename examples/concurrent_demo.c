/**
 * UVRPC Concurrent Methods Demo
 * Demonstrates all, any, retry, timeout methods
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_async.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    char response[64];
    snprintf(response, sizeof(response), "Echo: %.*s", (int)req->params_size, (char*)req->params);
    uvrpc_request_send_response(req, 0, (uint8_t*)response, strlen(response));
    uvrpc_request_free(req);
}

static void slow_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    /* Simulate slow response */
    for (volatile int i = 0; i < 100000000; i++);
    uvrpc_request_send_response(req, 0, (uint8_t*)"Slow response", 13);
    uvrpc_request_free(req);
}

static void error_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, 5, NULL, 0); /* Error code 5 */
    uvrpc_request_free(req);
}

int main(void) {
    printf("=== UVRPC Concurrent Methods Demo ===\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create async context */
    uvrpc_async_ctx_t* async_ctx = uvrpc_async_ctx_new(&loop);
    
    /* Create server */
    printf("1. Creating server...\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://concurrent_test");
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_register(server, "slow", slow_handler, NULL);
    uvrpc_server_register(server, "error", error_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create clients */
    printf("2. Creating clients...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://concurrent_test");
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* clients[3];
    for (int i = 0; i < 3; i++) {
        clients[i] = uvrpc_client_create(client_config);
        uvrpc_client_connect(clients[i]);
    }
    
    /* Run event loop briefly to establish connections */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Demo 1: Promise.all */
    printf("\n3. Demo: Promise.all (wait for all concurrent calls)...\n");
    {
        const char* methods[] = {"echo", "echo", "echo"};
        const uint8_t* params[] = {(uint8_t*)"Hello1", (uint8_t*)"Hello2", (uint8_t*)"Hello3"};
        size_t params_sizes[] = {6, 6, 6};
        uvrpc_async_result_t** results = NULL;
        
        int status = uvrpc_async_all(async_ctx, clients, methods, params, params_sizes,
                                      &results, 3, 5000);
        if (status == UVRPC_OK) {
            for (int i = 0; i < 3; i++) {
                printf("  Result %d: %.*s\n", i, (int)results[i]->result_size, results[i]->result);
                uvrpc_async_result_free(results[i]);
            }
            uvrpc_free(results);
        }
    }
    
    /* Demo 2: Promise.any */
    printf("\n4. Demo: Promise.any (wait for any one to complete)...\n");
    {
        const char* methods[] = {"slow", "echo", "slow"};
        const uint8_t* params[] = {(uint8_t*)"test1", (uint8_t*)"test2", (uint8_t*)"test3"};
        size_t params_sizes[] = {5, 5, 5};
        uvrpc_async_result_t* result = NULL;
        int completed_index = -1;
        
        int status = uvrpc_async_any(async_ctx, clients, methods, params, params_sizes,
                                      &result, &completed_index, 3, 5000);
        if (status == UVRPC_OK) {
            printf("  First completed: index %d\n", completed_index);
            printf("  Result: %.*s\n", (int)result->result_size, result->result);
            uvrpc_async_result_free(result);
        }
    }
    
    /* Demo 3: Promise.retry */
    printf("\n5. Demo: Promise.retry (retry failed calls)...\n");
    {
        uvrpc_async_result_t* result = NULL;
        
        printf("  Attempting call that fails...\n");
        int status = uvrpc_async_retry(async_ctx, clients[0], "error",
                                         (uint8_t*)"test", 4, &result, 2, 100);
        if (status != UVRPC_OK) {
            printf("  All retries failed (expected)\n");
        }
        
        printf("  Attempting call that succeeds...\n");
        status = uvrpc_async_retry(async_ctx, clients[0], "echo",
                                    (uint8_t*)"retry_test", 10, &result, 2, 100);
        if (status == UVRPC_OK) {
            printf("  Result: %.*s\n", (int)result->result_size, result->result);
            uvrpc_async_result_free(result);
        }
    }
    
    /* Demo 4: Promise.timeout */
    printf("\n6. Demo: Promise.timeout (call with timeout)...\n");
    {
        uvrpc_async_result_t* result = NULL;
        
        printf("  Fast call with 1s timeout...\n");
        int status = uvrpc_async_timeout(async_ctx, clients[0], "echo",
                                          (uint8_t*)"fast", 4, &result, 1000);
        if (status == UVRPC_OK) {
            printf("  Result: %.*s\n", (int)result->result_size, result->result);
            uvrpc_async_result_free(result);
        }
        
        printf("  Slow call with 10ms timeout...\n");
        status = uvrpc_async_timeout(async_ctx, clients[0], "slow",
                                      (uint8_t*)"slow", 4, &result, 10);
        if (status == UVRPC_ERROR_TIMEOUT) {
            printf("  Timeout (expected)\n");
        }
    }
    
    /* Cleanup */
    printf("\n7. Cleanup...\n");
    for (int i = 0; i < 3; i++) {
        uvrpc_client_free(clients[i]);
    }
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uvrpc_async_ctx_free(async_ctx);
    uv_loop_close(&loop);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}