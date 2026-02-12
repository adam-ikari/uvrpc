/**
 * UVRPC Simple Server Example
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    printf("Received request: method=%s, msgid=%lu\n", req->method, req->msgid);
    
    /* Echo back the params as result */
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

/* Add handler */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    printf("Received add request: msgid=%lu\n", req->msgid);
    
    /* Parse params (assuming two int32 values) */
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        
        printf("Calculating: %d + %d = %d\n", a, b, result);
        
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        printf("Invalid params size: %zu\n", req->params_size);
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

int run_server_or_client(uv_loop_t* loop, int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    
    printf("UVRPC Simple Server\n");
    printf("Address: %s\n\n", address);
    
        
    /* Create server configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
            return 1;
    }
    
    /* Register handlers */
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_register(server, "add", add_handler, NULL);
    
    /* Start server */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
            return 1;
    }
    
    printf("Server running. Press Ctrl+C to stop.\n\n");
    
    /* Run event loop */
    uv_run(loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    printf("\nStopping server...\n");
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(loop);
    
    printf("Server stopped\n");
    return 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    int result = run_server_or_client(&loop, argc, argv);
    
    uv_loop_close(&loop);
    return result;
}