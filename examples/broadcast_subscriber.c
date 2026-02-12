/**
 * UVRPC Broadcast Subscriber Example
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

void subscribe_callback(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    (void)ctx;
    
    printf("[%s] ", topic);
    if (data && size > 0) {
        fwrite(data, 1, size, stdout);
    }
    printf("\n");
}

int run_server_or_client(uv_loop_t* loop, int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "udp://127.0.0.1:5555";
    const char* topic = (argc > 2) ? argv[2] : "news";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Broadcast Subscriber\n");
    printf("Address: %s\n", address);
    printf("Topic: %s\n\n", topic);
    
        
    /* Create configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* Create subscriber */
    uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
    if (!subscriber) {
        fprintf(stderr, "Failed to create subscriber\n");
        uvrpc_config_free(config);
            return 1;
    }
    
    /* Connect to publisher */
    if (uvrpc_subscriber_connect(subscriber) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to publisher\n");
        uvrpc_subscriber_free(subscriber);
        uvrpc_config_free(config);
            return 1;
    }
    
    /* Subscribe to topic */
    if (uvrpc_subscriber_subscribe(subscriber, topic, subscribe_callback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to subscribe to topic\n");
        uvrpc_subscriber_disconnect(subscriber);
        uvrpc_subscriber_free(subscriber);
        uvrpc_config_free(config);
            return 1;
    }
    
    printf("Subscriber connected. Listening for messages...\n\n");
    
    /* Run event loop */
    while (g_running) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    printf("\nStopping subscriber...\n");
    
    /* Cleanup */
    uvrpc_subscriber_unsubscribe(subscriber, topic);
    uvrpc_subscriber_disconnect(subscriber);
    uvrpc_subscriber_free(subscriber);
    uvrpc_config_free(config);
    uv_loop_close(loop);
    
    printf("Subscriber stopped\n");
    return 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    int result = run_server_or_client(&loop, argc, argv);
    
    uv_loop_close(&loop);
    return result;
}
