/**
 * UVRPC Broadcast Publisher Example
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

void publish_callback(int status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("Message published successfully\n");
    }
}

int run_server_or_client(uv_loop_t* loop, int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "udp://0.0.0.0:5555";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Broadcast Publisher\n");
    printf("Address: %s\n\n", address);
    
        
    /* Create configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* Create publisher */
    uvrpc_publisher_t* publisher = uvrpc_publisher_create(config);
    if (!publisher) {
        fprintf(stderr, "Failed to create publisher\n");
        uvrpc_config_free(config);
            return 1;
    }
    
    /* Start publisher */
    if (uvrpc_publisher_start(publisher) != UVRPC_OK) {
        fprintf(stderr, "Failed to start publisher\n");
        uvrpc_publisher_free(publisher);
        uvrpc_config_free(config);
            return 1;
    }
    
    printf("Publisher started. Publishing messages...\n\n");
    
    /* Publish messages */
    int count = 0;
    while (g_running) {
        char message[256];
        snprintf(message, sizeof(message), "Hello from publisher #%d", count);
        
        uvrpc_publisher_publish(publisher, "news", 
                                 (const uint8_t*)message, strlen(message),
                                 publish_callback, NULL);
        
        count++;
        
        if (count % 10 == 0) {
            printf("Published %d messages\n", count);
        }
        
        uv_run(loop, UV_RUN_NOWAIT);
        usleep(100000);  /* 100ms */
    }
    
    printf("\nStopping publisher...\n");
    
    /* Cleanup */
    uvrpc_publisher_stop(publisher);
    uvrpc_publisher_free(publisher);
    uvrpc_config_free(config);
    uv_loop_close(loop);
    
    printf("Publisher stopped\n");
    return 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    int result = run_server_or_client(&loop, argc, argv);
    
    uv_loop_close(&loop);
    return result;
}
