/**
 * UVRPC TCP Performance Test - Minimal
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    printf("=== UVRPC TCP Performance Test ===\n");
    printf("Address: %s\n\n", address);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    
    printf("Creating server...\n");
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    
    printf("Starting server...\n");
    int result = uvrpc_server_start(server);
    if (result != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", result);
        return 1;
    }
    
    printf("Server running!\n");
    printf("Press Ctrl+C to stop.\n");
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    printf("\nStopping server...\n");
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("Done.\n");
    return 0;
}
