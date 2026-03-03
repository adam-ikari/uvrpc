/**
 * Boundary Value Tests for UVRPC
 * Tests edge cases, limits, and boundary conditions for RPC operations
 */

#include <uvrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <limits.h>

/* Test statistics */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("[FAIL] %s\n", message); \
            tests_failed++; \
            return; \
        } \
        tests_passed++; \
        printf("[PASS] %s\n", message); \
    } while(0)

/* Test helper: create test context */
typedef struct {
    volatile int completed;
    volatile int received;
    volatile int connected;
    uv_loop_t* loop;
} test_context_t;

/* Connection callback helper */
static void test_connect_callback(int status, void* ctx) {
    test_context_t* context = (test_context_t*)ctx;
    if (status == 0) {
        context->connected = 1;
    }
}

/* Test 1: Empty request (zero length) */
static void test_empty_request(void) {
    printf("\n=== Test 1: Empty Request (Zero Length) ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6001");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that receives empty request */
    void empty_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        printf("[SERVER] Received request with size: %zu\n", req->params_size);
        TEST_ASSERT(req->params_size == 0, "Server should receive empty request");
        
        /* Send response with no data */
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "EmptyTest", empty_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6001");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    int conn_iterations = 0;
    while (!ctx.connected && conn_iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        conn_iterations++;
    }
    
    /* Send empty request */
    void empty_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "Empty request should succeed");
        TEST_ASSERT(resp->result_size == 0, "Empty response should have zero size");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "EmptyTest", NULL, 0, empty_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive response for empty request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 2: Maximum request size */
static void test_max_request_size(void) {
    printf("\n=== Test 2: Maximum Request Size ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6002");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Create maximum size payload (1MB) */
    size_t max_size = 1024 * 1024;  /* 1MB */
    uint8_t* large_data = (uint8_t*)malloc(max_size);
    TEST_ASSERT(large_data != NULL, "Should allocate 1MB buffer");
    
    /* Fill with pattern */
    for (size_t i = 0; i < max_size; i++) {
        large_data[i] = i % 256;
    }
    
    /* Handler for large request */
    void large_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        printf("[SERVER] Received large request: %zu bytes\n", req->params_size);
        TEST_ASSERT(req->params_size == max_size, "Server should receive full size");
        
        /* Echo back */
        uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "LargeTest", large_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6002");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send large request */
    void large_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "Large request should succeed");
        TEST_ASSERT(resp->result_size == max_size, "Should receive full response");
        
        /* Verify data */
        int match = 1;
        for (size_t i = 0; i < resp->result_size && i < 100; i++) {
            if (resp->result[i] != (i % 256)) {
                match = 0;
                break;
            }
        }
        TEST_ASSERT(match, "Data should match");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "LargeTest", large_data, max_size, large_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 200) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive response for large request");
    
    /* Cleanup */
    free(large_data);
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 3: Single byte request */
static void test_single_byte_request(void) {
    printf("\n=== Test 3: Single Byte Request ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6003");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler for single byte request */
    void single_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        printf("[SERVER] Received single byte: %d\n", req->params[0]);
        TEST_ASSERT(req->params_size == 1, "Server should receive single byte");
        TEST_ASSERT(req->params[0] == 42, "Byte value should be 42");
        
        /* Echo back */
        uvrpc_request_send_response(req, UVRPC_OK, req->params, 1);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "SingleTest", single_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6003");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send single byte request */
    uint8_t single_byte = 42;
    void single_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "Single byte request should succeed");
        TEST_ASSERT(resp->result_size == 1, "Should receive single byte");
        TEST_ASSERT(resp->result[0] == 42, "Byte value should be 42");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "SingleTest", &single_byte, 1, single_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive response for single byte request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 4: NULL parameters in callback */
static void test_null_params(void) {
    printf("\n=== Test 4: NULL Parameters ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6004");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that checks for NULL params */
    void null_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        TEST_ASSERT(req->params == NULL, "Params should be NULL");
        TEST_ASSERT(req->params_size == 0, "Params size should be 0");
        
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "NullTest", null_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6004");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send request with NULL params */
    void null_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "NULL params request should succeed");
        TEST_ASSERT(resp->result == NULL, "Result should be NULL");
        TEST_ASSERT(resp->result_size == 0, "Result size should be 0");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "NullTest", NULL, 0, null_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive response for NULL params request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 5: Zero length response */
static void test_zero_length_response(void) {
    printf("\n=== Test 5: Zero Length Response ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6005");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that sends zero length response */
    void zero_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        /* Send response with zero length */
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "ZeroTest", zero_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6005");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send request */
    void zero_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "Zero length response should succeed");
        TEST_ASSERT(resp->result_size == 0, "Result size should be 0");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "ZeroTest", NULL, 0, zero_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive zero length response");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 6: Streaming with single chunk (boundary case) */
static void test_stream_single_chunk(void) {
    printf("\n=== Test 6: Streaming with Single Chunk ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6006");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that sends only one chunk (no ResponseMore) */
    void single_chunk_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        const char* data = "Single chunk data";
        /* Send only final response, no ResponseMore */
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)data, strlen(data) + 1);
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "SingleChunkTest", single_chunk_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6006");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send request */
    void stream_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status == UVRPC_OK, "Single chunk stream should succeed");
        TEST_ASSERT(uvrpc_response_is_stream_end(resp), "Should be stream end");
        TEST_ASSERT(!uvrpc_response_is_stream_more(resp), "Should not be stream more");
        TEST_ASSERT(strcmp((char*)resp->result, "Single chunk data") == 0, "Data should match");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "SingleChunkTest", NULL, 0, stream_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive single chunk stream");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 7: Streaming with many small chunks */
