/**
 * UVRPC Publisher (Broadcast)
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include "../include/uvbus.h"
#include "uvrpc_flatbuffers.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Publisher structure */
struct uvrpc_publisher {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    uvbus_transport_type_t transport_type;
    int is_running;
};

/* Create publisher */
uvrpc_publisher_t* uvrpc_publisher_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;

    uvrpc_publisher_t* publisher = uvrpc_calloc(1, sizeof(uvrpc_publisher_t));
    if (!publisher) return NULL;

    publisher->loop = config->loop;
    publisher->address = uvrpc_strdup(config->address);
    if (!publisher->address) {
        uvrpc_free(publisher);
        return NULL;
    }

    /* Convert uvrpc_transport_type to uvbus_transport_type_t */
    publisher->transport_type = (uvbus_transport_type_t)config->transport;
    publisher->is_running = 0;

    /* Create uvbus config */
    uvbus_config_t* bus_config = uvbus_config_new();
    if (!bus_config) {
        uvrpc_free(publisher->address);
        uvrpc_free(publisher);
        return NULL;
    }

    uvbus_config_set_loop(bus_config, publisher->loop);
    uvbus_config_set_transport(bus_config, publisher->transport_type);
    uvbus_config_set_address(bus_config, publisher->address);

    /* Create uvbus server (broadcast publisher) */
    publisher->uvbus = uvbus_server_new(bus_config);
    uvbus_config_free(bus_config);

    if (!publisher->uvbus) {
        uvrpc_free(publisher->address);
        uvrpc_free(publisher);
        return NULL;
    }

    return publisher;
}

/* Start publisher */
int uvrpc_publisher_start(uvrpc_publisher_t* publisher) {
    if (!publisher || !publisher->uvbus) return UVRPC_ERROR_INVALID_PARAM;

    if (publisher->is_running) return UVRPC_OK;

    /* Broadcast doesn't need a receive callback */
    int rv = uvbus_listen(publisher->uvbus);
    if (rv != UVBUS_OK) return UVRPC_ERROR_TRANSPORT;

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

    /* Free uvbus */
    if (publisher->uvbus) {
        uvbus_free(publisher->uvbus);
    }

    uvrpc_free(publisher->address);
    uvrpc_free(publisher);
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
    if (publisher->uvbus) {
        uvbus_client_send(publisher->uvbus, msg, total_size);
    }

    uvrpc_free(msg);

    /* Call callback */
    if (callback) {
        callback(UVRPC_OK, ctx);
    }

    return UVRPC_OK;
}
