/**
 * UVRPC Performance Server
 * Optimized for high-throughput testing
 */

#include "../include/uvrpc.h"
#include "../../generated/rpc_benchmark/rpc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    /* Transport type auto-detected from address prefix */
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* Register handlers using generated stub */
    rpc_register_all(server);
    
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