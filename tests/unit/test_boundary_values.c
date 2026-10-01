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

#include "../free_port.h"
#include <unistd.h>
#include <limits.h>

static int tests_passed = 0;
static int tests_failed = 0;

/* Aborts the enclosing test or callback on the first failure. Every function
 * that uses it returns void, so a bare `return` is right. */
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


typedef struct {
    volatile int completed;
    volatile int received;
    volatile int connected;
    /* Per-test scalars the callbacks used to reach through the enclosing
     * stack frame; a file-scope function has no such frame to reach. */
    size_t max_size;
    volatile int chunks_received;
    uv_loop_t* loop;
} test_context_t;

/* These were defined inside the test bodies. That is a GNU extension, and
 * the trampoline it needs lands on the stack, so the object file gets an
 * executable stack and the linker warns about it. File scope is free. */

void empty_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    printf("[SERVER] Received request with size: %zu\n", req->params_size);
    TEST_ASSERT(req->params_size == 0, "Server should receive empty request");
    
    /* Send response with no data */
    uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    c->completed = 1;
}
void empty_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "Empty request should succeed");
    TEST_ASSERT(resp->result_size == 0, "Empty response should have zero size");
    c->received = 1;
    uvrpc_response_free(resp);
}
void large_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    printf("[SERVER] Received large request: %zu bytes\n", req->params_size);
    TEST_ASSERT(req->params_size == c->max_size, "Server should receive full size");
    
    /* Echo back */
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    c->completed = 1;
}
void large_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "Large request should succeed");
    TEST_ASSERT(resp->result_size == c->max_size, "Should receive full response");
    
    /* Verify data */
    int match = 1;
    for (size_t i = 0; i < resp->result_size && i < 100; i++) {
        if (resp->result[i] != (i % 256)) {
            match = 0;
            break;
        }
    }
    TEST_ASSERT(match, "Data should match");
    c->received = 1;
    uvrpc_response_free(resp);
}
void single_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    printf("[SERVER] Received single byte: %d\n", req->params[0]);
    TEST_ASSERT(req->params_size == 1, "Server should receive single byte");
    TEST_ASSERT(req->params[0] == 42, "Byte value should be 42");
    
    /* Echo back */
    uvrpc_request_send_response(req, UVRPC_OK, req->params, 1);
    c->completed = 1;
}
void single_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "Single byte request should succeed");
    TEST_ASSERT(resp->result_size == 1, "Should receive single byte");
    TEST_ASSERT(resp->result[0] == 42, "Byte value should be 42");
    c->received = 1;
    uvrpc_response_free(resp);
}
void null_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    TEST_ASSERT(req->params == NULL, "Params should be NULL");
    TEST_ASSERT(req->params_size == 0, "Params size should be 0");
    
    uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    c->completed = 1;
}
void null_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "NULL params request should succeed");
    TEST_ASSERT(resp->result == NULL, "Result should be NULL");
    TEST_ASSERT(resp->result_size == 0, "Result size should be 0");
    c->received = 1;
    uvrpc_response_free(resp);
}
void zero_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    /* Send response with zero length */
    uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    c->completed = 1;
}
void zero_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "Zero length response should succeed");
    TEST_ASSERT(resp->result_size == 0, "Result size should be 0");
    c->received = 1;
    uvrpc_response_free(resp);
}
void single_chunk_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    const char* data = "Single chunk data";
    /* Send only final response, no ResponseMore */
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)data, strlen(data) + 1);
    c->completed = 1;
}
void stream_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status == UVRPC_OK, "Single chunk stream should succeed");
    TEST_ASSERT(uvrpc_response_is_stream_end(resp), "Should be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(resp), "Should not be stream more");
    TEST_ASSERT(strcmp((char*)resp->result, "Single chunk data") == 0, "Data should match");
    c->received = 1;
    uvrpc_response_free(resp);
}
void many_chunks_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
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
    c->completed = 1;
}
void many_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    c->chunks_received++;
    
    if (uvrpc_response_is_stream_more(resp)) {
        TEST_ASSERT(c->chunks_received < 100, "Should receive less than 100 chunks for ResponseMore");
    } else if (uvrpc_response_is_stream_end(resp)) {
        TEST_ASSERT(c->chunks_received == 100, "Should receive exactly 100 chunks");
    c->received = 1;
    }
    uvrpc_response_free(resp);
}
void oneway_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    /* Oneway: don't send response */
    c->completed = 1;
}
void max_handler(uvrpc_request_t* req, void* handler_ctx) {
    (void)handler_ctx;
    const char* data = "Response";
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)data, strlen(data) + 1);
}
void max_callback(uvrpc_response_t* resp, void* cb_ctx) {
    (void)cb_ctx;
    (void)resp;
    uvrpc_response_free(resp);
}
void error_handler(uvrpc_request_t* req, void* handler_ctx) {
    test_context_t* c = (test_context_t*)handler_ctx;
    (void)handler_ctx;
    /* Send error response */
    uvrpc_response_send_error(req, UVRPC_ERROR_INVALID_PARAM, "Test error message");
    c->completed = 1;
}
void error_callback(uvrpc_response_t* resp, void* cb_ctx) {
    test_context_t* c = (test_context_t*)cb_ctx;
    (void)cb_ctx;
    TEST_ASSERT(resp->status != UVRPC_OK, "Error response should have non-zero status");
    TEST_ASSERT(resp->error_message != NULL, "Error response should have message");
    TEST_ASSERT(strcmp(resp->error_message, "Test error message") == 0, "Error message should match");
    c->received = 1;
    uvrpc_response_free(resp);
}


