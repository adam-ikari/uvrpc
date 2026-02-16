#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int server_done = 0;
static int client_done = 0;

void simple_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[SERVER] Received request\n");
    int32_t result = 42;
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
}

void server_thread(void* arg) {
    uv_loop_t* loop = (uv_loop_t*)arg;
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, "inproc://test");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        printf("[SERVER] Failed to create server\n");
        return;
    }
    
    uvrpc_server_register(server, "test", simple_handler, NULL);
    
    int ret = uvrpc_server_start(server);
    printf("[SERVER] Start result: %d\n", ret);
    
    if (ret == UVRPC_OK) {
        printf("[SERVER] Running loop...\n");
        uv_run(loop, UV_RUN_DEFAULT);
    }
    
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    server_done = 1;
}

void client_thread(void* arg) {
    (void)arg;
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    sleep(1);  // Wait for server
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "inproc://test");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        printf("[CLIENT] Failed to create client\n");
        return;
    }
    
    int ret = uvrpc_client_connect(client);
    printf("[CLIENT] Connect result: %d\n", ret);
    
    if (ret == UVRPC_OK) {
        int32_t params = 10;
        uvrpc_response_t* resp = uvrpc_client_call(client, "test", (uint8_t*)&params, sizeof(params), 5000);
        if (resp) {
            printf("[CLIENT] Got response: status=%d\n", resp->status);
            if (resp->data && resp->size >= 4) {
                int32_t result = *(int32_t*)resp->data;
                printf("[CLIENT] Result: %d\n", result);
            }
            uvrpc_response_free(resp);
        }
    }
    
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    client_done = 1;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Start server in background */
    uv_thread_t server_tid;
    uv_thread_create(&server_tid, server_thread, &loop);
    
    sleep(1);
    
    /* Run client in main thread */
    client_thread(NULL);
    
    /* Stop server */
    uv_stop(&loop);
    uv_thread_join(&server_tid);
    
    uv_loop_close(&loop);
    
    printf("[MAIN] Test completed\n");
    return 0;
}
