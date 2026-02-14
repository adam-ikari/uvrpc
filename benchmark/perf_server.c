/**
 * UVRPC Performance Server
 * Optimized for high-throughput testing
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* Register handler */
    uvrpc_server_register(server, "add", add_handler, NULL);
    
    /* Start server */
    uvrpc_server_start(server);
    
    printf("Server started on %s\n", address);
    fflush(stdout);
    
    /* Run event loop */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}