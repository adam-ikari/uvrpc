/**
 * @file uvbus_broadcast_test.c
 * @brief UVBus Broadcast API Tests
 *
 * Tests for uvbus_broadcast() covering:
 * - NULL parameter handling
 * - Inactive server behavior
 * - TCP multi-client broadcast
 * - INPROC multi-client broadcast (zero-copy path)
 * - UDP multi-client broadcast (point-to-point)
 * - IPC multi-client broadcast (shared buffer with refcount)
 * - SAMELOOP multi-client broadcast (direct callback, no serialization)
 * - Broadcast with no clients connected
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "../include/uvbus.h"

#define TEST_TCP_PORT 15560
#define TEST_TCP_ADDRESS "tcp://127.0.0.1:15560"
#define TEST_INPROC_ADDRESS "inproc://broadcast_test"
#define TEST_TIMEOUT_MS 5000
#define TEST_MESSAGE "Broadcast test message"
#define NUM_CLIENTS 3

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) do { printf("\n[TEST] %s\n", name); fflush(stdout); } while(0)
#define TEST_PASS() do { tests_passed++; printf("[PASS] %s\n", __func__); } while(0)
#define TEST_FAIL(msg) do { tests_failed++; printf("[FAIL] %s: %s\n", __func__, msg); } while(0)
#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) { TEST_FAIL(msg); return; } } while(0)
#define ASSERT_NOT_NULL(ptr, msg) do { if ((ptr) == NULL) { TEST_FAIL(msg); return; } } while(0)

/* Per-client receive tracking */
typedef struct {
    int recv_count;
    char recv_data[1024];
    size_t recv_size;
} client_recv_ctx_t;

/* Server recv callback (shouldn't receive in broadcast test) */
static void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)data; (void)size; (void)client_ctx; (void)server_ctx;
}

/* Client recv callback */
static void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)server_ctx;
    client_recv_ctx_t* ctx = (client_recv_ctx_t*)client_ctx;
    ctx->recv_count++;
    if (size < sizeof(ctx->recv_data)) {
        memcpy(ctx->recv_data, data, size);
        ctx->recv_size = size;
    }
}

/* Connect callback (no-op, just for completeness) */
static void connect_cb(uvbus_error_t status, void* ctx) {
    (void)status; (void)ctx;
}

/**
 * Test 1: uvbus_broadcast(NULL, data, size) returns UVBUS_ERROR_INVALID_PARAM
 */
void test_broadcast_null_params(void) {
    TEST_START("test_broadcast_null_params");

    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    uvbus_error_t result = uvbus_broadcast(NULL, msg, strlen(TEST_MESSAGE));
    ASSERT_EQ(result, UVBUS_ERROR_INVALID_PARAM, "Expected UVBUS_ERROR_INVALID_PARAM for NULL bus");

    TEST_PASS();
}

/**
 * Test 2: Server created but NOT listening, broadcast returns UVBUS_ERROR_NOT_CONNECTED
 */
