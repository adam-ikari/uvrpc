/**
 * UVRPC Debug Test - Complete test with request/response
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_server_started = 0;
static int g_client_connected = 0;
static int g_requests_sent = 0;
static int g_responses_received = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    g_server_started = 1;
    printf("[Server] Received request: %s\n", req->method);
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)"Hello from server", 17);
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    g_responses_received++;
    if (resp->error_code == 0) {
        printf("[Client] Response #%d: %.*s\n", g_responses_received,
               (int)resp->result_size, (char*)resp->result);
    } else {
        printf("[Client] Response #%d: error_code=%d\n", g_responses_received, resp->error_code);
    }
}

int main() {
    printf("=== UVRPC Debug Test ===\n\n");
    
    /* Step 1: Create loop */
    printf("Step 1: Creating loop...\n");
    uv_loop_t loop;
    int rc = uv_loop_init(&loop);
    printf("  uv_loop_init: %d\n", rc);
    
    /* Step 2: Create config */
    printf("\nStep 2: Creating config...\n");
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "inproc://debug_test");
    printf("  Config created\n");
    
    /* Step 3: Create server */
    printf("\nStep 3: Creating server...\n");
    uvrpc_server_t* server = uvrpc_server_create(config);
    printf("  Server created: %p\n", (void*)server);
    
    printf("\nStep 4: Register handler...\n");
    int rv = uvrpc_server_register(server, "echo", echo_handler, NULL);
    printf("  Register result: %d\n", rv);
    
    printf("\nStep 5: Start server...\n");
    rv = uvrpc_server_start(server);
    printf("  Start result: %d\n", rv);
    
    /* Step 6: Run loop a bit */
    printf("\nStep 6: Running loop briefly...\n");
    for (int i = 0; i < 3; i++) {
        printf("  Iteration %d: ", i);
        int n = uv_run(&loop, UV_RUN_NOWAIT);
        printf(" uv_run_nowait returned %d\n", n);
    }
    
    /* Step 7: Create client */
    printf("\nStep 7: Creating client...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://debug_test");
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    printf("  Client created: %p\n", (void*)client);
    
    /* Step 8: Connect */
    printf("\nStep 8: Connecting client...\n");
    rv = uvrpc_client_connect(client);
    printf("  Connect result: %d\n", rv);
    
    /* Step 9: Run loop for connection */
    printf("\nStep 9: Running loop for connection...\n");
    for (int i = 0; i < 3; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Step 10: Send requests */
    printf("\nStep 10: Sending 5 requests...\n");
    for (int i = 0; i < 5; i++) {
        const char* msg = "Hello world";
        rv = uvrpc_client_call(client, "echo", (const uint8_t*)msg, strlen(msg),
                               response_callback, NULL);
        printf("  Send request #%d: %d\n", i+1, rv);
        if (rv == 0) g_requests_sent++;
    }
    
    /* Step 11: Run loop to process */
    printf("\nStep 11: Running loop to process requests...\n");
    for (int i = 0; i < 20; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        if (g_responses_received >= 5) break;
    }
    
    printf("\n=== Status ===\n");
    printf("Server started: %d\n", g_server_started);
    printf("Client connected: %d\n", g_client_connected);
    printf("Requests sent: %d\n", g_requests_sent);
    printf("Responses received: %d\n", g_responses_received);
    
    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("\n=== Test Complete ===\n");
    return (g_responses_received == 5) ? 0 : 1;
}
