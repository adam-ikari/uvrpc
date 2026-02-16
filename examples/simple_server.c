/**
 * UVRPC Simple Server Example - Debug Version
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[HANDLER] Received request: method=%s, msgid=%lu\n", req->method, req->msgid);
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

/* Add handler */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    fprintf(stderr, "[HANDLER] Received add request: msgid=%lu\n", req->msgid);
    fflush(stderr);
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        fprintf(stderr, "[HANDLER] Calculating: %d + %d = %d\n", a, b, result);
        fflush(stderr);
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        fprintf(stderr, "[HANDLER] Invalid params size: %zu\n", req->params_size);
        fflush(stderr);
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

uvrpc_server_t* create_server(uv_loop_t* loop, const char* address) {
    printf("[INIT] Creating server configuration...\n");
    fflush(stdout);
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    /* Transport type auto-detected from address prefix */
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    printf("[INIT] Creating server...\n");
    fflush(stdout);
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "[INIT] Failed to create server\n");
        uvrpc_config_free(config);
        return NULL;
    }
    printf("[INIT] Server created successfully\n");
    fflush(stdout);
    
    printf("[INIT] Registering echo handler...\n");
    fflush(stdout);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    printf("[INIT] Registering add handler...\n");
    fflush(stdout);
    uvrpc_server_register(server, "add", add_handler, NULL);
    
    printf("[INIT] Starting server...\n");
    fflush(stdout);
    int ret = uvrpc_server_start(server);
    printf("[INIT] uvrpc_server_start returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != UVRPC_OK) {
        fprintf(stderr, "[INIT] Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        return NULL;
    }
    printf("[INIT] Server started successfully\n");
    fflush(stdout);
    
    return server;
}

void destroy_server(uvrpc_server_t* server) {
    if (server) {
        printf("[CLEAN] Stopping server...\n");
        fflush(stdout);
        uvrpc_server_free(server);
        printf("[EXIT] Server stopped\n");
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    
    fprintf(stderr, "[MAIN] Starting main function\n");
    fflush(stderr);
    
    printf("[INIT] UVRPC Simple Server\n");
    printf("[INIT] Address: %s\n\n", address);
    fflush(stdout);
    
    printf("[MAIN] Initializing loop...\n");
    fflush(stdout);
    uv_loop_t loop;
    int loop_result = uv_loop_init(&loop);
    printf("[MAIN] uv_loop_init returned: %d\n", loop_result);
    fflush(stdout);
    
    /* Create and start server (server does NOT run the loop) */
    uvrpc_server_t* server = create_server(&loop, address);
    if (!server) {
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Run event loop externally */
    printf("[LOOP] Running event loop (server is driven by external loop)...\n");
    fflush(stdout);
    uv_run(&loop, UV_RUN_DEFAULT);
    printf("[LOOP] Event loop exited\n");
    fflush(stdout);
    
    /* Cleanup */
    destroy_server(server);
    uv_loop_close(&loop);
    
    return 0;
}