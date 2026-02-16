/**
 * UVRPC INPROC Transport Test
 * Demonstrates in-process communication between server and client
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Add handler */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[HANDLER] Add handler called\n");
    fflush(stdout);
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        printf("[HANDLER] Calculating: %d + %d = %d\n", a, b, result);
        fflush(stdout);
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
        printf("[HANDLER] Response sent\n");
        fflush(stdout);
    } else {
        fprintf(stderr, "[HANDLER] Invalid params size: %zu\n", req->params_size);
        fflush(stderr);
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

int main() {
    printf("=== UVRPC INPROC Transport Test ===\n");
    fflush(stdout);
    
    /* Create event loop */
    uv_loop_t loop;
    int ret = uv_loop_init(&loop);
    if (ret != 0) {
        fprintf(stderr, "Failed to init loop: %s\n", uv_strerror(ret));
        return 1;
    }
    printf("Loop initialized\n");
    fflush(stdout);
    
    /* Create server config */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://test_endpoint");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }
    printf("Server created\n");
    fflush(stdout);
    
    /* Register handler */
    uvrpc_server_register(server, "Add", add_handler, NULL);
    printf("Handler registered\n");
    fflush(stdout);
    
    /* Start server */
    ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        return 1;
    }
    printf("Server started\n");
    fflush(stdout);
    
    /* Create client config */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://test_endpoint");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }
    printf("Client created\n");
    fflush(stdout);
    
    /* Connect client */
    ret = uvrpc_client_connect(client);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to connect client: %d\n", ret);
        uvrpc_client_free(client);
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }
    printf("Client connected\n");
    fflush(stdout);
    
    /* Run event loop briefly to establish connection */
    uv_run(&loop, UV_RUN_NOWAIT);
    
    /* Send request */
    int32_t params[2] = {100, 200};
    int response_received = 0;
    int32_t response_value = 0;
    
void on_add_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    printf("[RESPONSE] Response callback called: status=%d, result=%p, result_size=%zu\n", 
           resp->status, resp->result, resp->result_size);
    fflush(stdout);
    if (resp->status == UVRPC_OK && resp->result && resp->result_size >= sizeof(int32_t)) {
        response_value = *((int32_t*)resp->result);
        printf("[RESPONSE] Response received: %d\n", response_value);
        response_received = 1;
    } else {
        fprintf(stderr, "[RESPONSE] Invalid response\n");
        fflush(stderr);
    }
}
    
    ret = uvrpc_client_call(client, "Add", (const uint8_t*)params, sizeof(params), on_add_response, NULL);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to send request: %d\n", ret);
    } else {
        printf("Request sent\n");
        fflush(stdout);
        
        /* Run event loop to process response */
            int iterations = 0;
            while (!response_received && iterations < 1000) {
                uv_run(&loop, UV_RUN_NOWAIT);
                iterations++;
                if (iterations % 100 == 0) {
                    printf("Still waiting for response... (%d iterations)\n", iterations);
                    fflush(stdout);
                }
            }        
        if (response_received) {
            printf("Test PASSED: 100 + 200 = %d\n", response_value);
        } else {
            fprintf(stderr, "Test FAILED: No response received\n");
        }
    }
    
    /* Cleanup */
    printf("\nCleaning up...\n");
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    
    /* Close loop */
    uv_loop_close(&loop);
    
    printf("Test complete\n");
    return response_received ? 0 : 1;
}