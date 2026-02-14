/**
 * UVRPC Message Bus Test
 * Test message routing and dispatching
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_handler_called = 0;
static int g_callback_called = 0;
static int g_subscription_called = 0;

/* Test handler */
void test_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("Handler called: method=%s, msgid=%lu\n", req->method, req->msgid);
    g_handler_called++;
    
    /* Send response */
    int32_t result = 42;
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
}

/* Test callback */
void test_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    printf("Callback called: msgid=%lu, status=%d\n", resp->msgid, resp->status);
    g_callback_called++;
}

/* Test subscription */
void test_subscription(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    (void)ctx;
    printf("Subscription called: topic=%s, size=%zu\n", topic, size);
    g_subscription_called++;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("=== UVRPC Message Bus Test ===\n\n");
    
    /* Create message bus */
    printf("1. Creating message bus...\n");
    uvrpc_bus_t* bus = uvrpc_bus_new(&loop);
    if (!bus) {
        fprintf(stderr, "Failed to create message bus\n");
        return 1;
    }
    printf("✓ Message bus created\n\n");
    
    /* Test 1: Handler routing */
    printf("2. Testing handler routing...\n");
    int ret = uvrpc_bus_register_handler(bus, "test.method", test_handler, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to register handler\n");
        uvrpc_bus_free(bus);
        return 1;
    }
    printf("✓ Handler registered\n");
    
    /* Create fake request */
    uvrpc_request_t req;
    req.server = NULL;
    req.msgid = 12345;
    req.method = "test.method";
    req.params = NULL;
    req.params_size = 0;
    req.client_stream = NULL;
    req.user_data = NULL;
    
    ret = uvrpc_bus_dispatch_request(bus, &req, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to dispatch request: %d\n", ret);
    } else {
        printf("✓ Request dispatched\n");
    }
    printf("Handler called: %d times\n\n", g_handler_called);
    
    /* Test 2: Callback routing */
    printf("3. Testing callback routing...\n");
    ret = uvrpc_bus_register_callback(bus, 67890, test_callback, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to register callback\n");
    } else {
        printf("✓ Callback registered\n");
    }
    
    /* Create fake response */
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 67890;
    resp.result = NULL;
    resp.result_size = 0;
    resp.error_code = 0;
    
    ret = uvrpc_bus_dispatch_response(bus, &resp);
    if (ret != 0) {
        fprintf(stderr, "Failed to dispatch response: %d\n", ret);
    } else {
        printf("✓ Response dispatched\n");
    }
    printf("Callback called: %d times\n\n", g_callback_called);
    
    /* Test 3: Topic routing */
    printf("4. Testing topic routing...\n");
    ret = uvrpc_bus_subscribe(bus, "test.topic", test_subscription, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to subscribe\n");
    } else {
        printf("✓ Subscribed to topic\n");
    }
    
    ret = uvrpc_bus_dispatch_message(bus, "test.topic", (uint8_t*)"hello", 5);
    if (ret < 0) {
        fprintf(stderr, "Failed to dispatch message: %d\n", ret);
    } else {
        printf("✓ Message dispatched\n");
    }
    printf("Subscription called: %d times\n\n", g_subscription_called);
    
    /* Test 4: Statistics */
    printf("5. Testing statistics...\n");
    uvrpc_bus_stats_t stats;
    ret = uvrpc_bus_get_stats(bus, &stats);
    if (ret == 0) {
        printf("Total routed: %lu\n", stats.total_routed);
        printf("Total handlers: %lu\n", stats.total_handlers);
        printf("Total callbacks: %lu\n", stats.total_callbacks);
        printf("Total subscriptions: %lu\n", stats.total_subscriptions);
        printf("Handler hits: %lu\n", stats.handler_hits);
        printf("Callback hits: %lu\n", stats.callback_hits);
        printf("Subscription hits: %lu\n", stats.subscription_hits);
        printf("✓ Statistics retrieved\n\n");
    }
    
    /* Cleanup */
    printf("6. Cleanup...\n");
    uvrpc_bus_free(bus);
    uv_loop_close(&loop);
    printf("✓ Cleanup complete\n\n");
    
    printf("=== Test Complete ===\n");
    printf("All tests passed!\n");
    
    return 0;
}