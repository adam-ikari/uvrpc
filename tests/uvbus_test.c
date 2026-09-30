/**
 * @file uvbus_test.c
 * @brief UVBus Unit Tests
 * 
 * Comprehensive unit tests for UVBus layer including:
 * - Configuration management
 * - TCP transport operations
 * - Error handling
 * - Memory leak verification
 * - Callback mechanisms
 * 
 * @author UVRPC Team
 * @date 2026-02-22
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <uv.h>
#include "../include/uvbus.h"
#include "../include/uvbus_config.h"

/* Test configuration */
#define TEST_TCP_PORT 15555
#define TEST_TCP_ADDRESS "tcp://127.0.0.1:15555"
#define TEST_TIMEOUT_MS 5000
#define TEST_MESSAGE "Hello UVBus!"
#define TEST_LARGE_MESSAGE_SIZE (1024 * 1024) /* 1MB */

/* Test statistics */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test context */
typedef struct {
    uv_loop_t* loop;
    int recv_count;
    int connect_count;
    int close_count;
    int error_count;
    char* recv_data;
    size_t recv_size;
    int test_complete;
    int test_success;
} test_context_t;

/* Helper macros */
#define TEST_START(name) \
    printf("\n[TEST] %s\n", name); \
    fflush(stdout);

#define TEST_PASS() \
    do { \
        tests_passed++; \
        printf("[PASS] %s\n", __func__); \
    } while (0)

#define TEST_FAIL(msg) \
    do { \
        tests_failed++; \
        printf("[FAIL] %s: %s\n", __func__, msg); \
    } while (0)

#define ASSERT_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(condition, msg) \
    do { \
        if (condition) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg) \
    do { \
        if ((ptr) == NULL) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(ptr, msg) \
    do { \
        if ((ptr) != NULL) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b, msg) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while (0)

/* Callback functions */
static void test_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    test_context_t* ctx = (test_context_t*)server_ctx;
    ctx->recv_count++;
    
    if (ctx->recv_data) {
        free(ctx->recv_data);
    }
    
    ctx->recv_data = malloc(size + 1);
    if (ctx->recv_data) {
        memcpy(ctx->recv_data, data, size);
        ctx->recv_data[size] = '\0';
        ctx->recv_size = size;
    }
}

static void test_connect_callback(uvbus_error_t status, void* user_ctx) {
    test_context_t* ctx = (test_context_t*)user_ctx;
    ctx->connect_count++;
    
    if (status == UVBUS_OK) {
        ctx->test_success = 1;
    } else {
        ctx->test_success = 0;
    }
    ctx->test_complete = 1;
}

static void test_close_callback(void* user_ctx) {
    test_context_t* ctx = (test_context_t*)user_ctx;
    ctx->close_count++;
}

static void test_error_callback(uvbus_error_t error_code, const char* error_msg, void* user_ctx) {
    test_context_t* ctx = (test_context_t*)user_ctx;
    ctx->error_count++;
    printf("[ERROR] Code: %d, Msg: %s\n", error_code, error_msg);
}

/* Test functions */

/**
 * Test 1: Create and free UVBus config
 */
void test_config_create_free(void) {
    TEST_START("Config create and free");
    
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NOT_NULL(config, "Failed to create config");
    
    uvbus_config_free(config);
    TEST_PASS();
}

/**
 * Test 2: Set and get config parameters
 */
