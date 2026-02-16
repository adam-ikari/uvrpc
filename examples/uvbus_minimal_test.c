/**
 * UVBus Minimal Test - Minimal test to isolate the issue
 */

#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
#include "../include/uvbus.h"

int main() {
    printf("UVBus Minimal Test\n");
    printf("===================\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    int result = uv_loop_init(&loop);
    printf("uv_loop_init returned: %d\n", result);
    
    /* Create server configuration */
    uvbus_config_t* config = uvbus_config_new();
    printf("uvbus_config_new returned: %p\n", config);
    
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:9999");
    
    /* Create server */
    printf("Creating server...\n");
    uvbus_t* server = uvbus_server_new(config);
    printf("uvbus_server_new returned: %p\n", server);
    
    if (server) {
        printf("Server created successfully\n");
        
        /* Start listening */
        printf("Starting listen...\n");
        uvbus_error_t listen_result = uvbus_listen(server);
        printf("uvbus_listen returned: %d\n", listen_result);
        
        if (listen_result == UVBUS_OK) {
            printf("Server is listening\n");
            
            /* Run loop briefly */
            printf("Running loop...\n");
            for (int i = 0; i < 5; i++) {
                uv_run(&loop, UV_RUN_ONCE);
                uv_sleep(10);
            }
            
            printf("Loop ran successfully\n");
        } else {
            printf("Failed to listen: %d\n", listen_result);
        }
        
        /* Cleanup */
        printf("Cleaning up server...\n");
        uvbus_free(server);
    } else {
        printf("Failed to create server\n");
    }
    
    uvbus_config_free(config);
    uv_loop_close(&loop);
    
    printf("Test completed\n");
    return 0;
}