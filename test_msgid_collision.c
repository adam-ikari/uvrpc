#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Testing msgid collision...\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create 10 clients */
    uvrpc_client_t* clients[10];
    for (int i = 0; i < 10; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
        
        clients[i] = uvrpc_client_create(config);
        if (!clients[i]) {
            printf("Failed to create client %d\n", i);
            return 1;
        }
        printf("Client %d created\n", i);
    }
    
    /* Start server */
    system("/home/zhaodi-chen/project/uvrpc/dist/bin/perf_server &");
    sleep(2);
    
    /* Connect all clients */
    for (int i = 0; i < 10; i++) {
        int ret = uvrpc_client_connect_with_callback(clients[i], NULL, NULL);
        printf("Client %d connect: %d\n", i, ret);
    }
    
    /* Run event loop */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Try to send requests */
    int32_t params[2] = {10, 20};
    int success = 0, failed = 0;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            int ret = uvrpc_client_call(clients[i], "add", (uint8_t*)params, sizeof(params), NULL, NULL);
            if (ret == UVRPC_OK) {
                success++;
            } else {
                printf("Client %d request %d failed: %d\n", i, j, ret);
                failed++;
            }
        }
    }
    
    printf("\nResults: Success=%d, Failed=%d\n", success, failed);
    
    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        uvrpc_client_free(clients[i]);
    }
    
    /* Run event loop for cleanup */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    uv_loop_close(&loop);
    killall -9 perf_server 2>/dev/null;
    
    return 0;
}