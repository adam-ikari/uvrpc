/**
 * UVRPC Broadcast Performance Benchmark
 * Tests publisher-subscriber performance with various transports
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#define DEFAULT_ADDRESS "udp://127.0.0.1:6000"
#define DEFAULT_DURATION_MS 1000
#define DEFAULT_MESSAGE_SIZE 100
#define DEFAULT_TOPIC "benchmark_topic"
#define MAX_MESSAGES 1000000

/* Global state */
static volatile sig_atomic_t g_running = 1;
static atomic_int g_messages_sent = 0;
static atomic_int g_messages_received = 0;
static atomic_int g_bytes_sent = 0;
static atomic_int g_bytes_received = 0;

/* Timing */
static struct timespec g_start_time;
static struct timespec g_end_time;

/* Signal handler */
void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Subscriber callback */
void subscriber_callback(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    (void)ctx;
    (void)topic;
    
    atomic_fetch_add(&g_messages_received, 1);
    atomic_fetch_add(&g_bytes_received, size);
}

/* Publisher callback */
void publisher_callback(int status, void* ctx) {
    (void)ctx;
    
    if (status == UVRPC_OK) {
        atomic_fetch_add(&g_messages_sent, 1);
    }
}

/* Run as publisher */
int run_publisher(uv_loop_t* loop, const char* address, const char* topic, 
                  int duration_ms, int message_size, int batch_size) {
    printf("=== Broadcast Publisher Benchmark ===\n");
    printf("Address: %s\n", address);
    printf("Topic: %s\n", topic);
    printf("Duration: %d ms\n", duration_ms);
    printf("Message size: %d bytes\n", message_size);
    printf("Batch size: %d\n", batch_size);
    printf("\n");
    
    /* Create configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* Set transport based on address */
    if (strncmp(address, "tcp://", 6) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    } else if (strncmp(address, "ipc://", 6) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    } else if (strncmp(address, "inproc://", 9) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    } else {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    }
    
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
    
    printf("Publisher started\n");
    
    /* Prepare message data */
    uint8_t* message_data = malloc(message_size);
    if (!message_data) {
        fprintf(stderr, "Failed to allocate message buffer\n");
        uvrpc_publisher_stop(publisher);
        uvrpc_publisher_free(publisher);
        uvrpc_config_free(config);
        return 1;
    }
    
    memset(message_data, 'X', message_size);
    
    /* Wait a bit for subscribers to connect */
    sleep(1);
    
    /* Start timing */
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
    
    /* Publish messages */
    int messages_published = 0;
    while (g_running && messages_published < MAX_MESSAGES) {
        /* Publish batch */
        for (int i = 0; i < batch_size && g_running; i++) {
            if (uvrpc_publisher_publish(publisher, topic, message_data, 
                                       message_size, publisher_callback, NULL) == UVRPC_OK) {
                atomic_fetch_add(&g_bytes_sent, message_size);
                messages_published++;
            }
        }
        
        /* Check if duration exceeded */
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long elapsed_ms = (current_time.tv_sec - g_start_time.tv_sec) * 1000 +
                         (current_time.tv_nsec - g_start_time.tv_nsec) / 1000000;
        
        if (elapsed_ms >= duration_ms) {
            break;
        }
        
        /* Small delay to avoid overwhelming the system */
        usleep(1000);
    }
    
    /* End timing */
    clock_gettime(CLOCK_MONOTONIC, &g_end_time);
    
    /* Calculate duration */
    long duration_ns = (g_end_time.tv_sec - g_start_time.tv_sec) * 1000000000L +
                      (g_end_time.tv_nsec - g_start_time.tv_nsec);
    double duration_s = duration_ns / 1000000000.0;
    
    printf("\n=== Publisher Results ===\n");
    printf("Duration: %.3f seconds\n", duration_s);
    printf("Messages published: %d\n", messages_published);
    printf("Bytes sent: %d\n", atomic_load(&g_bytes_sent));
    printf("Throughput: %.2f msg/s\n", messages_published / duration_s);
    printf("Bandwidth: %.2f KB/s\n", (atomic_load(&g_bytes_sent) / 1024.0) / duration_s);
    
    /* Cleanup */
    free(message_data);
    uvrpc_publisher_stop(publisher);
    uvrpc_publisher_free(publisher);
    uvrpc_config_free(config);
    
    return 0;
}

