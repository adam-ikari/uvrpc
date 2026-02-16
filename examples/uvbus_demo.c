/**
 * UVBus Demo - Simple example showing how to use UVBus
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "../include/uvbus.h"

/* Receive callback */
void on_recv(const uint8_t* data, size_t size, void* ctx) {
    printf("Received %zu bytes\n", size);
    if (size > 0 && data) {
        printf("Data: %.*s\n", (int)size, (char*)data);
    }
}

/* Connect callback */
void on_connect(uvbus_error_t status, void* ctx) {
    if (status == UVBUS_OK) {
        printf("Connected successfully\n");
    } else {
        printf("Connection failed with error: %d\n", status);
    }
}

/* Error callback */
void on_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("Error: %d - %s\n", error_code, error_msg ? error_msg : "Unknown error");
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("UVBus Demo\n");
    printf("==========\n\n");
    
    /* Create configuration */
    uvbus_config_t* config = uvbus_config_new();
    if (!config) {
        printf("Failed to create config\n");
        return 1;
    }
    
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:6666");
    
    uvbus_config_set_recv_callback(config, on_recv, NULL);
    uvbus_config_set_connect_callback(config, on_connect, NULL);
    uvbus_config_set_error_callback(config, on_error, NULL);
    
    printf("Configuration created:\n");
    printf("  Transport: TCP\n");
    printf("  Address: tcp://127.0.0.1:6666\n\n");
    
    /* Create client */
    uvbus_t* client = uvbus_client_new(config);
    if (!client) {
        printf("Failed to create client\n");
        uvbus_config_free(config);
        return 1;
    }
    
    printf("Client created\n");
    printf("  Transport type: %d\n", uvbus_get_transport_type(client));
    printf("  Is server: %s\n", uvbus_is_server(client) ? "yes" : "no");
    printf("  Is connected: %s\n\n", uvbus_is_connected(client) ? "yes" : "no");
    
    /* Note: Actual connection would require server to be running
     * This is just to demonstrate the API */
    
    /* Cleanup */
    printf("Cleaning up...\n");
    uvbus_free(client);
    uvbus_config_free(config);
    
    uv_loop_close(&loop);
    
    printf("Demo completed successfully\n");
    return 0;
}