/**
 * Example: Using auto-generated client API
 * 
 * This example shows how to use the generated rpc_client_create() function
 * to create and connect a client. The connection process runs asynchronously
 * in the event loop.
 */

#include "../include/uvrpc.h"
#include "../generated/rpc_benchmark/rpc_benchmark_builder.h"
#include "../generated/rpc_benchmark/rpc_benchmark_reader.h"
#include "../generated/rpc_benchmark/rpc_api.h"
#include <stdio.h>
#include <stdlib.h>

/* Connection callback */
void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        printf("Client connected successfully!\n");
    } else {
        printf("Connection failed with status: %d\n", status);
    }
}

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp && resp->data) {
        rpc_BenchmarkAddResponse_table_t result = 
            rpc_BenchmarkAddResponse_as_root(resp->data);
        int32_t sum = rpc_BenchmarkAddResponse_result(result);
        printf("Add result: %d\n", sum);
    }
}

int main() {
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create and connect client (async) */
    printf("Creating client and initiating connection...\n");
    uvrpc_client_t* client = rpc_client_create(&loop, "tcp://127.0.0.1:5555", on_connect, NULL);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Wait a bit for connection to establish */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    /* Prepare request */
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    rpc_BenchmarkAddRequest_start_as_root(&builder);
    rpc_BenchmarkAddRequest_a_add(&builder, 10);
    rpc_BenchmarkAddRequest_b_add(&builder, 20);
    rpc_BenchmarkAddRequest_end_as_root(&builder);
    
    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    
    /* Call RPC method */
    printf("Calling Add(10, 20)...\n");
    BenchmarkService_Add(client, rpc_BenchmarkAddRequest_as_root(buf), on_response, NULL);
    
    free(buf);
    flatcc_builder_clear(&builder);
    
    /* Run event loop to handle response */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    /* Cleanup */
    rpc_client_free(client);
    uv_loop_close(&loop);
    
    printf("Example completed\n");
    return 0;
}
