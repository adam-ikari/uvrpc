#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;
static volatile int g_connected = 0;
static int g_response_count = 0;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

void connection_callback(int status, void* ctx) {
    (void)ctx;
    if (status == UVRPC_OK) {
        g_connected = 1;
        printf("Connected!\n");
        fflush(stdout);
    } else {
        printf("Connection failed: %d\n", status);
        fflush(stdout);
        g_running = 0;
    }
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    g_response_count++;
    printf("Response %d: status=%d\n", g_response_count, resp->status);
    fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <address>\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    
    const char* address = argv[1];
    
    printf("Creating loop...\n");
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("Creating config...\n");
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    printf("Creating client...\n");
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    
    printf("Connecting...\n");
    int ret = uvrpc_client_connect_with_callback(client, connection_callback, NULL);
    if (ret != UVRPC_OK) {
        printf("Failed to initiate connection: %d\n", ret);
        return 1;
    }
    
    printf("Waiting for connection...\n");
    int iter = 0;
    while (!g_connected && iter < 50 && g_running) {
        printf("Loop iteration %d (before uv_run)\n", iter);
        fflush(stdout);
        uv_run(&loop, UV_RUN_ONCE);
        printf("Loop iteration %d (after uv_run)\n", iter);
        fflush(stdout);
        iter++;
        usleep(100000);  // 100ms
    }
    
    if (!g_connected) {
        printf("Connection timeout\n");
        return 1;
    }
    
    printf("Sending request...\n");
    const char* msg = "Hello";
    ret = uvrpc_client_call(client, "echo", (uint8_t*)msg, strlen(msg), 
                             response_callback, NULL);
    if (ret != UVRPC_OK) {
        printf("Failed to send request: %d\n", ret);
        return 1;
    }
    
    printf("Waiting for response (max 5 seconds)...\n");
    iter = 0;
    while (g_response_count == 0 && iter < 50 && g_running) {
        printf("Waiting iteration %d\n", iter);
        fflush(stdout);
        uv_run(&loop, UV_RUN_ONCE);
        iter++;
        usleep(100000);  // 100ms
    }
    
    printf("Final response count: %d\n", g_response_count);
    
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}