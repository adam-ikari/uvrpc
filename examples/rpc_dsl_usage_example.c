/*
 * UVRPC DSL Example - Using Generated RPC Code
 * 
 * This example demonstrates how to use the auto-generated RPC code
 * with your own implementation.
 */

#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/uvrpc.h"
#include "../../generated/rpc/rpc_api.h"

/* External user implementation */
extern int rpc_handle_request(const char* method_name, const void* request, uvrpc_request_t* req);

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        printf("[Client] Response received, size: %zu\n", resp->result_size);
    } else {
        fprintf(stderr, "[Client] Error: %d\n", resp->status);
    }
}

int main(int argc, char** argv) {
    const char* mode = (argc > 1) ? argv[1] : "server";
    const char* address = (argc > 2) ? argv[2] : "tcp://127.0.0.1:5556";
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    if (strcmp(mode, "server") == 0) {
        /* Server mode */
        printf("=== UVRPC DSL Server ===\n");
        printf("Address: %s\n\n", address);
        
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
        
        uvrpc_server_t* server = uvrpc_server_create(config);
        if (!server) {
            fprintf(stderr, "Failed to create server\n");
            return 1;
        }
        
        /* Register all RPC handlers */
        rpc_register_all(server);
        
        printf("Registered %zu RPC methods\n", g_rpc_method_count);
        printf("Server starting...\n\n");
        
        if (uvrpc_server_start(server) != UVRPC_OK) {
            fprintf(stderr, "Failed to start server\n");
            return 1;
        }
        
        /* Run event loop */
        uv_run(&loop, UV_RUN_DEFAULT);
        
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        
    } else if (strcmp(mode, "client") == 0) {
        /* Client mode */
        printf("=== UVRPC DSL Client ===\n");
        printf("Address: %s\n\n", address);
        
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
        
        uvrpc_client_t* client = uvrpc_client_create(config);
        if (!client) {
            fprintf(stderr, "Failed to create client\n");
            return 1;
        }
        
        printf("Connecting to server...\n");
        if (uvrpc_client_connect(client) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect\n");
            return 1;
        }
        
        /* Wait for connection */
        for (int i = 0; i < 50; i++) {
            uv_run(&loop, UV_RUN_ONCE);
        }
        
        printf("Connected!\n\n");
        
        /* Call RPC methods */
        printf("Calling RPC methods...\n");
        
        /* Example: Add */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_MathAddRequest_start_as_root(&builder);
        rpc_MathAddRequest_a_add(&builder, 10);
        rpc_MathAddRequest_b_add(&builder, 20);
        rpc_MathAddRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        rpc_MathAddRequest_table_t request = rpc_MathAddRequest_as_root(buf);
        MathService_Add(client, request, on_response, NULL);
        
        free(buf);
    flatcc_builder_clear(&builder);
        
        /* Run event loop to process requests */
        for (int i = 0; i < 50; i++) {
            uv_run(&loop, UV_RUN_ONCE);
        }
        
        printf("\nClient shutting down...\n");
        
        uvrpc_client_disconnect(client);
        uvrpc_client_free(client);
        uvrpc_config_free(config);
    }
    
    uv_loop_close(&loop);
    
    return 0;
}