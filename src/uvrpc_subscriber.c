/**
 * @file uvrpc_subscriber.c
 * @brief UVRPC Subscriber (Broadcast) Implementation
 * 
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include "../include/uvbus.h"
#include "uvrpc_broadcast.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Debug logging macro - compiles out in release builds */
#ifdef UVRPC_DEBUG
#define UVRPC_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define UVRPC_LOG(fmt, ...) ((void)0)
#endif

/* Error logging - always enabled */
#define UVRPC_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

/* Subscription entry */
typedef struct subscription_entry {
    char* topic;
    uvrpc_subscribe_callback_t callback;
    void* ctx;
    UT_hash_handle hh;
} subscription_entry_t;

/* Subscriber structure */
struct uvrpc_subscriber {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    uvbus_transport_type_t transport_type;
    int is_connected;
    subscription_entry_t* subscriptions;
};

/* Transport receive callback */
static void subscriber_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    uvrpc_subscriber_t* subscriber = (uvrpc_subscriber_t*)server_ctx;

    /* Decode broadcast message using FlatBuffers */
    char* topic = NULL;
    const uint8_t* msg_data = NULL;
    size_t msg_data_size = 0;

    int ret = uvrpc_broadcast_decode(data, size, &topic, &msg_data, &msg_data_size);
    if (ret != UVRPC_OK || !topic) {
        return;
    }

    /* Find subscription for this topic */
    subscription_entry_t* entry = NULL;
    HASH_FIND_STR(subscriber->subscriptions, topic, entry);

    if (entry && entry->callback) {
        /* Call callback with the data */
        uint8_t* data_copy = NULL;
        if (msg_data_size > 0) {
            data_copy = (uint8_t*)uvrpc_alloc(msg_data_size);
            if (data_copy) {
                memcpy(data_copy, msg_data, msg_data_size);
            }
        }

        entry->callback(topic, data_copy, msg_data_size, entry->ctx);

        if (data_copy) uvrpc_free(data_copy);
    }

    uvrpc_broadcast_free_decoded(topic);
}

/* Connect callback */
static void subscriber_connect_callback(int status, void* ctx) {
    uvrpc_subscriber_t* subscriber = (uvrpc_subscriber_t*)ctx;
    subscriber->is_connected = (status == 0);

    if (status != 0) {
        UVRPC_ERROR("Subscriber connection failed: %d", status);
        return;
    }
    
    /* Send registration message to notify publisher of our presence */
    /* This is needed for UDP broadcast to record client address */
    uint8_t reg_msg[] = {'U', 'V', 'R', 'P', 'C', '_', 'R', 'E', 'G'};  /* Registration magic */
    int rv = uvbus_send(subscriber->uvbus, reg_msg, sizeof(reg_msg));
    fprintf(stderr, "[Subscriber] Sent registration message: %d\n", rv);
}

/* Create subscriber */
uvrpc_subscriber_t* uvrpc_subscriber_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;

    uvrpc_subscriber_t* subscriber = uvrpc_calloc(1, sizeof(uvrpc_subscriber_t));
    if (!subscriber) return NULL;

    subscriber->loop = config->loop;
    subscriber->address = uvrpc_strdup(config->address);
    if (!subscriber->address) {
        uvrpc_free(subscriber);
        return NULL;
    }

    /* Convert uvrpc_transport_type to uvbus_transport_type_t */
    subscriber->transport_type = (uvbus_transport_type_t)config->transport;
    subscriber->is_connected = 0;
    subscriber->subscriptions = NULL;

    /* Create uvbus config */
    uvbus_config_t* bus_config = uvbus_config_new();
    if (!bus_config) {
        uvrpc_free(subscriber->address);
        uvrpc_free(subscriber);
        return NULL;
    }

    uvbus_config_set_loop(bus_config, subscriber->loop);
    uvbus_config_set_transport(bus_config, subscriber->transport_type);
    uvbus_config_set_address(bus_config, subscriber->address);
    uvbus_config_set_recv_callback(bus_config, subscriber_recv_callback, subscriber);
    uvbus_config_set_connect_callback(bus_config, subscriber_connect_callback, subscriber);

    /* Create uvbus client (broadcast subscriber) */
    subscriber->uvbus = uvbus_client_new(bus_config);
    uvbus_config_free(bus_config);

    if (!subscriber->uvbus) {
        uvrpc_free(subscriber->address);
        uvrpc_free(subscriber);
        return NULL;
    }

    return subscriber;
}

