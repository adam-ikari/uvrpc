/**
 * UVRPC Publisher (Broadcast)
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include "uvrpc_flatbuffers.h"
#include "uvrpc_transport.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Publisher structure */
struct uvrpc_publisher {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_t* transport;
    uvrpc_transport_type transport_type;
    int is_running;
};

/* Create publisher */
uvrpc_publisher_t* uvrpc_publisher_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    uvrpc_publisher_t* publisher = calloc(1, sizeof(uvrpc_publisher_t));
    if (!publisher) return NULL;
    
    publisher->loop = config->loop;
    publisher->address = strdup(config->address);
    publisher->transport_type = config->transport;
    publisher->is_running = 0;
    
    /* Create transport based on type */
    publisher->transport = uvrpc_transport_server_new(config->loop, config->transport);
    if (!publisher->transport) {
        free(publisher->address);
        free(publisher);
        return NULL;
    }
    
    return publisher;
}

/* Start publisher */
int uvrpc_publisher_start(uvrpc_publisher_t* publisher) {
    if (!publisher || !publisher->transport) return UVRPC_ERROR_INVALID_PARAM;
    
    if (publisher->is_running) return UVRPC_OK;
    
    /* Broadcast doesn't need a receive callback */
    int rv = uvrpc_transport_listen(publisher->transport, publisher->address, NULL, NULL);
    if (rv != UVRPC_OK) return rv;
    
    publisher->is_running = 1;
    printf("Publisher started on %s\n", publisher->address);
    
    return UVRPC_OK;
}

/* Stop publisher */
void uvrpc_publisher_stop(uvrpc_publisher_t* publisher) {
    if (!publisher) return;
    publisher->is_running = 0;
}

/* Free publisher */
void uvrpc_publisher_free(uvrpc_publisher_t* publisher) {
    if (!publisher) return;
    
    uvrpc_publisher_stop(publisher);
    
    /* Free transport */
    if (publisher->transport) {
        uvrpc_transport_free(publisher->transport);
    }
    
    free(publisher->address);
    free(publisher);
}

/* Publish message */
int uvrpc_publisher_publish(uvrpc_publisher_t* publisher, const char* topic,
                             const uint8_t* data, size_t size,
                             uvrpc_publish_callback_t callback, void* ctx) {
    if (!publisher || !topic) return UVRPC_ERROR_INVALID_PARAM;
    
    if (!publisher->is_running) {
        return UVRPC_ERROR;
    }
    
    /* Encode broadcast message: [topic_len, topic, data_len, data] */
    size_t topic_len = strlen(topic);
    size_t total_size = 4 + topic_len + 4 + size;
    
    uint8_t* msg = (uint8_t*)uvrpc_alloc(total_size);
    if (!msg) return UVRPC_ERROR_NO_MEMORY;
    
    uint8_t* p = msg;
    
    /* Topic length (4 bytes little-endian for consistency with FlatBuffers) */
    *(uint32_t*)p = (uint32_t)topic_len;
    p += 4;
    
    /* Topic */
    memcpy(p, topic, topic_len);
    p += topic_len;
    
    /* Data length (4 bytes little-endian) */
    *(uint32_t*)p = (uint32_t)size;
    p += 4;
    
    /* Data */
    if (data && size > 0) {
        memcpy(p, data, size);
    }
    
    /* Send message */
    if (publisher->transport) {
        uvrpc_transport_send(publisher->transport, msg, total_size);
    }
    
    free(msg);
    
    /* Call callback */
    if (callback) {
        callback(UVRPC_OK, ctx);
    }
    
    return UVRPC_OK;
}
