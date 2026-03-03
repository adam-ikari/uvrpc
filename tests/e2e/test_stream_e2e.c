/**
 * End-to-End Test for Stream Response
 * Tests complete streaming RPC flow from client to server
 */

#include <uvrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#define NUM_CHUNKS 5
#define NUM_REQUESTS 10

static int g_total_chunks = 0;
static int g_end_markers = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("[FAIL] %s\n", message); \
            g_tests_failed++; \
            return; \
        } \
        g_tests_passed++; \
        printf("[PASS] %s\n", message); \
    } while(0)

/* Server handler - sends multiple chunks */
static void stream_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    printf("[SERVER] Received stream request, msgid=%u\n", req->msgid);
    
    /* Check if params contains "single" for single response mode */
    int is_single = (req->params && strstr((char*)req->params, "single") != NULL);
    
    if (is_single) {
        /* Single response mode */
        char response[64];
        snprintf(response, sizeof(response), "Single response");
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)response, strlen(response) + 1);
        printf("[SERVER] Sent single response for msgid=%u\n", req->msgid);
    } else {
        /* Stream mode - send NUM_CHUNKS-1 intermediate chunks */
        for (int i = 0; i < NUM_CHUNKS - 1; i++) {
            char chunk[64];
            snprintf(chunk, sizeof(chunk), "Chunk %d", i + 1);
            
            uvrpc_request_send_response_more(req, (uint8_t*)chunk, strlen(chunk) + 1);
        }
        
        /* Send final chunk */
        char final_chunk[64];
        snprintf(final_chunk, sizeof(final_chunk), "Chunk %d (FINAL)", NUM_CHUNKS);
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)final_chunk, strlen(final_chunk) + 1);
        
        printf("[SERVER] Stream complete for msgid=%u\n", req->msgid);
    }
}

/* Client callback - receives chunks */
static void stream_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    if (uvrpc_response_is_stream_more(resp)) {
        g_total_chunks++;
        printf("[CLIENT] Received chunk %d (more): %s\n", 
               g_total_chunks, 
               resp->result ? (char*)resp->result : "(empty)");
    } else if (uvrpc_response_is_stream_end(resp)) {
        g_total_chunks++;
        g_end_markers++;
        printf("[CLIENT] Received chunk %d (last): %s\n", 
               g_total_chunks, 
               resp->result ? (char*)resp->result : "(empty)");
        printf("[CLIENT] End markers: %d/%d\n", g_end_markers, NUM_REQUESTS);
    }
}

/* Test 1: Basic stream flow */
static void test_basic_stream_flow(uvrpc_client_t* client, uvrpc_server_t* server) {
    printf("\n=== Test 1: Basic Stream Flow ===\n");
    
    g_total_chunks = 0;
    g_end_markers = 0;
    
    /* Send 5 stream requests */
    for (int i = 0; i < 5; i++) {
        const char* params = "test";
        uvrpc_client_call(client, "stream", (uint8_t*)params, strlen(params) + 1,
                         stream_callback, NULL);
    }
    
    /* Run event loop with timeout */
    int timeout_ms = 3000;  // 3 seconds
    int64_t start = uv_now(uv_default_loop());
    while (g_end_markers < 5 && (uv_now(uv_default_loop()) - start) < timeout_ms) {
        uv_run(uv_default_loop(), UV_RUN_DEFAULT);
        usleep(10000);  // 10ms
    }
    
    TEST_ASSERT(g_end_markers == 5, "Should receive all 5 end markers");
    TEST_ASSERT(g_total_chunks == 25, "Should receive 25 total chunks (5 requests * 5 chunks)");
}

/* Test 2: Multiple concurrent streams */
static void test_concurrent_streams(uvrpc_client_t* client, uvrpc_server_t* server) {
    printf("\n=== Test 2: Concurrent Streams ===\n");
    
    g_total_chunks = 0;
    g_end_markers = 0;
    
    /* Send 10 concurrent stream requests */
    for (int i = 0; i < NUM_REQUESTS; i++) {
        const char* params = "concurrent";
        uvrpc_client_call(client, "stream", (uint8_t*)params, strlen(params) + 1,
                         stream_callback, NULL);
    }
    
    /* Run event loop with timeout */
    int timeout_ms = 5000;  // 5 seconds
    int64_t start = uv_now(uv_default_loop());
    while (g_end_markers < NUM_REQUESTS && (uv_now(uv_default_loop()) - start) < timeout_ms) {
        uv_run(uv_default_loop(), UV_RUN_DEFAULT);
        usleep(10000);  // 10ms
    }
    
    TEST_ASSERT(g_end_markers == NUM_REQUESTS, 
                "Should receive all end markers for concurrent streams");
    TEST_ASSERT(g_total_chunks == NUM_REQUESTS * NUM_CHUNKS,
                "Should receive correct total chunks for concurrent streams");
}

/* Test 3: Single response (no stream) */
static int g_single_response_received = 0;
static void single_response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    g_single_response_received++;
    
    if (uvrpc_response_is_stream_end(resp)) {
        printf("[CLIENT] Single response received\n");
    }
}

static void test_single_response(uvrpc_client_t* client, uvrpc_server_t* server) {
    printf("\n=== Test 3: Single Response (No Stream) ===\n");
    
    g_single_response_received = 0;
    
    /* Send a request that returns a single response */
    const char* params = "single";
    uvrpc_client_call(client, "stream", (uint8_t*)params, strlen(params) + 1,
                     single_response_callback, NULL);
    
    /* Run event loop with timeout */
    int timeout_ms = 2000;  // 2 seconds
    int64_t start = uv_now(uv_default_loop());
    while (g_single_response_received == 0 && (uv_now(uv_default_loop()) - start) < timeout_ms) {
        uv_run(uv_default_loop(), UV_RUN_DEFAULT);
        usleep(10000);  // 10ms
    }
    
    TEST_ASSERT(g_single_response_received == 1, 
                "Should receive single response with end marker");
}

int main(void) {
    printf("========================================\n");
    printf("UVRPC Stream Response E2E Tests\n");
    printf("========================================\n");
    
    /* Initialize libuv loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://test_e2e");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }
    
    uvrpc_server_register(server, "stream", stream_handler, NULL);
    uvrpc_server_start(server);
    printf("[SETUP] Server started\n");
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://test_e2e");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        return 1;
    }
    
    uvrpc_client_connect(client);
    printf("[SETUP] Client connected\n\n");
    
    /* Run tests */
    test_basic_stream_flow(client, server);
    test_concurrent_streams(client, server);
    test_single_response(client, server);
    
    /* Cleanup */
    printf("\n[CLEANUP] Shutting down...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
    
    printf("\n========================================\n");
    printf("E2E Test Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
}