static void test_stream_many_chunks(void) {
    printf("\n=== Test 7: Streaming with Many Small Chunks ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6007");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that sends many small chunks */
    void many_chunks_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        int num_chunks = 100;
        for (int i = 0; i < num_chunks; i++) {
            char chunk[32];
            snprintf(chunk, sizeof(chunk), "Chunk %d", i);
            
            if (i < num_chunks - 1) {
                uvrpc_request_send_response_more(req, (uint8_t*)chunk, strlen(chunk) + 1);
            } else {
                uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)chunk, strlen(chunk) + 1);
            }
        }
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "ManyChunksTest", many_chunks_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6007");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Track chunks received */
    int chunks_received = 0;
    
    /* Send request */
    void many_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        chunks_received++;
        
        if (uvrpc_response_is_stream_more(resp)) {
            TEST_ASSERT(chunks_received < 100, "Should receive less than 100 chunks for ResponseMore");
        } else if (uvrpc_response_is_stream_end(resp)) {
            TEST_ASSERT(chunks_received == 100, "Should receive exactly 100 chunks");
            ctx.received = 1;
        }
    }
    
    uvrpc_client_call(client, "ManyChunksTest", NULL, 0, many_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 500) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive all 100 chunks");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 8: Oneway with NULL callback */
static void test_oneway_null_callback(void) {
    printf("\n=== Test 8: Oneway with NULL Callback ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6008");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that doesn't send response */
    void oneway_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        /* Oneway: don't send response */
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "OnewayTest", oneway_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6008");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send oneway request (NULL callback) */
    const char* data = "Oneway data";
    uvrpc_client_call(client, "OnewayTest", (uint8_t*)data, strlen(data) + 1, NULL, NULL);
    
    /* Run event loop briefly to send the request */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    TEST_ASSERT(ctx.completed, "Oneway handler should complete");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 9: Maximum pending callbacks */
static void test_max_pending_callbacks(void) {
    printf("\n=== Test 9: Maximum Pending Callbacks ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6009");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that responds immediately */
    void max_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        const char* data = "Response";
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)data, strlen(data) + 1);
    }
    
    uvrpc_server_register(server, "MaxTest", max_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client with small pending buffer */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6009");
    uvrpc_config_set_max_pending_callbacks(client_config, 10);  /* Small buffer */
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send 20 requests (exceeds buffer) */
    int total_sent = 0;
    int total_failed = 0;
    
    void max_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        (void)resp;
    }
    
    for (int i = 0; i < 20; i++) {
        int ret = uvrpc_client_call(client, "MaxTest", NULL, 0, max_callback, NULL);
        if (ret == UVRPC_OK) {
            total_sent++;
        } else if (ret == UVRPC_ERROR_CALLBACK_LIMIT) {
            total_failed++;
        }
    }
    
    /* Run event loop to process responses */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
    }
    
    TEST_ASSERT(total_sent == 10, "Should send exactly 10 requests (buffer limit)");
    TEST_ASSERT(total_failed == 10, "Should fail 10 requests (buffer full)");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 10: Error response */
static void test_error_response(void) {
    printf("\n=== Test 10: Error Response ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:6010");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0, 0, 0, &loop};
    
    /* Handler that sends error response */
    void error_handler(uvrpc_request_t* req, void* handler_ctx) {
        (void)handler_ctx;
        /* Send error response */
        uvrpc_response_send_error(req, UVRPC_ERROR_INVALID_PARAM, "Test error message");
        ctx.completed = 1;
    }
    
    uvrpc_server_register(server, "ErrorTest", error_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:6010");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    /* Wait for connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* Send request */
    void error_callback(uvrpc_response_t* resp, void* cb_ctx) {
        (void)cb_ctx;
        TEST_ASSERT(resp->status != UVRPC_OK, "Error response should have non-zero status");
        TEST_ASSERT(resp->error_message != NULL, "Error response should have message");
        TEST_ASSERT(strcmp(resp->error_message, "Test error message") == 0, "Error message should match");
        ctx.received = 1;
    }
    
    uvrpc_client_call(client, "ErrorTest", NULL, 0, error_callback, &ctx);
    
    /* Run event loop */
    int iterations = 0;
    while (!ctx.received && iterations < 50) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    TEST_ASSERT(ctx.received, "Should receive error response");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Main test runner */
int main(int argc, char** argv) {
    printf("=========================================\n");
    printf("UVRPC Boundary Value Tests\n");
    printf("=========================================\n");
    
    (void)argc;
    (void)argv;
    
    /* Run all tests */
    test_empty_request();
    test_max_request_size();
    test_single_byte_request();
    test_null_params();
    test_zero_length_response();
    test_stream_single_chunk();
    test_stream_many_chunks();
    test_oneway_null_callback();
    test_max_pending_callbacks();
    test_error_response();
    
    /* Print summary */
    printf("\n=========================================\n");
    printf("Test Summary\n");
    printf("=========================================\n");
    printf("Total tests: %d\n", tests_passed + tests_failed);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("=========================================\n");
    
    return (tests_failed == 0) ? 0 : 1;
}
