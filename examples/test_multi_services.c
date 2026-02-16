/**
 * Test multiple services in a single loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "generated/rpc_mathservice_api.h"
#include "generated/rpc_echoservice_api.h"

/* Response callback */
static void on_response(uvrpc_response_t* resp, void* ctx) {
    int* count = (int*)ctx;
    (*count)++;
    
    if (resp->status == UVRPC_OK) {
        if (resp->result && resp->result_size > 0) {
            printf("Response received (count: %d)\n", *count);
        }
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("Testing multiple services...\n");
    
    /* Create MathService client */
    uvrpc_client_t* math_client = uvrpc_mathservice_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);
    if (!math_client) {
        printf("Failed to create MathService client\n");
        return 1;
    }
    
    /* Create EchoService client */
    uvrpc_client_t* echo_client = uvrpc_echoservice_create_client(&loop, "tcp://127.0.0.1:5556", NULL, NULL);
    if (!echo_client) {
        printf("Failed to create EchoService client\n");
        uvrpc_mathservice_free_client(math_client);
        return 1;
    }
    
    printf("Clients created successfully\n");
    
    /* Run loop for a short time */
    uv_run(&loop, UV_RUN_NOWAIT);
    
    /* Cleanup */
    uvrpc_mathservice_free_client(math_client);
    uvrpc_echoservice_free_client(echo_client);
    
    uv_loop_close(&loop);
    
    printf("Test completed\n");
    return 0;
}