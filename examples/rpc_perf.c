/**
 * UVRPC Performance Test - Simple Echo
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_received = 0;
static int g_sent = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK) {
        g_received++;
    }
}

int main() {
    const char* msg = "Hello";
    int num = 100;
    
    printf("UVRPC Performance Test\n");
    printf("Testing %d RPC calls...\n\n", num);
    
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "inproc://test");
    
    /* Server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    /* Client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_client_connect(client);
    
    /* Run loop briefly */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    /* Test */
    printf("Sending requests...\n");
    for (int i = 0; i < num; i++) {
        uvrpc_client_call(client, "echo",
                         (const uint8_t*)msg, strlen(msg),
                         response_callback, NULL);
        g_sent++;
        
        /* Process events */
        for (int j = 0; j < 10; j++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
    }
    
    /* Wait for responses */
    int timeout = 100; /* 10 seconds max */
    while (g_received < num && timeout > 0) {
        uv_run(&loop, UV_RUN_ONCE);
        timeout--;
        usleep(100000);
    }
    
    /* Results */
    printf("\nResults:\n");
    printf("  Sent: %d\n", g_sent);
    printf("  Received: %d\n", g_received);
    printf("  Success Rate: %.1f%%\n", (g_received * 100.0 / num));
    
    /* Cleanup */
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
