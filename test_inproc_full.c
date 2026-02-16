#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int request_count = 0;
static int response_count = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    request_count++;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    request_count++;
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

void on_connect(int status, void* ctx) {
    printf("Connected: %d\n", status);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    response_count++;
    if (resp->status == UVRPC_OK) {
        printf("Response received: msgid=%lu, size=%zu\n", resp->msgid, resp->result_size);
        fflush(stdout);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    printf("=== INPROC Full Functionality Test ===\n");

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://full_test");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    assert(server != NULL);

    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_register(server, "add", add_handler, NULL);

    int ret = uvrpc_server_start(server);
    assert(ret == UVRPC_OK);
    printf("Server started\n");
    fflush(stdout);

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://full_test");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);

    ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    assert(ret == UVRPC_OK);

    /* Wait for connection */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    printf("Client connected\n");

    /* Test 1: Echo request */
    printf("\nTest 1: Echo request\n");
    const char* test_data = "Hello, INPROC!";
    ret = uvrpc_client_call(client, "echo",
                            (uint8_t*)test_data, strlen(test_data),
                            on_response, NULL);
    assert(ret == UVRPC_OK);

    for (int i = 0; i < 50 && response_count < 1; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    assert(response_count == 1);
    printf("Echo test PASSED\n");

    /* Test 2: Add request */
    printf("\nTest 2: Add request\n");
    int32_t params[2] = {10, 20};
    response_count = 0;
    ret = uvrpc_client_call(client, "add",
                            (uint8_t*)params, sizeof(params),
                            on_response, NULL);
    assert(ret == UVRPC_OK);

    for (int i = 0; i < 50 && response_count < 1; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    assert(response_count == 1);
    printf("Add test PASSED\n");

    /* Test 3: Multiple requests */
    printf("\nTest 3: Multiple requests (10)\n");
    response_count = 0;
    for (int i = 0; i < 10; i++) {
        ret = uvrpc_client_call(client, "echo",
                                (uint8_t*)test_data, strlen(test_data),
                                on_response, NULL);
        assert(ret == UVRPC_OK);
    }

    for (int i = 0; i < 200 && response_count < 10; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    assert(response_count == 10);
    printf("Multiple requests test PASSED\n");

    /* Summary */
    printf("\n=== Test Summary ===\n");
    printf("Requests processed: %d\n", request_count);
    printf("Responses received: %d\n", response_count);
    printf("All tests PASSED!\n");

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    return 0;
}