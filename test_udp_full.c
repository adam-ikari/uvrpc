/**
 * UDP Full Functionality Test
 */

#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int response_count = 0;
static uvrpc_client_t* g_client = NULL;
static uv_loop_t* g_loop = NULL;

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    response_count++;
    if (resp->status == UVRPC_OK) {
        printf("Response received: msgid=%lu, size=%zu\n", resp->msgid, resp->result_size);
        fflush(stdout);
    }
}

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

/* Add handler */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    }
}

/* Connection callback */
void on_connect(int status, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;
    printf("Connected: %d\n", status);
    fflush(stdout);
    
    if (status == 0) {
        printf("Client connected\n\n");
        fflush(stdout);

        /* Test 1: Echo request */
        printf("Test 1: Echo request\n");
        fflush(stdout);
        const char* echo_msg = "Hello, UDP!";
        uvrpc_client_call(client, "echo", (uint8_t*)echo_msg, strlen(echo_msg), on_response, NULL);

        /* Run loop to get response */
        for (int i = 0; i < 50; i++) {
            uv_run(g_loop, UV_RUN_ONCE);
        }

        if (response_count == 1) {
            printf("Echo test PASSED\n\n");
            fflush(stdout);

            /* Test 2: Add request */
            printf("Test 2: Add request\n");
            fflush(stdout);
            int32_t params[2] = {10, 20};
            uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, NULL);

            /* Run loop to get response */
            for (int i = 0; i < 50; i++) {
                uv_run(g_loop, UV_RUN_ONCE);
            }

            if (response_count == 2) {
                printf("Add test PASSED\n\n");
                fflush(stdout);

                /* Test 3: Multiple requests */
                printf("Test 3: Multiple requests (5)\n");
                fflush(stdout);
                for (int i = 0; i < 5; i++) {
                    uvrpc_client_call(client, "echo", (uint8_t*)echo_msg, strlen(echo_msg), on_response, NULL);
                }

                /* Run loop to get all responses */
                for (int i = 0; i < 100; i++) {
                    uv_run(g_loop, UV_RUN_ONCE);
                }

                if (response_count == 7) {
                    printf("Multiple requests test PASSED\n\n");
                    fflush(stdout);
                    printf("=== ALL UDP TESTS PASSED ===\n");
                    fflush(stdout);
                }
            }
        }
    }
}

int main() {
    printf("=== UDP Full Functionality Test ===\n");
    fflush(stdout);

    /* Create loop */
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "Failed to init loop\n");
        return 1;
    }
    g_loop = &loop;

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "udp://127.0.0.1:9999");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_register(server, "add", add_handler, NULL);

    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        return 1;
    }
    printf("Server started\n");
    fflush(stdout);

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "udp://127.0.0.1:9999");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }

    int connect_result = uvrpc_client_connect_with_callback(client, on_connect, client);

    if (connect_result != UVRPC_OK) {
        fprintf(stderr, "Failed to connect\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }

    /* Run loop briefly */
    for (int i = 0; i < 200; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

/* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);

    return 0;
}