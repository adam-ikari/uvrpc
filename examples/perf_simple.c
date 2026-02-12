/**
 * UVRPC Simple Performance Test
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_responses_received = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK) {
        g_responses_received++;
    }
}

int main() {
    printf("UVRPC Simple Performance Test\n");
    printf("============================\n\n");
    
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Configure */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5560");
    
    /* Create server */
    printf("Creating server...\n");
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    printf("Creating client...\n");
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_client_connect(client);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Test */
    printf("\nRunning 100 RPC calls...\n");
    const char* msg = "Hello";
    int num_requests = 100;
    
    clock_t start = clock();
    
    for (int i = 0; i < num_requests; i++) {
        uvrpc_client_call(client, "echo",
                         (const uint8_t*)msg, strlen(msg),
                         response_callback, NULL);
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Wait for responses */
    while (g_responses_received < num_requests) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("\nResults:\n");
    printf("  Requests: %d\n", num_requests);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Throughput: %.0f req/s\n", num_requests / elapsed);
    printf("  Latency: %.3f ms\n", (elapsed / num_requests) * 1000);
    
    /* Cleanup */
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
