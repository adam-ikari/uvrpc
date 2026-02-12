/**
 * UVRPC Async/Await Demo
 * Demonstrates coroutine-like async/await syntax
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_async.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Handler for echo requests */
static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[Server] Received request: %s\n", req->method);
    uvrpc_request_send_response(req, 0, (uint8_t*)"Hello from server", 17);
    uvrpc_request_free(req);
}

int main(void) {
    printf("=== UVRPC Async/Await Demo ===\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create async context */
    uvrpc_async_ctx_t* async_ctx = uvrpc_async_ctx_new(&loop);
    
    /* Create server */
    printf("1. Creating server...\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://async_test");
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    printf("2. Creating client...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://async_test");
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect(client);
    
    /* Run event loop briefly to establish connection */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    printf("\n3. Using async/await to make RPC calls...\n");
    
    /* Execute async block */
    UVRPC_ASYNC(async_ctx, 5000) {
        /* Call 1 */
        uvrpc_async_result_t* result1 = NULL;
        UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "echo",
                                               (uint8_t*)"test1", 5, &result1));
        if (result1 && result1->error_code == 0) {
            printf("Response 1: %.*s\n", (int)result1->result_size, result1->result);
        }
        if (result1) uvrpc_async_result_free(result1);
        
        /* Call 2 */
        uvrpc_async_result_t* result2 = NULL;
        UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "echo",
                                               (uint8_t*)"test2", 5, &result2));
        if (result2 && result2->error_code == 0) {
            printf("Response 2: %.*s\n", (int)result2->result_size, result2->result);
        }
        if (result2) uvrpc_async_result_free(result2);
        
        /* Call 3 */
        uvrpc_async_result_t* result3 = NULL;
        UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "echo",
                                               (uint8_t*)"test3", 5, &result3));
        if (result3 && result3->error_code == 0) {
            printf("Response 3: %.*s\n", (int)result3->result_size, result3->result);
        }
        if (result3) uvrpc_async_result_free(result3);
    }
    
async_cleanup:
    /* Cleanup */
    printf("\n4. Cleanup...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uvrpc_async_ctx_free(async_ctx);
    uv_loop_close(&loop);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}