void test_broadcast_inactive_server(void) {
    TEST_START("test_broadcast_inactive_server");

    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NOT_NULL(config, "Failed to create config");

    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    uvbus_config_set_recv_callback(config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");

    /* Do NOT call uvbus_listen() - server is inactive */
    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    uvbus_error_t result = uvbus_broadcast(server, msg, strlen(TEST_MESSAGE));
    ASSERT_EQ(result, UVBUS_ERROR_NOT_CONNECTED, "Expected UVBUS_ERROR_NOT_CONNECTED for inactive server");

    uvbus_free(server);
    uvbus_config_free(config);
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Helper: Run multi-client broadcast integration test for a given transport type.
 */
static void run_multi_client_broadcast_test(const char* test_name,
                                             uvbus_transport_type_t transport,
                                             const char* address) {
    TEST_START(test_name);

    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    /* INPROC and SAMELOOP peers meet through a registry the caller owns; the
     * socket transports ignore it. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    ASSERT_NOT_NULL(registry, "Failed to create registry");

    /* Create server config and server */
    uvbus_config_t* server_config = uvbus_config_new();
    ASSERT_NOT_NULL(server_config, "Failed to create server config");

    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_loop_registry(server_config, registry);
    uvbus_config_set_transport(server_config, transport);
    uvbus_config_set_address(server_config, address);
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(server_config);
    ASSERT_NOT_NULL(server, "Failed to create server");

    uvbus_error_t result = uvbus_listen(server);
    ASSERT_EQ(result, UVBUS_OK, "Failed to listen");

    /* Create 3 clients, each with its own recv context */
    uvbus_config_t* client_configs[NUM_CLIENTS];
    uvbus_t* clients[NUM_CLIENTS];
    client_recv_ctx_t recv_ctxs[NUM_CLIENTS];

    memset(recv_ctxs, 0, sizeof(recv_ctxs));

    for (int i = 0; i < NUM_CLIENTS; i++) {
        client_configs[i] = uvbus_config_new();
        ASSERT_NOT_NULL(client_configs[i], "Failed to create client config");

        uvbus_config_set_loop(client_configs[i], &loop);
        uvbus_config_set_loop_registry(client_configs[i], registry);
        uvbus_config_set_transport(client_configs[i], transport);
        uvbus_config_set_address(client_configs[i], address);
        uvbus_config_set_recv_callback(client_configs[i], client_recv, &recv_ctxs[i]);
        uvbus_config_set_connect_callback(client_configs[i], connect_cb, NULL);

        clients[i] = uvbus_client_new(client_configs[i]);
        ASSERT_NOT_NULL(clients[i], "Failed to create client");

        result = uvbus_connect(clients[i]);
        ASSERT_EQ(result, UVBUS_OK, "Failed to initiate connect");
    }

    /* Poll to establish connections */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);
    }

    /* Verify all clients are connected */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (!uvbus_is_connected(clients[i])) {
            printf("WARNING: Client %d not connected after initial poll, continuing...\n", i);
        }
    }

    /* Broadcast a message from the server */
    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    size_t msg_len = strlen(TEST_MESSAGE);
    result = uvbus_broadcast(server, msg, msg_len);
    ASSERT_EQ(result, UVBUS_OK, "Broadcast failed");

    /* Poll until all clients received or timeout */
    int all_received = 0;
    for (int iter = 0; iter < 100 && !all_received; iter++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);

        all_received = 1;
        for (int i = 0; i < NUM_CLIENTS; i++) {
            if (recv_ctxs[i].recv_count < 1) {
                all_received = 0;
                break;
            }
        }
    }

    /* Assert each client received exactly once with correct data */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        char assert_msg[64];
        snprintf(assert_msg, sizeof(assert_msg), "Client %d recv_count != 1", i);
        ASSERT_EQ(recv_ctxs[i].recv_count, 1, assert_msg);

        snprintf(assert_msg, sizeof(assert_msg), "Client %d data mismatch", i);
        ASSERT_EQ(recv_ctxs[i].recv_size, msg_len, assert_msg);

        if (memcmp(recv_ctxs[i].recv_data, msg, msg_len) != 0) {
            TEST_FAIL("Client received data does not match broadcast message");
            goto cleanup;
        }
    }

    TEST_PASS();

cleanup:
    /* Cleanup: free clients BEFORE their configs (avoid use-after-free) */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_free(clients[i]);
    }
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_config_free(client_configs[i]);
    }
    uvbus_free(server);
    uvbus_config_free(server_config);
    uvbus_loop_registry_free(registry);
    uv_loop_close(&loop);
}

/**
 * Test 3: TCP multi-client broadcast - server + 3 clients, broadcast, verify delivery
 */
void test_broadcast_tcp_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_tcp_multi_client",
        UVBUS_TRANSPORT_TCP,
        TEST_TCP_ADDRESS
    );
}

/**
 * Test 4: INPROC multi-client broadcast - zero-copy path
 */
void test_broadcast_inproc_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_inproc_multi_client",
        UVBUS_TRANSPORT_INPROC,
        TEST_INPROC_ADDRESS
    );
}

/**
 * Test: UDP multi-client broadcast - point-to-point send to each client
 */
void test_broadcast_udp_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_udp_multi_client",
        UVBUS_TRANSPORT_UDP,
        "udp://127.0.0.1:15562"
    );
}

/**
 * Test: IPC multi-client broadcast - shared buffer with refcount
 */
void test_broadcast_ipc_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_ipc_multi_client",
        UVBUS_TRANSPORT_IPC,
        "ipc://@uvrpc_broadcast_ipc_test"
    );
}

/**
 * Test: SAMELOOP multi-client broadcast - direct callback, no serialization
 */
void test_broadcast_sameloop_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_sameloop_multi_client",
        UVBUS_TRANSPORT_SAMELOOP,
        "sameloop://broadcast_test"
    );
}

/**
 * Test 5: Server listens with no clients, broadcast returns UVBUS_OK
 */
void test_broadcast_no_clients(void) {
    TEST_START("test_broadcast_no_clients");

    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NOT_NULL(config, "Failed to create config");

    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:15561");
    uvbus_config_set_recv_callback(config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");

    uvbus_error_t result = uvbus_listen(server);
    ASSERT_EQ(result, UVBUS_OK, "Failed to listen");

    /* Broadcast with no clients connected */
    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    result = uvbus_broadcast(server, msg, strlen(TEST_MESSAGE));
    ASSERT_EQ(result, UVBUS_OK, "Expected UVBUS_OK when broadcasting with no clients");

    uvbus_free(server);
    uvbus_config_free(config);
    uv_loop_close(&loop);
    TEST_PASS();
}

int main(void) {
    printf("========================================\n");
    printf("  UVBus Broadcast API Tests\n");
    printf("========================================\n");

    test_broadcast_null_params();
    test_broadcast_inactive_server();
    test_broadcast_no_clients();
    test_broadcast_tcp_multi_client();
    test_broadcast_inproc_multi_client();
    test_broadcast_udp_multi_client();
    test_broadcast_ipc_multi_client();
    test_broadcast_sameloop_multi_client();

    printf("\n=== Broadcast Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
