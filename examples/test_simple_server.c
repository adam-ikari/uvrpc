#include <stdio.h>
#include <stdlib.h>
#include "../include/uvbus.h"

int main() {
    printf("Testing uvbus_perf_test crash...\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:15556");
    
    uvbus_t* server = uvbus_server_new(config);
    uvbus_config_free(config);
    
    if (!server) {
        printf("Failed to create server\n");
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Server created successfully\n");
    
    uvbus_error_t err = uvbus_listen(server);
    if (err != UVBUS_OK) {
        printf("Failed to listen: %d\n", err);
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Server listening\n");
    
    uvbus_free(server);
    uv_loop_close(&loop);
    
    printf("Test completed successfully\n");
    return 0;
}