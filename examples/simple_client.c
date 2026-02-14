/**
 * UVRPC Simple Client Example - Debug Version
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_running = 1;
static volatile int g_connected = 0;

/* Connection callback */
void on_connect(int status, void* ctx) {
    printf("[CLIENT] Connection callback: status=%d\n", status);
    fflush(stdout);
    if (status == 0) {
        g_connected = 1;
        printf("[CLIENT] Connected successfully\n");
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
        g_running = 0;
    }
    fflush(stdout);
    printf("[CLIENT] Connection callback done\n");
    fflush(stdout);
}

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    printf("[RESPONSE] Received response: status=%d, msgid=%lu, result_size=%zu\n", 
           resp->status, resp->msgid, resp->result_size);
    fflush(stdout);
    
    if (resp->status == UVRPC_OK && resp->result_size == 4) {
        int32_t result = *(int32_t*)resp->result;
        printf("[RESPONSE] Result: %d\n", result);
        fflush(stdout);
    }
    
    g_running = 0;  /* Signal to exit */
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    
    printf("[CLIENT] Starting client\n");
    printf("[CLIENT] Connecting to: %s\n", address);
    fflush(stdout);
    
    /* Create loop */
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "[CLIENT] Failed to init loop\n");
        return 1;
    }
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    printf("[CLIENT] Creating client...\n");
    fflush(stdout);
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "[CLIENT] Failed to create client\n");
        uvrpc_config_free(config);
        return 1;
    }
    printf("[CLIENT] Client created\n");
    fflush(stdout);
    
    /* Connect with callback */
    printf("[CLIENT] Connecting...\n");
    fflush(stdout);
    int ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    printf("[CLIENT] Connect returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to initiate connect: %d\n", ret);
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Wait for connection */
    printf("[CLIENT] Waiting for connection...\n");
    fflush(stdout);
    int iterations = 0;
    while (!g_connected && g_running && iterations < 100) {  /* Max 5 seconds */
        int n = uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        if (iterations % 10 == 0) {
            printf("[CLIENT] Waiting for connection... (%d/100), n=%d\n", iterations, n);
            fflush(stdout);
        }
    }
    
    if (!g_connected) {
        fprintf(stderr, "[CLIENT] Connection timeout\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }
    printf("[CLIENT] Connection established\n");
    fflush(stdout);
    
    /* Call RPC */
    int32_t params[2] = {10, 20};
    printf("[CLIENT] Calling 'add' with params %d + %d\n", params[0], params[1]);
    fflush(stdout);
    ret = uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, NULL);
    printf("[CLIENT] Call returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to call: %d\n", ret);
        g_running = 0;
    }
    
    /* Wait for response */
    printf("[CLIENT] Waiting for response...\n");
    fflush(stdout);
    iterations = 0;
    while (g_running && iterations < 50) {  /* Max 2.5 seconds */
        int n = uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        if (iterations % 10 == 0) {
            printf("[CLIENT] Waiting... (%d/50), n=%d\n", iterations, n);
            fflush(stdout);
        }
    }
    printf("[CLIENT] Wait finished\n");
    fflush(stdout);
    
    /* Cleanup */
    printf("[CLIENT] Cleaning up...\n");
    fflush(stdout);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    
    /* Close loop */
    printf("[CLIENT] Closing loop...\n");
    fflush(stdout);
    int close_result = uv_loop_close(&loop);
    printf("[CLIENT] Loop closed: %d\n", close_result);
    fflush(stdout);
    
    printf("[CLIENT] Exiting\n");
    fflush(stdout);
    return 0;
}