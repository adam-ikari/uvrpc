/**
 * Test program to verify retry logic in generated code
 * Demonstrates usage of rpc_set_max_retries() and automatic retry
 */

#include "rpc_benchmark_builder.h"
#include "rpc_benchmark_reader.h"
#include "rpc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

/* Connection callback */
static int g_connected = 0;
static void on_connect(int status, void* ctx) {
    if (status == 0) {
        printf("Connected to server successfully!\n");
        g_connected = 1;
    } else {
        printf("Connection failed: %d\n", status);
    }
}

/* Response callback */
static int g_response_count = 0;
static void on_response(const char* method_name, const void* response, 
                       size_t size, int status, void* ctx) {
    g_response_count++;
    
    if (status == 0) {
        printf("Response #%d received for %s (success)\n", g_response_count, method_name);
        
        /* Parse Add response */
        if (strcmp(method_name, "Add") == 0) {
            rpc_BenchmarkAddResponse_table_t resp = rpc_BenchmarkAddResponse_as_root(response);
            int64_t result = rpc_BenchmarkAddResponse_result(resp);
            printf("  Result: %ld\n", (long)result);
        }
    } else {
        printf("Response #%d received for %s (failed: %d)\n", g_response_count, method_name, status);
    }
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uvrpc_client_t* client;
    
    /* Parse command line */
    const char* address = "tcp://127.0.0.1:5555";
    int num_requests = 5;
    int max_retries = 3;
    
    if (argc >= 2) address = argv[1];
    if (argc >= 3) num_requests = atoi(argv[2]);
    if (argc >= 4) max_retries = atoi(argv[3]);
    
    printf("=== Retry Logic Test ===\n");
    printf("Server: %s\n", address);
    printf("Requests: %d\n", num_requests);
    printf("Max retries: %d\n", max_retries);
    printf("========================\n\n");
    
    /* Initialize event loop */
    uv_loop_init(&loop);
    
    /* Set retry configuration */
    printf("Setting max retries to %d...\n", max_retries);
    rpc_set_max_retries(max_retries);
    printf("Current max retries: %d\n", rpc_get_max_retries());
    
    /* Create and connect client */
    printf("Creating client...\n");
    client = rpc_client_create(&loop, address, on_connect, NULL);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Run event loop to establish connection */
    printf("Waiting for connection...\n");
    int loop_count = 0;
    while (!g_connected && loop_count < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        loop_count++;
    }
    
    if (!g_connected) {
        fprintf(stderr, "Connection timeout\n");
        rpc_client_free(client);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("\nSending %d Add requests with retry logic...\n", num_requests);
    
    /* Send requests */
    for (int i = 0; i < num_requests; i++) {
        /* Build Add request */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_BenchmarkAddRequest_start_as_root(&builder);
        rpc_BenchmarkAddRequest_a_add(&builder, i);
        rpc_BenchmarkAddRequest_b_add(&builder, i + 1);
        rpc_BenchmarkAddRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        /* Send request - will automatically retry on failure */
        int ret = BenchmarkService_Add(client, 
                                       rpc_BenchmarkAddRequest_as_root(buf),
                                       on_response, NULL);
        
        if (ret == 0) {
            printf("Sent request #%d\n", i + 1);
        } else {
            printf("Failed to send request #%d (error: %d) - will retry...\n", i + 1, ret);
        }
        
        free(buf);
        flatcc_builder_reset(&builder);
        
        /* Run event loop between requests */
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    printf("\nWaiting for responses...\n");
    
    /* Wait for all responses */
    loop_count = 0;
    while (g_response_count < num_requests && loop_count < 1000) {
        uv_run(&loop, UV_RUN_ONCE);
        loop_count++;
    }
    
    printf("\n=== Test Results ===\n");
    printf("Expected responses: %d\n", num_requests);
    printf("Received responses: %d\n", g_response_count);
    printf("Success rate: %.1f%%\n", 100.0 * g_response_count / num_requests);
    printf("====================\n");
    
    /* Cleanup */
    rpc_client_free(client);
    uv_loop_close(&loop);
    
    return (g_response_count == num_requests) ? 0 : 1;
}
