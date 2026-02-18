/**
 * UVRPC Broadcast End-to-End Integration Test
 * Tests publish/subscribe mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include "../../include/uvrpc.h"

#define TEST_BROADCAST_ADDR "inproc://uvrpc_broadcast_test"
#define TEST_TOPIC "test_topic"
#define TIMEOUT_MS 5000

static int subscriber_received = 0;
static int publisher_status = 0;
static int test_complete = 0;

/* Subscriber callback for messages */
static void subscriber_callback(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    (void)ctx;
    subscriber_received++;
    
    printf("Subscriber received message on topic: %s\n", topic);
    
    /* Verify topic */
    assert(strcmp(topic, TEST_TOPIC) == 0);
    
    /* Verify data */
    if (data && size == 10) {
        assert(data[0] == 'B');
        assert(data[1] == 'R');
        assert(data[2] == 'O');
        assert(data[3] == 'A');
        assert(data[4] == 'D');
        assert(data[5] == 'C');
        assert(data[6] == 'A');
        assert(data[7] == 'S');
        assert(data[8] == 'T');
        assert(data[9] == '!');
    }
}

/* Publisher callback for publish status */
static void publisher_callback(int status, void* ctx) {
    (void)ctx;
    publisher_status = status;
    printf("Publisher publish status: %d\n", status);
}

/* Timeout timer callback */
static void timeout_callback(uv_timer_t* handle) {
    int* should_stop = (int*)handle->data;
    *should_stop = 1;
    printf("Timeout reached\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    uv_loop_t* loop = uv_default_loop();
    int should_stop = 0;
    
    printf("=== UVRPC Broadcast End-to-End Test ===\n");
    
    /* Create publisher configuration */
    uvrpc_config_t* pub_config = uvrpc_config_new();
    pub_config = uvrpc_config_set_loop(pub_config, loop);
    pub_config = uvrpc_config_set_address(pub_config, TEST_BROADCAST_ADDR);
    pub_config = uvrpc_config_set_transport(pub_config, UVRPC_TRANSPORT_INPROC);
    pub_config = uvrpc_config_set_comm_type(pub_config, UVRPC_COMM_BROADCAST);
    
    /* Create publisher */
    uvrpc_publisher_t* publisher = uvrpc_publisher_create(pub_config);
    assert(publisher != NULL);
    
    /* Start publisher */
    int rv = uvrpc_publisher_start(publisher);
    assert(rv == 0);
    printf("Publisher started on %s\n", TEST_BROADCAST_ADDR);
    
    /* Give publisher time to start */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Create subscriber configuration */
    uvrpc_config_t* sub_config = uvrpc_config_new();
    sub_config = uvrpc_config_set_loop(sub_config, loop);
    sub_config = uvrpc_config_set_address(sub_config, TEST_BROADCAST_ADDR);
    sub_config = uvrpc_config_set_transport(sub_config, UVRPC_TRANSPORT_INPROC);
    sub_config = uvrpc_config_set_comm_type(sub_config, UVRPC_COMM_BROADCAST);
    
    /* Create subscriber */
    uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(sub_config);
    assert(subscriber != NULL);
    
    /* Subscribe to topic */
    rv = uvrpc_subscriber_subscribe(subscriber, TEST_TOPIC, subscriber_callback, NULL);
    assert(rv == 0);
    printf("Subscriber subscribed to topic: %s\n", TEST_TOPIC);
    
    /* Connect subscriber */
    rv = uvrpc_subscriber_connect(subscriber);
    assert(rv == 0);
    printf("Subscriber connecting...\n");
    
    /* Setup timeout timer */
    uv_timer_t timeout_timer;
    uv_timer_init(loop, &timeout_timer);
    timeout_timer.data = &should_stop;
    
    /* Run event loop to establish connection */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Connection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        return 1;
    }
    
    printf("Publishing message...\n");
    
    /* Publisher publishes message */
    uint8_t test_data[] = {'B', 'R', 'O', 'A', 'D', 'C', 'A', 'S', 'T', '!'};
    rv = uvrpc_publisher_publish(publisher, TEST_TOPIC, test_data, sizeof(test_data), publisher_callback, NULL);
    assert(rv == 0);
    printf("Publisher published message to topic: %s\n", TEST_TOPIC);
    
    /* Run event loop to receive message */
    should_stop = 0;
    uv_timer_again(&timeout_timer);
    
    for (int i = 0; i < 200 && !should_stop && subscriber_received == 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    if (subscriber_received == 0) {
        printf("ERROR: Subscriber did not receive message\n");
    }
    
    printf("\n=== Test Results ===\n");
    printf("Subscriber received messages: %d\n", subscriber_received);
    printf("Publisher status: %d\n", publisher_status);
    
    /* Cleanup */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_subscriber_disconnect(subscriber);
    uvrpc_subscriber_free(subscriber);
    uvrpc_publisher_stop(publisher);
    uvrpc_publisher_free(publisher);
    uvrpc_config_free(sub_config);
    uvrpc_config_free(pub_config);
    
    /* Run event loop to process cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    /* Verify test results */
    if (subscriber_received == 1 && publisher_status == 0) {
        printf("\n=== Broadcast End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Broadcast End-to-End Test FAILED ===\n");
        return 1;
    }
}
