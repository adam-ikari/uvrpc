/**
 * UVRPC Message Bus
 * 
 * High-performance message routing and dispatching
 * Zero threads, Zero locks, Zero global variables
 * All routing managed by libuv event loop
 */

#ifndef UVRPC_BUS_H
#define UVRPC_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct uvrpc_bus uvrpc_bus_t;

/* Request structure (from uvrpc.h) */
typedef struct uvrpc_request uvrpc_request_t;

/* Response structure (from uvrpc.h) */
typedef struct uvrpc_response uvrpc_response_t;

/* Handler callback type */
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);

/* Response callback type */
typedef void (*uvrpc_callback_t)(uvrpc_response_t* resp, void* ctx);

/* Subscribe callback type */
typedef void (*uvrpc_subscribe_callback_t)(const char* topic, const uint8_t* data, size_t size, void* ctx);

/* Filter function type */
typedef int (*uvrpc_filter_fn)(const char* pattern, const char* key, void* ctx);

/* Message types */
typedef enum {
    UVRPC_MSG_REQUEST = 0,
    UVRPC_MSG_RESPONSE = 1,
    UVRPC_MSG_MESSAGE = 2
} uvrpc_msg_type_t;

/* Routing statistics */
typedef struct {
    uint64_t total_routed;
    uint64_t total_handlers;
    uint64_t total_callbacks;
    uint64_t total_subscriptions;
    uint64_t handler_hits;
    uint64_t callback_hits;
    uint64_t subscription_hits;
} uvrpc_bus_stats_t;

/* Create/free message bus */
uvrpc_bus_t* uvrpc_bus_new(uv_loop_t* loop);
void uvrpc_bus_free(uvrpc_bus_t* bus);

/* Handler routing (server side) */
int uvrpc_bus_register_handler(uvrpc_bus_t* bus, const char* method,
                                 uvrpc_handler_t handler, void* ctx);
int uvrpc_bus_unregister_handler(uvrpc_bus_t* bus, const char* method);
int uvrpc_bus_dispatch_request(uvrpc_bus_t* bus, uvrpc_request_t* req,
                                 void* response_sender);

/* Callback routing (client side) */
int uvrpc_bus_register_callback(uvrpc_bus_t* bus, uint64_t msgid,
                                 uvrpc_callback_t callback, void* ctx);
int uvrpc_bus_unregister_callback(uvrpc_bus_t* bus, uint64_t msgid);
int uvrpc_bus_dispatch_response(uvrpc_bus_t* bus, uvrpc_response_t* resp);

/* Topic routing (pub/sub) */
int uvrpc_bus_subscribe(uvrpc_bus_t* bus, const char* topic,
                         uvrpc_subscribe_callback_t callback, void* ctx);
int uvrpc_bus_unsubscribe(uvrpc_bus_t* bus, const char* topic, void* callback);
int uvrpc_bus_dispatch_message(uvrpc_bus_t* bus, const char* topic,
                                 const uint8_t* data, size_t size);

/* Advanced features */
int uvrpc_bus_set_filter(uvrpc_bus_t* bus, const char* pattern,
                           uvrpc_filter_fn filter, void* ctx);
int uvrpc_bus_get_stats(uvrpc_bus_t* bus, uvrpc_bus_stats_t* stats);
int uvrpc_bus_clear_stats(uvrpc_bus_t* bus);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_BUS_H */