void test_config_set_get(void) {
    TEST_START("Config set and get parameters");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NOT_NULL(config, "Failed to create config");
    
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    ASSERT_EQ(config->transport, UVBUS_TRANSPORT_TCP, "Transport type mismatch");
    ASSERT_STR_EQ(config->address, TEST_TCP_ADDRESS, "Address mismatch");
    
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 3: Set all callbacks
 */
void test_config_callbacks(void) {
    TEST_START("Config callbacks");
    
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NOT_NULL(config, "Failed to create config");
    
    test_context_t ctx = {0};
    
    uvbus_config_set_recv_callback(config, test_recv_callback, &ctx);
    uvbus_config_set_connect_callback(config, test_connect_callback, &ctx);
    uvbus_config_set_close_callback(config, test_close_callback, &ctx);
    uvbus_config_set_error_callback(config, test_error_callback, &ctx);
    
    ASSERT_NOT_NULL(config->recv_cb, "Receive callback not set");
    ASSERT_NOT_NULL(config->connect_cb, "Connect callback not set");
    ASSERT_NOT_NULL(config->close_cb, "Close callback not set");
    ASSERT_NOT_NULL(config->error_cb, "Error callback not set");
    
    uvbus_config_free(config);
    TEST_PASS();
}

/**
 * Test 4: Create and free server
 */
void test_server_create_free(void) {
    TEST_START("Server create and free");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");
    
    ASSERT_EQ(uvbus_is_server(server), 1, "Server flag not set");
    ASSERT_NOT_NULL(uvbus_get_loop(server), "Loop not set");
    ASSERT_STR_EQ(uvbus_get_address(server), TEST_TCP_ADDRESS, "Address mismatch");
    
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 5: Create server with NULL config
 */
void test_server_null_config(void) {
    TEST_START("Server NULL config");
    
    uvbus_t* server = uvbus_server_new(NULL);
    ASSERT_NULL(server, "Server created with NULL config");
    
    TEST_PASS();
}

/**
 * Test 6: Create and free client
 */
void test_client_create_free(void) {
    TEST_START("Client create and free");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* client = uvbus_client_new(config);
    ASSERT_NOT_NULL(client, "Failed to create client");
    
    ASSERT_EQ(uvbus_is_server(client), 0, "Server flag incorrectly set");
    ASSERT_EQ(uvbus_is_connected(client), 0, "Client incorrectly connected");
    
    uvbus_free(client);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 7: Client without server (connect failure)
 */
void test_client_connect_no_server(void) {
    TEST_START("Client connect without server");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:19999"); /* Non-existent server */
    
    uvbus_t* client = uvbus_client_new(config);
    ASSERT_NOT_NULL(client, "Failed to create client");
    
    uvbus_error_t result = uvbus_connect(client);
    /* Connection is async, so we just check it doesn't crash */
    
    uvbus_free(client);
    uvbus_config_free(config);
    /* Releasing while the attempt is in flight only drops a reference: the
     * connect callback still has to run for the transport and the client to
     * be reclaimed. Pump until they are, or they leak. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 8: Server listen and stop
 */
void test_server_listen_stop(void) {
    TEST_START("Server listen and stop");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");
    
    uvbus_error_t result = uvbus_listen(server);
    ASSERT_EQ(result, UVBUS_OK, "Failed to listen");
    
    uvbus_stop(server);
    
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 9: Invalid parameters error handling
 */
void test_invalid_params(void) {
    TEST_START("Invalid parameters");
    
    /* Test with NULL pointers */
    ASSERT_EQ(uvbus_listen(NULL), UVBUS_ERROR_INVALID_PARAM, "listen with NULL should fail");
    ASSERT_EQ(uvbus_connect(NULL), UVBUS_ERROR_INVALID_PARAM, "connect with NULL should fail");
    ASSERT_EQ(uvbus_send(NULL, NULL, 0), UVBUS_ERROR_INVALID_PARAM, "send with NULL should fail");
    ASSERT_EQ(uvbus_client_send(NULL, NULL, 0), UVBUS_ERROR_INVALID_PARAM, "client_send with NULL should fail");
    
    TEST_PASS();
}

/**
 * Test 10: Send data from unconnected client
 */
void test_client_send_unconnected(void) {
    TEST_START("Client send unconnected");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* client = uvbus_client_new(config);
    ASSERT_NOT_NULL(client, "Failed to create client");
    
    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    uvbus_error_t result = uvbus_client_send(client, msg, strlen(TEST_MESSAGE));
    ASSERT_EQ(result, UVBUS_ERROR_NOT_CONNECTED, "Send from unconnected client should fail");
    
    uvbus_free(client);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 11: Server send when not active
 */
void test_server_send_inactive(void) {
    TEST_START("Server send inactive");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");
    
    const uint8_t* msg = (const uint8_t*)TEST_MESSAGE;
    uvbus_error_t result = uvbus_send(server, msg, strlen(TEST_MESSAGE));
    ASSERT_EQ(result, UVBUS_ERROR_NOT_CONNECTED, "Send from inactive server should fail");
    
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 12: Memory leak test - multiple create/free cycles
 */
void test_memory_leak(void) {
    TEST_START("Memory leak test");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create and free multiple times */
    for (int i = 0; i < 100; i++) {
        uvbus_config_t* config = uvbus_config_new();
        uvbus_config_set_loop(config, &loop);
        uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
        uvbus_config_set_address(config, TEST_TCP_ADDRESS);
        
        uvbus_t* server = uvbus_server_new(config);
        if (server) {
            uvbus_free(server);
        }
        uvbus_config_free(config);
    }
    
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 14: Get transport type
 */
void test_get_transport_type(void) {
    TEST_START("Get transport type");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    
    /* Test TCP */
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_t* tcp_bus = uvbus_server_new(config);
    ASSERT_NOT_NULL(tcp_bus, "Failed to create TCP bus");
    ASSERT_EQ(uvbus_get_transport_type(tcp_bus), UVBUS_TRANSPORT_TCP, "Transport type mismatch");
    uvbus_free(tcp_bus);
    
    /* Test UDP */
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_UDP);
    uvbus_t* udp_bus = uvbus_server_new(config);
    ASSERT_NOT_NULL(udp_bus, "Failed to create UDP bus");
    ASSERT_EQ(uvbus_get_transport_type(udp_bus), UVBUS_TRANSPORT_UDP, "Transport type mismatch");
    uvbus_free(udp_bus);
    
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 15: Callback mechanism test
 */
void test_callback_mechanism(void) {
    TEST_START("Callback mechanism");
    
    test_context_t ctx = {0};
    
    /* Manually call callbacks to verify they work */
    test_recv_callback((const uint8_t*)"test", 4, NULL, &ctx);
    ASSERT_EQ(ctx.recv_count, 1, "Receive callback not called");
    
    test_connect_callback(UVBUS_OK, &ctx);
    ASSERT_EQ(ctx.connect_count, 1, "Connect callback not called");
    ASSERT_EQ(ctx.test_success, 1, "Connect status not set");
    
    test_close_callback(&ctx);
    ASSERT_EQ(ctx.close_count, 1, "Close callback not called");
    
    test_error_callback(UVBUS_ERROR, "test error", &ctx);
    ASSERT_EQ(ctx.error_count, 1, "Error callback not called");
    
    /* Cleanup */
    if (ctx.recv_data) {
        free(ctx.recv_data);
    }
    
    TEST_PASS();
}

/**
 * Test 16: Empty address handling
 */
void test_empty_address(void) {
    TEST_START("Empty address handling");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "");
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server with empty address");
    
    /* Should not crash */
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 17: Null address handling
 */
void test_null_address(void) {
    TEST_START("Null address handling");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, NULL);
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server with NULL address");
    
    /* Should not crash */
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
/**
 * Test 19: Server disconnect
 */
void test_server_disconnect(void) {
    TEST_START("Server disconnect");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    
    uvbus_t* server = uvbus_server_new(config);
    ASSERT_NOT_NULL(server, "Failed to create server");
    
    /* Server should ignore disconnect */
    uvbus_disconnect(server);
    
    uvbus_free(server);
    uvbus_config_free(config);
    /* Handles closed by uvbus_free()/uvbus_stop() are reclaimed by their
     * close callbacks, which only run while the loop is pumped. Without
     * this the structs they point at are still held at exit. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    TEST_PASS();
}

/**
 * Test 20: Multiple config objects
 */
void test_multiple_configs(void) {
    TEST_START("Multiple config objects");
    
    uvbus_config_t* configs[10];
    
    /* Create multiple configs */
    for (int i = 0; i < 10; i++) {
        configs[i] = uvbus_config_new();
        ASSERT_NOT_NULL(configs[i], "Failed to create config");
    }
    
    /* Free all configs */
    for (int i = 0; i < 10; i++) {
        uvbus_config_free(configs[i]);
    }
    
    TEST_PASS();
}

/* Main test runner */
int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("  UVBus Unit Tests\n");
    printf("========================================\n\n");
    
    /* Run all tests */
    test_config_create_free();
    test_config_set_get();
    test_config_callbacks();
    test_server_create_free();
    test_server_null_config();
    test_client_create_free();
    test_client_connect_no_server();
    test_server_listen_stop();
    test_invalid_params();
    test_client_send_unconnected();
    test_server_send_inactive();
    test_memory_leak();
    test_get_transport_type();
    test_callback_mechanism();
    test_empty_address();
    test_null_address();
    test_server_disconnect();
    test_multiple_configs();
    
    /* Print summary */
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Total: %d\n", tests_passed + tests_failed);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("========================================\n");
    
    return (tests_failed > 0) ? 1 : 0;
}
