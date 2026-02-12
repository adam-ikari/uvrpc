/**
 * UVRPC Simple Client Example
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Response callback */
void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    printf("Received response: msgid=%lu, status=%d\n", resp->msgid, resp->status);
    
    if (resp->status == UVRPC_OK) {
        printf("Result: %zu bytes\n", resp->result_size);
        if (resp->result_size > 0) {
            printf("Data: ");
            for (size_t i = 0; i < resp->result_size && i < 32; i++) {
                printf("%02x ", resp->result[i]);
            }
            printf("\n");
            
            /* Try to print as string if possible */
            if (resp->result_size < 1024) {
                char str[1025];
                memcpy(str, resp->result, resp->result_size);
                str[resp->result_size] = '\0';
                if (resp->result_size == strlen(str)) {
                    printf("String: %s\n", str);
                }
            }
        }
    } else if (resp->error) {
        printf("Error: %s\n", resp->error);
    }
    
    g_running = 0; /* Exit after first response */
}

int run_server_or_client(uv_loop_t* loop, int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    const char* method = (argc > 2) ? argv[2] : "echo";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Simple Client\n");
    printf("Address: %s\n", address);
    printf("Method: %s\n\n", method);
    
        
    /* Create client configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
            return 1;
    }
    
    /* Connect to server */
    printf("Connecting to server...\n");
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
            return 1;
    }
    
    printf("Connected. Sending request...\n\n");
    
    /* Prepare request data */
    uint8_t params[256];
    size_t params_size = 0;
    
    if (strcmp(method, "echo") == 0) {
        const char* message = "Hello, UVRPC!";
        memcpy(params, message, strlen(message));
        params_size = strlen(message);
    } else if (strcmp(method, "add") == 0) {
        int32_t a = 42;
        int32_t b = 58;
        memcpy(params, &a, sizeof(a));
        memcpy(params + sizeof(a), &b, sizeof(b));
        params_size = sizeof(a) + sizeof(b);
    }
    
    /* Send request */
    if (uvrpc_client_call(client, method, params, params_size,
                           response_callback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to send request\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
            return 1;
    }
    
    printf("Request sent. Waiting for response...\n\n");
    
    /* Run event loop */
    while (g_running) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Cleanup */
    printf("\nDisconnecting...\n");
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(loop);
    
    printf("Client stopped\n");
    return 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    int result = run_server_or_client(&loop, argc, argv);
    
    uv_loop_close(&loop);
    return result;
}