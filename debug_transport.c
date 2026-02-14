/**
 * Debug test for transport layer
 */

#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_request_received = 0;
static volatile int g_response_received = 0;

void debug_server_recv(uint8_t* data, size_t size, void* ctx) {
    printf("[SERVER] Received %zu bytes\n", size);
    fflush(stdout);

    /* Decode request */
    uint32_t msgid;
    char* method = NULL;
    const uint8_t* params = NULL;
    size_t params_size = 0;

    int rv = uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size);
    if (rv != UVRPC_OK) {
        printf("[SERVER] Failed to decode request: %d\n", rv);
        fflush(stdout);
        return;
    }

    printf("[SERVER] Decoded: msgid=%u, method='%s', params_size=%zu\n", msgid, method, params_size);
    fflush(stdout);

    g_request_received = 1;

    /* Send response */
    int32_t result = 42;
    uint8_t* resp_data = NULL;
    size_t resp_size = 0;

    rv = uvrpc_encode_response(msgid, 0, (uint8_t*)&result, sizeof(result), &resp_data, &resp_size);
    if (rv != UVRPC_OK) {
        printf("[SERVER] Failed to encode response: %d\n", rv);
        fflush(stdout);
        free(method);
        return;
    }

    printf("[SERVER] Sending response: %zu bytes\n", resp_size);
    fflush(stdout);

    /* We need to send this back to the client */
    /* For now, just free */
    free(method);
    free(resp_data);
}

void debug_client_recv(uint8_t* data, size_t size, void* ctx) {
    printf("[CLIENT] Received %zu bytes\n", size);
    fflush(stdout);

    /* Decode response */
    uint32_t msgid;
    int32_t error_code;
    const uint8_t* result = NULL;
    size_t result_size = 0;

    int rv = uvrpc_decode_response(data, size, &msgid, &error_code, &result, &result_size);
    if (rv != UVRPC_OK) {
        printf("[CLIENT] Failed to decode response: %d\n", rv);
        fflush(stdout);
        return;
    }

    printf("[CLIENT] Decoded: msgid=%u, error_code=%d, result_size=%zu\n", msgid, error_code, result_size);
    fflush(stdout);

    if (result_size == sizeof(int32_t)) {
        int32_t r = *(int32_t*)result;
        printf("[CLIENT] Result: %d\n", r);
        fflush(stdout);
    }

    g_response_received = 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server|client>\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];
    const char* address = "127.0.0.1:5555";

    if (strcmp(mode, "server") == 0) {
        printf("[SERVER] Starting...\n");
        fflush(stdout);

        uv_loop_t loop;
        uv_loop_init(&loop);

        uvrpc_transport_t* transport = uvrpc_transport_server_new(&loop, UVRPC_TRANSPORT_TCP);
        if (!transport) {
            fprintf(stderr, "[SERVER] Failed to create transport\n");
            return 1;
        }

        int rv = uvrpc_transport_listen(transport, address, debug_server_recv, NULL);
        if (rv != UVRPC_OK) {
            fprintf(stderr, "[SERVER] Failed to listen: %d\n", rv);
            return 1;
        }

        printf("[SERVER] Listening on %s\n", address);
        fflush(stdout);

        uv_run(&loop, UV_RUN_DEFAULT);

        uvrpc_transport_free(transport);
        uv_loop_close(&loop);

    } else if (strcmp(mode, "client") == 0) {
        printf("[CLIENT] Starting...\n");
        fflush(stdout);

        uv_loop_t loop;
        uv_loop_init(&loop);

        uvrpc_transport_t* transport = uvrpc_transport_client_new(&loop, UVRPC_TRANSPORT_TCP);
        if (!transport) {
            fprintf(stderr, "[CLIENT] Failed to create transport\n");
            return 1;
        }

        /* Connect with callback */
        int rv = uvrpc_transport_connect(transport, address, NULL, debug_client_recv, NULL);
        if (rv != UVRPC_OK) {
            fprintf(stderr, "[CLIENT] Failed to connect: %d\n", rv);
            return 1;
        }

        printf("[CLIENT] Connecting...\n");
        fflush(stdout);

        /* Wait for connection */
        int iterations = 0;
        while (!transport->is_connected && iterations < 100) {
            uv_run(&loop, UV_RUN_ONCE);
            iterations++;
            if (iterations % 10 == 0) {
                printf("[CLIENT] Waiting for connection... (%d/100)\n", iterations);
                fflush(stdout);
            }
        }

        if (!transport->is_connected) {
            fprintf(stderr, "[CLIENT] Connection timeout\n");
            return 1;
        }

        printf("[CLIENT] Connected!\n");
        fflush(stdout);

        /* Send request */
        uint8_t* req_data = NULL;
        size_t req_size = 0;

        rv = uvrpc_encode_request(1, "test", NULL, 0, &req_data, &req_size);
        if (rv != UVRPC_OK) {
            fprintf(stderr, "[CLIENT] Failed to encode request: %d\n", rv);
            return 1;
        }

        printf("[CLIENT] Sending request: %zu bytes\n", req_size);
        fflush(stdout);

        uvrpc_transport_send(transport, req_data, req_size);

        free(req_data);

        /* Wait for response */
        iterations = 0;
        while (!g_response_received && iterations < 100) {
            uv_run(&loop, UV_RUN_ONCE);
            iterations++;
            if (iterations % 10 == 0) {
                printf("[CLIENT] Waiting for response... (%d/100)\n", iterations);
                fflush(stdout);
            }
        }

        if (!g_response_received) {
            fprintf(stderr, "[CLIENT] Response timeout\n");
            return 1;
        }

        printf("[CLIENT] Test passed!\n");
        fflush(stdout);

        uvrpc_transport_free(transport);
        uv_loop_close(&loop);

    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return 1;
    }

    return 0;
}