/* Connect to publisher */
int uvrpc_subscriber_connect(uvrpc_subscriber_t* subscriber) {
    if (!subscriber || !subscriber->uvbus) return UVRPC_ERROR_INVALID_PARAM;

    if (subscriber->is_connected) return UVRPC_OK;

    int rv = uvbus_connect(subscriber->uvbus);
    if (rv != 0) return UVRPC_ERROR_TRANSPORT;

    return UVRPC_OK;
}

/* Disconnect from publisher */
void uvrpc_subscriber_disconnect(uvrpc_subscriber_t* subscriber) {
    if (!subscriber) return;

    if (subscriber->uvbus) {
        uvbus_disconnect(subscriber->uvbus);
    }

    subscriber->is_connected = 0;
}

/* Free subscriber */
void uvrpc_subscriber_free(uvrpc_subscriber_t* subscriber) {
    if (!subscriber) return;

    uvrpc_subscriber_disconnect(subscriber);

    /* Free uvbus */
    if (subscriber->uvbus) {
        uvbus_free(subscriber->uvbus);
    }

    /* Free subscriptions */
    subscription_entry_t* entry, *tmp;
    HASH_ITER(hh, subscriber->subscriptions, entry, tmp) {
        HASH_DEL(subscriber->subscriptions, entry);
        uvrpc_free(entry->topic);
        uvrpc_free(entry);
    }

    uvrpc_free(subscriber->address);
    uvrpc_free(subscriber);
}

/* Subscribe to topic */
int uvrpc_subscriber_subscribe(uvrpc_subscriber_t* subscriber, const char* topic,
                                 uvrpc_subscribe_callback_t callback, void* ctx) {
    if (!subscriber || !topic || !callback) return UVRPC_ERROR_INVALID_PARAM;

    /* Check if already subscribed */
    subscription_entry_t* entry = NULL;
    HASH_FIND_STR(subscriber->subscriptions, topic, entry);
    if (entry) return UVRPC_ERROR; /* Already subscribed */

    /* Create new subscription */
    entry = uvrpc_calloc(1, sizeof(subscription_entry_t));
    if (!entry) return UVRPC_ERROR_NO_MEMORY;

    entry->topic = uvrpc_strdup(topic);
    if (!entry->topic) {
        uvrpc_free(entry);
        return UVRPC_ERROR_NO_MEMORY;
    }
    entry->callback = callback;
    entry->ctx = ctx;

    HASH_ADD_STR(subscriber->subscriptions, topic, entry);

    printf("Subscribed to topic: %s\n", topic);

    return UVRPC_OK;
}

/* Unsubscribe from topic */
int uvrpc_subscriber_unsubscribe(uvrpc_subscriber_t* subscriber, const char* topic) {
    if (!subscriber || !topic) return UVRPC_ERROR_INVALID_PARAM;

    /* Find subscription */
    subscription_entry_t* entry = NULL;
    HASH_FIND_STR(subscriber->subscriptions, topic, entry);
    if (!entry) return UVRPC_ERROR; /* Not subscribed */

    /* Remove subscription */
    HASH_DEL(subscriber->subscriptions, entry);
    uvrpc_free(entry->topic);
    uvrpc_free(entry);

    printf("Unsubscribed from topic: %s\n", topic);

    return UVRPC_OK;
}
