/**
 * UVRPC Connection Reconnection End-to-End Integration Test
 * Tests client reconnection after server restart
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include "../../include/uvrpc.h"

#define TEST_PORT 15558
#define TEST_HOST "127.0.0.1"
#define TIMEOUT_MS 5000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    server_received++;
    
    printf("Server received request (instance %d)\n", server_received);
    
    /* Send response */
    uint8_t reply_data[] = {'R', 'E', 'C', 'O', 'N', 'N', 'E', 'C', 'T', '!', 'O', 'K'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    client_received++;
    
    printf("Client received response (%d)\n", client_received);
    
    /* Verify response data */
    if (resp->result && resp->result_size == 12) {
        assert(resp->result[0] == 'R');
        assert(resp->result[1] == 'E');
        assert(resp->result[2] == 'C');
        assert(resp->result[3] == 'O');
        assert(resp->result[4] == 'N');
        assert(resp->result[5] == 'N');
        assert(resp->result[6] == 'E');
        assert(resp->result[7] == 'C');
        assert(resp->result[8] == 'T');
        assert(resp->result[9] == '!');
        assert(resp->result[10] == 'O');
        assert(resp->result[11] == 'K');
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
    
    printf("=== UVRPC Connection Reconnection End-to-End Test ===\n");
    
    /* Create server configuration */
    char server_addr[128];
    snprintf(server_addr, sizeof(server_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
    
    uvrpc_config_t* server_config = uvrpc_config_new();
    server_config = uvrpc_config_set_loop(server_config, loop);
    server_config = uvrpc_config_set_address(server_config, server_addr);
    server_config = uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_TCP);
    server_config = uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    assert(server != NULL);
    
    /* Register server handler */
    uvrpc_server_register(server, "reconnect_test_method", server_handler, NULL);
    
    /* Start server */
    int rv = uvrpc_server_start(server);
    assert(rv == 0);
    printf("Server started on %s\n", server_addr);
    
    /* Give server time to start listening */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Create client configuration */
    char client_addr[128];
    snprintf(client_addr, sizeof(client_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
    
    uvrpc_config_t* client_config = uvrpc_config_new();
    client_config = uvrpc_config_set_loop(client_config, loop);
    client_config = uvrpc_config_set_address(client_config, client_addr);
    client_config = uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_TCP);
    client_config = uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);
    
    /* Connect client */
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connecting...\n");
    
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
        printf("ERROR: Initial connection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    printf("Making first RPC call...\n");
    
    /* Client makes first RPC call */
    uint8_t test_data[] = {'T', 'E', 'S', 'T'};
    rv = uvrpc_client_call(client, "reconnect_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    
    /* Run event loop to process first RPC */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && server_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (server_received == 0) {
        printf("ERROR: Server did not receive first request\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    /* Run event loop to receive first response */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && client_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (client_received == 0) {
        printf("ERROR: Client did not receive first response\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    printf("First RPC call successful. Disconnecting client...\n");
    
    /* Disconnect client */
    uvrpc_client_disconnect(client);
    
    /* Stop and free server */
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    
    /* Run event loop to process cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    printf("Server stopped. Restarting server...\n");
    
    /* Recreate and restart server */
    server = uvrpc_server_create(server_config);
    assert(server != NULL);
    uvrpc_server_register(server, "reconnect_test_method", server_handler, NULL);
    rv = uvrpc_server_start(server);
    assert(rv == 0);
    
    /* Give server time to start listening */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    printf("Server restarted. Reconnecting client...\n");
    
    /* Reconnect client */
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    
    /* Run event loop to establish reconnection */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Reconnection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    printf("Making second RPC call after reconnection...\n");
    
    /* Client makes second RPC call after reconnection */
    rv = uvrpc_client_call(client, "reconnect_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    
    /* Run event loop to process second RPC */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && server_received < 2; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (server_received < 2) {
        printf("ERROR: Server did not receive second request\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    /* Run event loop to receive second response */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && client_received < 2; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (client_received < 2) {
        printf("ERROR: Client did not receive second response\n");
    }
    
    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received);
    printf("Client received responses: %d\n", client_received);
    printf("Expected: 2 requests and 2 responses\n");
    
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
    if (server_received == 2 && client_received == 2) {
        printf("\n=== Reconnection End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Reconnection End-to-End Test FAILED ===\n");
        return 1;
    }
}
