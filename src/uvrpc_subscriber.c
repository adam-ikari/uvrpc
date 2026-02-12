/**
 * UVRPC Subscriber (Broadcast)
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "uvrpc_transport.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

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
    uvrpc_transport_t* transport;
    uvrpc_transport_type transport_type;
    int is_connected;
    subscription_entry_t* subscriptions;
};

/* Transport receive callback */
static void subscriber_recv_callback(uint8_t* data, size_t size, void* ctx) {
    uvrpc_subscriber_t* subscriber = (uvrpc_subscriber_t*)ctx;
    
    if (size < 8) {
        free(data);
        return;
    }
    
    uint8_t* p = data;
    
    /* Parse topic length */
    uint32_t topic_len = ntohl(*(uint32_t*)p);
    p += 4;
    
    if (size < 8 + topic_len) {
        free(data);
        return;
    }
    
    /* Extract topic */
    char* topic = (char*)malloc(topic_len + 1);
    if (!topic) {
        free(data);
        return;
    }
    memcpy(topic, p, topic_len);
    topic[topic_len] = '\0';
    p += topic_len;
    
    /* Parse data length */
    uint32_t data_size = ntohl(*(uint32_t*)p);
    p += 4;
    
    if (size < 8 + topic_len + data_size) {
        free(topic);
        free(data);
        return;
    }
    
    /* Find subscription for this topic */
    subscription_entry_t* entry = NULL;
    HASH_FIND_STR(subscriber->subscriptions, topic, entry);
    
    if (entry && entry->callback) {
        /* Call callback with the data */
        uint8_t* data_copy = NULL;
        if (data_size > 0) {
            data_copy = (uint8_t*)malloc(data_size);
            if (data_copy) {
                memcpy(data_copy, p, data_size);
            }
        }
        
        entry->callback(topic, data_copy, data_size, entry->ctx);
        
        if (data_copy) free(data_copy);
    }
    
    free(topic);
    free(data);
}

/* Create subscriber */
uvrpc_subscriber_t* uvrpc_subscriber_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    uvrpc_subscriber_t* subscriber = calloc(1, sizeof(uvrpc_subscriber_t));
    if (!subscriber) return NULL;
    
    subscriber->loop = config->loop;
    subscriber->address = strdup(config->address);
    subscriber->transport_type = config->transport;
    subscriber->is_connected = 0;
    subscriber->subscriptions = NULL;
    
    /* Create transport */
    subscriber->transport = uvrpc_transport_client_new(config->loop, config->transport);
    if (!subscriber->transport) {
        free(subscriber->address);
        free(subscriber);
        return NULL;
    }
    
    return subscriber;
}

/* Connect to publisher */
int uvrpc_subscriber_connect(uvrpc_subscriber_t* subscriber) {
    if (!subscriber || !subscriber->transport) return UVRPC_ERROR_INVALID_PARAM;
    
    if (subscriber->is_connected) return UVRPC_OK;
    
    return uvrpc_transport_connect(subscriber->transport, subscriber->address,
                                    NULL, subscriber_recv_callback, subscriber);
}

/* Disconnect from publisher */
void uvrpc_subscriber_disconnect(uvrpc_subscriber_t* subscriber) {
    if (!subscriber) return;
    
    if (subscriber->transport) {
        uvrpc_transport_disconnect(subscriber->transport);
    }
    
    subscriber->is_connected = 0;
}

/* Free subscriber */
void uvrpc_subscriber_free(uvrpc_subscriber_t* subscriber) {
    if (!subscriber) return;
    
    uvrpc_subscriber_disconnect(subscriber);
    
    /* Free transport */
    if (subscriber->transport) {
        uvrpc_transport_free(subscriber->transport);
    }
    
    /* Free subscriptions */
    subscription_entry_t* entry, *tmp;
    HASH_ITER(hh, subscriber->subscriptions, entry, tmp) {
        HASH_DEL(subscriber->subscriptions, entry);
        free(entry->topic);
        free(entry);
    }
    
    free(subscriber->address);
    free(subscriber);
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
    entry = calloc(1, sizeof(subscription_entry_t));
    if (!entry) return UVRPC_ERROR_NO_MEMORY;
    
    entry->topic = strdup(topic);
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
    free(entry->topic);
    free(entry);
    
    printf("Unsubscribed from topic: %s\n", topic);
    
    return UVRPC_OK;
}