/* Run as subscriber */
int run_subscriber(uv_loop_t* loop, const char* address, const char* topic, 
                   int duration_ms) {
    printf("=== Broadcast Subscriber Benchmark ===\n");
    printf("Address: %s\n", address);
    printf("Topic: %s\n", topic);
    printf("Duration: %d ms\n", duration_ms);
    printf("\n");
    
    /* Create configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* Set transport based on address */
    if (strncmp(address, "tcp://", 6) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    } else if (strncmp(address, "ipc://", 6) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    } else if (strncmp(address, "inproc://", 9) == 0) {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    } else {
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    }
    
    /* Create subscriber */
    uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
    if (!subscriber) {
        fprintf(stderr, "Failed to create subscriber\n");
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Subscribe to topic */
    if (uvrpc_subscriber_subscribe(subscriber, topic, subscriber_callback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to subscribe to topic\n");
        uvrpc_subscriber_free(subscriber);
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
    
    printf("Subscriber connected and waiting for messages...\n");
    
    /* Wait for duration */
    sleep(duration_ms / 1000);
    
    /* Calculate results */
    printf("\n=== Subscriber Results ===\n");
    printf("Messages received: %d\n", atomic_load(&g_messages_received));
    printf("Bytes received: %d\n", atomic_load(&g_bytes_received));
    printf("Average message size: %.2f bytes\n", 
           atomic_load(&g_messages_received) > 0 ? 
           (double)atomic_load(&g_bytes_received) / atomic_load(&g_messages_received) : 0);
    
    /* Cleanup */
    uvrpc_subscriber_unsubscribe(subscriber, topic);
    uvrpc_subscriber_disconnect(subscriber);
    uvrpc_subscriber_free(subscriber);
    uvrpc_config_free(config);
    
    return 0;
}

void print_usage(const char* prog_name) {
    printf("UVRPC Broadcast Performance Benchmark\n\n");
    printf("Usage: %s [mode] [options]\n\n", prog_name);
    printf("Modes:\n");
    printf("  publisher    Run as publisher (sends messages)\n");
    printf("  subscriber   Run as subscriber (receives messages)\n\n");
    printf("Options:\n");
    printf("  -a <address>  Address (default: %s)\n", DEFAULT_ADDRESS);
    printf("  -t <topic>    Topic (default: %s)\n", DEFAULT_TOPIC);
    printf("  -d <duration> Duration in ms (default: %d)\n", DEFAULT_DURATION_MS);
    printf("  -s <size>     Message size in bytes (default: %d)\n", DEFAULT_MESSAGE_SIZE);
    printf("  -b <batch>    Batch size for publisher (default: 10)\n");
    printf("  -h            Show this help\n\n");
    printf("Examples:\n");
    printf("  # Start UDP publisher\n");
    printf("  %s publisher -a udp://127.0.0.1:6000 -d 5000\n\n", prog_name);
    printf("  # Start UDP subscriber\n");
    printf("  %s subscriber -a udp://127.0.0.1:6000 -d 5000\n\n", prog_name);
    printf("  # Start TCP publisher\n");
    printf("  %s publisher -a tcp://127.0.0.1:6000 -d 5000\n\n", prog_name);
    printf("  # Start IPC publisher\n");
    printf("  %s publisher -a ipc:///tmp/benchmark.sock -d 5000\n\n", prog_name);
    printf("  # Start INPROC publisher\n");
    printf("  %s publisher -a inproc://benchmark -d 5000\n\n", prog_name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    const char* mode = argv[1];
    const char* address = DEFAULT_ADDRESS;
    const char* topic = DEFAULT_TOPIC;
    int duration_ms = DEFAULT_DURATION_MS;
    int message_size = DEFAULT_MESSAGE_SIZE;
    int batch_size = 10;
    
    /* Parse arguments */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            address = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            topic = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            duration_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            message_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            batch_size = atoi(argv[++i]);
        }
    }
    
    uv_loop_t* loop = uv_default_loop();
    int ret;
    
    if (strcmp(mode, "publisher") == 0) {
        ret = run_publisher(loop, address, topic, duration_ms, message_size, batch_size);
    } else if (strcmp(mode, "subscriber") == 0) {
        ret = run_subscriber(loop, address, topic, duration_ms);
    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        print_usage(argv[0]);
        ret = 1;
    }
    
    /* Run event loop to process cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    return ret;
}
