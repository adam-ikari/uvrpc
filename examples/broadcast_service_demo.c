/**
 * UVRPC Broadcast Service Demo
 * Demonstrates DSL-driven broadcast API generation
 *
 * This example shows how to use the auto-generated broadcast service APIs
 * from rpc_broadcast.fbs schema.
 */

#include "../include/uvrpc.h"
#include "../src/uvrpc_broadcast_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_running = 1;

/* Publisher callback */
void on_publish_complete(int status, void* ctx) {
    printf("[Publisher] Message published: %s\n", status == UVRPC_OK ? "OK" : "FAILED");
}

/* News subscriber callback */
void on_news_received(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    printf("[Subscriber] Received news on topic: %s\n", topic);

    /* Decode using auto-generated API */
    char* title = NULL;
    char* content = NULL;
    char* author = NULL;
    int64_t timestamp = 0;

    if (uvrpc_broadcast_service_decode_publish_news(data, size, &title, &content, &timestamp, &author) == UVRPC_OK) {
        printf("  Title: %s\n", title);
        printf("  Content: %s\n", content);
        printf("  Author: %s\n", author);
        printf("  Timestamp: %ld\n", (long)timestamp);

        uvrpc_free(title);
        uvrpc_free(content);
        uvrpc_free(author);
    }
}

/* Weather subscriber callback */
void on_weather_received(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    printf("[Subscriber] Received weather update on topic: %s\n", topic);

    /* Decode using auto-generated API */
    char* location = NULL;
    char* condition = NULL;
    float temperature = 0.0f;
    int32_t humidity = 0;
    int64_t timestamp = 0;

    if (uvrpc_broadcast_service_decode_update_weather(data, size, &location, &temperature, &humidity, &condition, &timestamp) == UVRPC_OK) {
        printf("  Location: %s\n", location);
        printf("  Temperature: %.1f°C\n", temperature);
        printf("  Humidity: %d%%\n", humidity);
        printf("  Condition: %s\n", condition);
        printf("  Timestamp: %ld\n", (long)timestamp);

        uvrpc_free(location);
        uvrpc_free(condition);
    }
}

/* Event subscriber callback */
void on_event_received(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    printf("[Subscriber] Received event notification on topic: %s\n", topic);

    /* Decode using auto-generated API */
    char* event_type = NULL;
    const uint8_t* event_data = NULL;
    size_t event_data_size = 0;
    int64_t timestamp = 0;
    int32_t priority = 0;

    if (uvrpc_broadcast_service_decode_notify_event(data, size, &event_type, &event_data, &event_data_size, &timestamp, &priority) == UVRPC_OK) {
        printf("  Event Type: %s\n", event_type);
        printf("  Priority: %d\n", priority);
        printf("  Data Size: %zu bytes\n", event_data_size);
        printf("  Timestamp: %ld\n", (long)timestamp);

        uvrpc_free(event_type);
    }
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    const char* mode = (argc > 1) ? argv[1] : "publisher";
    const char* address = (argc > 2) ? argv[2] : "udp://0.0.0.0:5555";

    printf("=== UVRPC Broadcast Service Demo (DSL-Generated) ===\n");
    printf("Mode: %s\n", mode);
    printf("Address: %s\n\n", address);

    if (strcmp(mode, "publisher") == 0) {
        /* Publisher mode */
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);

        uvrpc_publisher_t* publisher = uvrpc_publisher_create(config);
        uvrpc_config_free(config);

        if (!publisher) {
            fprintf(stderr, "Failed to create publisher\n");
            return 1;
        }

        if (uvrpc_publisher_start(publisher) != UVRPC_OK) {
            fprintf(stderr, "Failed to start publisher\n");
            uvrpc_publisher_free(publisher);
            return 1;
        }

        printf("[Publisher] Started successfully\n\n");

        /* Publish news using auto-generated API */
        printf("[Publisher] Publishing news...\n");
        uvrpc_broadcast_service_publish_news(
            publisher,
            "Breaking News",
            "UVRPC now supports DSL-driven broadcast service generation!",
            time(NULL) * 1000,
            "UVRPC Team",
            on_publish_complete,
            NULL
        );
        printf("\n");

        /* Publish weather update using auto-generated API */
        printf("[Publisher] Publishing weather update...\n");
        uvrpc_broadcast_service_update_weather(
            publisher,
            "Beijing",
            25.5f,
            65,
            "Sunny",
            time(NULL) * 1000,
            on_publish_complete,
            NULL
        );
        printf("\n");

        /* Publish event notification using auto-generated API */
        printf("[Publisher] Publishing event notification...\n");
        const char* event_msg = "System alert: High CPU usage detected";
        uvrpc_broadcast_service_notify_event(
            publisher,
            "system.alert",
            (const uint8_t*)event_msg,
            strlen(event_msg),
            time(NULL) * 1000,
            1,  /* High priority */
            on_publish_complete,
            NULL
        );
        printf("\n");

        /* Run event loop for a short time */
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_ONCE);
            usleep(100000);  /* 100ms */
        }

        uvrpc_publisher_stop(publisher);
        uvrpc_publisher_free(publisher);

    } else if (strcmp(mode, "subscriber") == 0) {
        /* Subscriber mode */
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);

        uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
        uvrpc_config_free(config);

        if (!subscriber) {
            fprintf(stderr, "Failed to create subscriber\n");
            return 1;
        }

        /* Subscribe to all topics using auto-generated service names */
        uvrpc_subscriber_subscribe(subscriber, "PublishNews", on_news_received, NULL);
        uvrpc_subscriber_subscribe(subscriber, "UpdateWeather", on_weather_received, NULL);
        uvrpc_subscriber_subscribe(subscriber, "NotifyEvent", on_event_received, NULL);

        if (uvrpc_subscriber_connect(subscriber) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect subscriber\n");
            uvrpc_subscriber_free(subscriber);
            return 1;
        }

        printf("[Subscriber] Connected and listening for messages...\n\n");

        /* Run event loop */
        for (int i = 0; i < 30; i++) {
            uv_run(&loop, UV_RUN_ONCE);
            usleep(100000);  /* 100ms */
        }

        uvrpc_subscriber_disconnect(subscriber);
        uvrpc_subscriber_free(subscriber);

    } else {
        fprintf(stderr, "Usage: %s [publisher|subscriber] [address]\n", argv[0]);
        return 1;
    }

    uv_loop_close(&loop);

    printf("\n=== Demo Complete ===\n");
    printf("\nKey Benefits of DSL-Driven Broadcast:\n");
    printf("  1. Type Safety: Auto-generated APIs from FlatBuffers schema\n");
    printf("  2. No Manual Encoding: Topic and data encoding handled automatically\n");
    printf("  3. Easy Extension: Add methods to rpc_broadcast.fbs -> regenerate\n");
    printf("  4. Consistency: All services follow the same pattern\n");

    return 0;
}