/* Test statistics */

/* Wait until a flag the callbacks set, or the budget runs out.
 *
 * The obvious loop does not work. UV_RUN_DEFAULT never returns while the
 * server's listening socket keeps the loop alive, so the counter never
 * advances. Replacing it with UV_RUN_NOWAIT spins through the whole budget in
 * microseconds and gives up long before a TCP round trip finishes. Waiting
 * between passes is what makes the deadline mean anything. */
static int wait_for(uv_loop_t* loop, volatile int* flag, int timeout_ms) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed++) {
        if (*flag) return 1;
        uv_run(loop, UV_RUN_NOWAIT);
        usleep(1000);
    }
    return *flag;
}

/* Test helper: create test context */

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

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that receives empty request */
    
    uvrpc_server_register(server, "EmptyTest", empty_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send empty request */
    
    uvrpc_client_call(client, "EmptyTest", NULL, 0, empty_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive response for empty request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 2: Maximum request size */
static void test_max_request_size(void) {
    printf("\n=== Test 2: Maximum Request Size ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* The largest payload the socket transports actually carry.
     *
     * This was 1MB, which no transport can deliver: the frame limit is 64KB
     * (UVBUS_DEFAULT_MAX_FRAME_SIZE) and the encoded frame has to fit inside
     * it, so a 1MB request is dropped whole and the caller waits out its own
     * timeout with no error pointing at it. Measured per transport -- TCP
     * 65488, IPC 65484, UDP 65452 -- so TCP is the number worth using here.
     * The three differ only by their own encoding overhead at the limit. */
    /* 65480, not the 65488 usually quoted for TCP. The method name is part of
     * the frame, so a longer name eats into the payload budget: with the
     * one-letter name used in the measurement 65488 fits, and with this test's
     * "LargeTest" the ceiling drops by exactly the extra bytes. Measured, not
     * derived -- there is no port-0 API to ask the server what it bound. */
    size_t max_size = 65480;
    ctx.max_size = max_size;   /* the handler asserts against this */

    uint8_t* large_data = (uint8_t*)malloc(max_size);
    TEST_ASSERT(large_data != NULL, "Should allocate the maximum payload");
    
    /* Fill with pattern */
    for (size_t i = 0; i < max_size; i++) {
        large_data[i] = i % 256;
    }
    
    /* Handler for large request */
    
    uvrpc_server_register(server, "LargeTest", large_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send large request */
    
    uvrpc_client_call(client, "LargeTest", large_data, max_size, large_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive response for large request");
    
    /* Cleanup */
    free(large_data);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 3: Single byte request */
static void test_single_byte_request(void) {
    printf("\n=== Test 3: Single Byte Request ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler for single byte request */
    
    uvrpc_server_register(server, "SingleTest", single_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send single byte request */
    uint8_t single_byte = 42;
    
    uvrpc_client_call(client, "SingleTest", &single_byte, 1, single_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive response for single byte request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 4: NULL parameters in callback */
static void test_null_params(void) {
    printf("\n=== Test 4: NULL Parameters ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that checks for NULL params */
    
    uvrpc_server_register(server, "NullTest", null_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send request with NULL params */
    
    uvrpc_client_call(client, "NullTest", NULL, 0, null_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive response for NULL params request");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 5: Zero length response */
static void test_zero_length_response(void) {
    printf("\n=== Test 5: Zero Length Response ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that sends zero length response */
    
    uvrpc_server_register(server, "ZeroTest", zero_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send request */
    
    uvrpc_client_call(client, "ZeroTest", NULL, 0, zero_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive zero length response");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 6: Streaming with single chunk (boundary case) */
static void test_stream_single_chunk(void) {
    printf("\n=== Test 6: Streaming with Single Chunk ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that sends only one chunk (no ResponseMore) */
    
    uvrpc_server_register(server, "SingleChunkTest", single_chunk_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send request */
    
    uvrpc_client_call(client, "SingleChunkTest", NULL, 0, stream_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive single chunk stream");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 7: Streaming with many small chunks */
static void test_stream_many_chunks(void) {
    printf("\n=== Test 7: Streaming with Many Small Chunks ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that sends many small chunks */
    
    uvrpc_server_register(server, "ManyChunksTest", many_chunks_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Track chunks received */
    ctx.chunks_received = 0;
    
    /* Send request */
    
    uvrpc_client_call(client, "ManyChunksTest", NULL, 0, many_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive all 100 chunks");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 8: Oneway with NULL callback */
static void test_oneway_null_callback(void) {
    printf("\n=== Test 8: Oneway with NULL Callback ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that doesn't send response */
    
    uvrpc_server_register(server, "OnewayTest", oneway_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send oneway request (NULL callback) */
    const char* data = "Oneway data";
    uvrpc_client_call(client, "OnewayTest", (uint8_t*)data, strlen(data) + 1, NULL, NULL);
    
    /* Wait for the flag the handler sets. Waiting on ctx.connected -- already
     * true by now -- returned instantly and never gave the loop a chance to
     * deliver the request, so this test could not have passed. */
    wait_for(&loop, &ctx.completed, 3000);
    
    TEST_ASSERT(ctx.completed, "Oneway handler should complete");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 9: Maximum pending callbacks */
static void test_max_pending_callbacks(void) {
    printf("\n=== Test 9: Maximum Pending Callbacks ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that responds immediately */
    
    uvrpc_server_register(server, "MaxTest", max_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client with small pending buffer */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    /* 64, not 10. Values below 64 -- and anything that is not a power of two
     * -- are silently replaced with the default, so asking for 10 left the
     * ring at its default size and every request below fit. The limit was
     * never under test. */
    uvrpc_config_set_max_pending_callbacks(client_config, 64);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* More requests than the ring holds, sent without pumping the loop in
     * between: no response can come back, so no slot is released, and the
     * first 64 fill the ring for good. */
    int total_sent = 0;
    int total_failed = 0;
    
    for (int i = 0; i < 100; i++) {
        int ret = uvrpc_client_call(client, "MaxTest", NULL, 0, max_callback, NULL);
        if (ret == UVRPC_OK) {
            total_sent++;
        } else if (ret == UVRPC_ERROR_CALLBACK_LIMIT) {
            total_failed++;
        }
    }
    
    /* Let the responses land. A deadline, not an iteration count. */
    for (int elapsed = 0; elapsed < 1000 && total_sent == 64; elapsed++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(1000);
    }
    
    TEST_ASSERT(total_sent == 64, "Should send exactly 64 requests (ring buffer size)");
    TEST_ASSERT(total_failed == 36, "Should refuse the rest once the ring is full");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
}

/* Test 10: Error response */
static void test_error_response(void) {
    printf("\n=== Test 10: Error Response ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);

    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", acceptance_free_port());
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    test_context_t ctx = {0};  /* designated below */
    ctx.loop = &loop;
    
    /* Handler that sends error response */
    
    uvrpc_server_register(server, "ErrorTest", error_handler, &ctx);
    uvrpc_server_start(server);
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, test_connect_callback, &ctx);
    
    wait_for(&loop, &ctx.connected, 3000);
    
    /* Send request */
    
    uvrpc_client_call(client, "ErrorTest", NULL, 0, error_callback, &ctx);
    
    /* Run event loop */
    wait_for(&loop, &ctx.received, 3000);
    
    TEST_ASSERT(ctx.received, "Should receive error response");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* free() only drops a reference, so pump the loop for the teardown to
     * actually finish -- otherwise this server is still listening when the
     * next test starts. */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
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

