/**
 * UVRPC IPC Diagnostic Test
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>

void handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[Server] Request: %s\n", req->method);
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <address>\n", argv[0]);
        printf("Example: %s ipc:///tmp/test.sock\n", argv[0]);
        return 1;
    }
    
    const char* address = argv[1];
    printf("=== IPC Diagnostic Test ===\n");
    printf("Address: %s\n\n", address);
    
    uv_loop_t loop;
    int loop_result = uv_loop_init(&loop);
    printf("uv_loop_init: %d\n", loop_result);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    
    printf("Config transport: %d\n", config->transport);
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    printf("Server created\n");
    
    uvrpc_server_register(server, "test", handler, NULL);
    printf("Handler registered\n");
    
    printf("Starting server...\n");
    int result = uvrpc_server_start(server);
    printf("uvrpc_server_start returned: %d\n", result);
    
    if (result != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", result);
        return 1;
    }
    
    printf("Server running!\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
