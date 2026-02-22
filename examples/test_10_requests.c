#include <stdio.h>
#include <stdlib.h>
#include "../include/uvrpc.h"

static int request_count = 0;

void test_callback(uvrpc_response_t* resp, void* data) {
    (void)resp;
    (void)data;
    request_count++;
    printf("Request #%d received response\n", request_count);
    fflush(stdout);
}

int main(void) {
    printf("=== UVRPC 10 Requests Test ===\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create client */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Set concurrency to 10 */
    uvrpc_config_set_max_concurrent(config, 10);
    uvrpc_config_set_max_pending_callbacks(config, 10);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        printf("Failed to connect\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("Connected!\n");
    fflush(stdout);
    
    /* Run event loop to process connection */
    uv_run(&loop, UV_RUN_ONCE);
    
    /* Send 10 requests */
    printf("Sending 10 requests...\n");
    for (int i = 0; i < 10; i++) {
        int32_t params[2] = {i, i+1};
        int ret = uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), test_callback, NULL);
        printf("Request #%d: ret=%d\n", i, ret);
        fflush(stdout);
    }
    
    printf("Sent all requests. Running event loop...\n");
    fflush(stdout);
    
    /* Run event loop to process responses */
    int iterations = 0;
    while (request_count < 10 && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }
    
    printf("\n=== Results ===\n");
    printf("Requests sent: 10\n");
    printf("Responses received: %d\n", request_count);
    printf("Event loop iterations: %d\n", iterations);
    printf("Result: %s\n", (request_count == 10) ? "PASS" : "FAIL");
    
    /* Cleanup */
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    return (request_count == 10) ? 0 : 1;
}