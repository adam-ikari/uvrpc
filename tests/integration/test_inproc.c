/**
 * UVRPC INPROC End-to-End Integration Test
 * Tests in-process transport (same process)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include "../../include/uvrpc.h"

#define TEST_INPROC_ADDR "inproc://uvrpc_test"
#define TIMEOUT_MS 5000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    server_received++;
    
    printf("Server received INPROC request\n");
    
    /* Verify request data */
    if (req->params && req->params_size == 6) {
        assert(req->params[0] == 'I');
        assert(req->params[1] == 'N');
        assert(req->params[2] == 'P');
        assert(req->params[3] == 'R');
        assert(req->params[4] == 'O');
        assert(req->params[5] == 'C');
    }
    
    /* Send response */
    uint8_t reply_data[] = {'I', 'N', 'P', 'R', 'O', 'C', '_', 'O', 'K'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    client_received++;
    
    printf("Client received INPROC response\n");
    
    /* Verify response data */
    if (resp->result && resp->result_size == 9) {
        assert(resp->result[0] == 'I');
        assert(resp->result[1] == 'N');
        assert(resp->result[2] == 'P');
        assert(resp->result[3] == 'R');
        assert(resp->result[4] == 'O');
        assert(resp->result[5] == 'C');
        assert(resp->result[6] == '_');
        assert(resp->result[7] == 'O');
        assert(resp->result[8] == 'K');
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
    
    printf("=== UVRPC INPROC End-to-End Test ===\n");
    
    /* Create server configuration */
    uvrpc_config_t* server_config = uvrpc_config_new();
    server_config = uvrpc_config_set_loop(server_config, loop);
    server_config = uvrpc_config_set_address(server_config, TEST_INPROC_ADDR);
    server_config = uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    server_config = uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    assert(server != NULL);
    
    /* Register server handler */
    uvrpc_server_register(server, "inproc_test_method", server_handler, NULL);
    
    /* Start server */
    int rv = uvrpc_server_start(server);
    assert(rv == 0);
    printf("Server started on %s\n", TEST_INPROC_ADDR);
    
    /* Give server time to start listening */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Create client configuration */
    uvrpc_config_t* client_config = uvrpc_config_new();
    client_config = uvrpc_config_set_loop(client_config, loop);
    client_config = uvrpc_config_set_address(client_config, TEST_INPROC_ADDR);
    client_config = uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    client_config = uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);
    
    /* Connect client */
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connecting via INPROC...\n");
    
    /* Setup timeout timer */
    uv_timer_t timeout_timer;
    uv_timer_init(loop, &timeout_timer);
    timeout_timer.data = &should_stop;
    
    /* Run event loop to establish connection */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Connection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    printf("Making INPROC RPC call...\n");
    
    /* Client makes RPC call */
    uint8_t test_data[] = {'I', 'N', 'P', 'R', 'O', 'C'};
    rv = uvrpc_client_call(client, "inproc_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    printf("Client called inproc_test_method\n");
    
    /* Run event loop to process RPC */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && server_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (server_received == 0) {
        printf("ERROR: Server did not receive request\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    /* Run event loop to receive response */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && client_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (client_received == 0) {
        printf("ERROR: Client did not receive response\n");
    }
    
    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received);
    printf("Client received responses: %d\n", client_received);
    
    /* Cleanup */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    
    /* Run event loop to process cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    /* Verify test results */
    if (server_received == 1 && client_received == 1) {
        printf("\n=== INPROC End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== INPROC End-to-End Test FAILED ===\n");
        return 1;
    }
}