/**
 * UVBus Server Only - Standalone UVBus server
 */

#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
#include "../include/uvbus.h"

void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER RECV] CALLED! size=%zu, data=%p, client_ctx=%p, server_ctx=%p\n", 
           size, data, client_ctx, server_ctx);
    fflush(stdout);
    printf("[SERVER] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    fflush(stdout);
    
    uvbus_t* server = (uvbus_t*)server_ctx;
    if (server && client_ctx) {
        uvbus_send_to(server, data, size, client_ctx);
        printf("[SERVER] Echoed back\n");
        fflush(stdout);
    }
}

void server_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[SERVER ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown");
}

int main() {
    printf("UVBus Server\n");
    printf("============\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:8989");
    
    /* IMPORTANT: Set callbacks BEFORE creating server */
    uvbus_config_set_recv_callback(config, server_recv, config);
    uvbus_config_set_error_callback(config, server_error, config);
    
    uvbus_t* server = uvbus_server_new(config);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }
    
    if (uvbus_listen(server) != UVBUS_OK) {
        printf("Failed to listen\n");
        return 1;
    }
    
    printf("Server listening on tcp://127.0.0.1:8989\n");
    printf("Press Ctrl+C to stop\n\n");
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    printf("Server stopped\n");
    uvbus_free(server);
    uvbus_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
