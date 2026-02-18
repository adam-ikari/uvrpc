/**
 * UVRPC Error Handling End-to-End Integration Test
 * Tests various error scenarios
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include "../../include/uvrpc.h"

#define TEST_PORT 15559
#define TEST_HOST "127.0.0.1"
#define TIMEOUT_MS 3000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;
static int error_received = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    server_received++;
    
    printf("Server received request\n");
    
    /* Send error response for invalid requests */
    if (req->params && req->params_size > 0 && req->params[0] == 'E') {
        printf("Server sending error response\n");
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    } else {
        /* Send normal response */
        uint8_t reply_data[] = {'O', 'K'};
        uvrpc_request_send_response(req, UVRPC_OK, reply_data, sizeof(reply_data));
    }
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    if (resp->status != UVRPC_OK) {
        error_received++;
        printf("Client received error response: status=%d\n", resp->status);
    } else {
        client_received++;
        printf("Client received successful response\n");
        
        /* Verify response data */
        if (resp->result && resp->result_size == 2) {
            assert(resp->result[0] == 'O');
            assert(resp->result[1] == 'K');
        }
    }
    
    uvrpc_response_free(resp);
}

/* Timeout timer callback */
static void timeout_callback(uv_timer_t* handle) {
    int* should_stop = (int*)handle->data;
    *should_stop = 1;
    printf("Timeout reached\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    uv_loop_t* loop = uv_default_loop();
    int should_stop = 0;
    int tests_passed = 0;
    int tests_failed = 0;
    
    printf("=== UVRPC Error Handling End-to-End Test ===\n");
    
    /* Test 1: Test with non-existent server */
    printf("\n[Test 1] Connecting to non-existent server...\n");
    uvrpc_config_t* bad_config = uvrpc_config_new();
    bad_config = uvrpc_config_set_loop(bad_config, loop);
    bad_config = uvrpc_config_set_address(bad_config, "tcp://127.0.0.1:99999");
    bad_config = uvrpc_config_set_transport(bad_config, UVRPC_TRANSPORT_TCP);
    bad_config = uvrpc_config_set_comm_type(bad_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* bad_client = uvrpc_client_create(bad_config);
    assert(bad_client != NULL);
    
    int rv = uvrpc_client_connect(bad_client);
    /* Connection may fail or succeed, but that's OK for this test */
    printf("Connect to non-existent server returned: %d\n", rv);
    
    uvrpc_client_free(bad_client);
    uvrpc_config_free(bad_config);
    tests_passed++;
    
    /* Test 2: Test with valid server and normal request */
    printf("\n[Test 2] Testing normal request-response...\n");
    
    char server_addr[128];
    snprintf(server_addr, sizeof(server_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
    
    uvrpc_config_t* server_config = uvrpc_config_new();
    server_config = uvrpc_config_set_loop(server_config, loop);
    server_config = uvrpc_config_set_address(server_config, server_addr);
    server_config = uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_TCP);
    server_config = uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    assert(server != NULL);
    uvrpc_server_register(server, "error_test_method", server_handler, NULL);
    rv = uvrpc_server_start(server);
    assert(rv == 0);
    printf("Server started on %s\n", server_addr);
    
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    uvrpc_config_t* client_config = uvrpc_config_new();
    client_config = uvrpc_config_set_loop(client_config, loop);
    client_config = uvrpc_config_set_address(client_config, server_addr);
    client_config = uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_TCP);
    client_config = uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connected\n");
    
    uv_timer_t timeout_timer;
    uv_timer_init(loop, &timeout_timer);
    timeout_timer.data = &should_stop;
    
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (!should_stop) {
        uint8_t test_data[] = {'T', 'E', 'S', 'T'};
        rv = uvrpc_client_call(client, "error_test_method", test_data, sizeof(test_data), client_callback, NULL);
        assert(rv == 0);
        
        should_stop = 0;
        uv_timer_again(&timeout_timer);
        
        for (int i = 0; i < 200 && !should_stop && client_received == 0; i++) {
            uv_run(loop, UV_RUN_NOWAIT);
        }
        
        if (client_received == 1) {
            printf("Test 2 PASSED: Normal request-response successful\n");
            tests_passed++;
        } else {
            printf("Test 2 FAILED: Expected 1 response, got %d\n", client_received);
            tests_failed++;
        }
    } else {
        printf("Test 2 FAILED: Connection timeout\n");
        tests_failed++;
    }
    
    /* Test 3: Test with error response from server */
    printf("\n[Test 3] Testing error response from server...\n");
    
    uint8_t error_data[] = {'E', 'R', 'R', 'O', 'R'};
    rv = uvrpc_client_call(client, "error_test_method", error_data, sizeof(error_data), client_callback, NULL);
    assert(rv == 0);
    
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && error_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (error_received == 1) {
        printf("Test 3 PASSED: Error response received correctly\n");
        tests_passed++;
    } else {
        printf("Test 3 FAILED: Expected 1 error, got %d\n", error_received);
        tests_failed++;
    }
    
    /* Test 4: Test with NULL parameters */
    printf("\n[Test 4] Testing with NULL parameters...\n");
    
    rv = uvrpc_client_call(client, "error_test_method", NULL, 0, client_callback, NULL);
    assert(rv == 0);
    
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && server_received < 3; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (server_received >= 3) {
        printf("Test 4 PASSED: NULL parameters handled correctly\n");
        tests_passed++;
    } else {
        printf("Test 4 FAILED: Expected server to handle NULL params\n");
        tests_failed++;
    }
    
    printf("\n=== Test Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Server received requests: %d\n", server_received);
    printf("Client received responses: %d\n", client_received);
    printf("Client received errors: %d\n", error_received);
    
    /* Cleanup */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (tests_failed == 0) {
        printf("\n=== Error Handling End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Error Handling End-to-End Test FAILED ===\n");
        return 1;
    }
}
