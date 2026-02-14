/**
 * UVRPC Message Bus Implementation
 * 
 * High-performance message routing using uthash
 * Zero threads, Zero locks, Zero global variables
 */

#include "../include/uvrpc_bus.h"
#include "../include/uvrpc_allocator.h"
#include "../include/uvrpc.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>

/* Handler entry for server-side routing */
typedef struct handler_entry {
    char* name;
    uvrpc_handler_t handler;
    void* ctx;
    UT_hash_handle hh;
} handler_entry_t;

/* Callback entry for client-side routing */
typedef struct callback_entry {
    uint64_t msgid;
    uvrpc_callback_t callback;
    void* ctx;
    UT_hash_handle hh;
} callback_entry_t;

/* Subscription entry for pub/sub routing */
typedef struct subscription_entry {
    char* topic;
    uvrpc_subscribe_callback_t callback;
    void* ctx;
    UT_hash_handle hh;
} subscription_entry_t;

/* Filter entry for topic filtering */
typedef struct filter_entry {
    char* pattern;
    uvrpc_filter_fn filter;
    void* ctx;
    UT_hash_handle hh;
} filter_entry_t;

/* Message bus context */
struct uvrpc_bus {
    uv_loop_t* loop;
    
    /* Handler routing (server) */
    handler_entry_t* handlers;
    
    /* Callback routing (client) */
    callback_entry_t* callbacks;
    
    /* Topic routing (pub/sub) */
    subscription_entry_t* subscriptions;
    
    /* Filters */
    filter_entry_t* filters;
    
    /* Statistics */
    uvrpc_bus_stats_t stats;
};

/* Create message bus */
uvrpc_bus_t* uvrpc_bus_new(uv_loop_t* loop) {
    if (!loop) {
        return NULL;
    }
    
    uvrpc_bus_t* bus = (uvrpc_bus_t*)uvrpc_calloc(1, sizeof(uvrpc_bus_t));
    if (!bus) {
        return NULL;
    }
    
    bus->loop = loop;
    bus->handlers = NULL;
    bus->callbacks = NULL;
    bus->subscriptions = NULL;
    bus->filters = NULL;
    memset(&bus->stats, 0, sizeof(uvrpc_bus_stats_t));
    
    return bus;
}

/* Free message bus */
void uvrpc_bus_free(uvrpc_bus_t* bus) {
    if (!bus) {
        return;
    }
    
    /* Free handlers */
    handler_entry_t* handler, *handler_tmp;
    HASH_ITER(hh, bus->handlers, handler, handler_tmp) {
        HASH_DEL(bus->handlers, handler);
        if (handler->name) {
            uvrpc_free(handler->name);
        }
        uvrpc_free(handler);
    }
    
    /* Free callbacks */
    callback_entry_t* callback, *callback_tmp;
    HASH_ITER(hh, bus->callbacks, callback, callback_tmp) {
        HASH_DEL(bus->callbacks, callback);
        uvrpc_free(callback);
    }
    
    /* Free subscriptions */
    subscription_entry_t* sub, *sub_tmp;
    HASH_ITER(hh, bus->subscriptions, sub, sub_tmp) {
        HASH_DEL(bus->subscriptions, sub);
        if (sub->topic) {
            uvrpc_free(sub->topic);
        }
        uvrpc_free(sub);
    }
    
    /* Free filters */
    filter_entry_t* filter, *filter_tmp;
    HASH_ITER(hh, bus->filters, filter, filter_tmp) {
        HASH_DEL(bus->filters, filter);
        if (filter->pattern) {
            uvrpc_free(filter->pattern);
        }
        uvrpc_free(filter);
    }
    
    uvrpc_free(bus);
}

/* Register handler (server side) */
int uvrpc_bus_register_handler(uvrpc_bus_t* bus, const char* method,
                                 uvrpc_handler_t handler, void* ctx) {
    if (!bus || !method || !handler) {
        return -1;
    }
    
    /* Check if handler already exists */
    handler_entry_t* existing;
    HASH_FIND_STR(bus->handlers, method, existing);
    if (existing) {
        /* Update existing handler */
        existing->handler = handler;
        existing->ctx = ctx;
        return 0;
    }
    
    /* Create new handler entry */
    handler_entry_t* entry = (handler_entry_t*)uvrpc_calloc(1, sizeof(handler_entry_t));
    if (!entry) {
        return -1;
    }
    
    entry->name = uvrpc_strdup(method);
    if (!entry->name) {
        uvrpc_free(entry);
        return -1;
    }
    
    entry->handler = handler;
    entry->ctx = ctx;
    
    HASH_ADD_STR(bus->handlers, entry->name, entry);
    bus->stats.total_handlers++;
    
    return 0;
}

/* Unregister handler (server side) */
int uvrpc_bus_unregister_handler(uvrpc_bus_t* bus, const char* method) {
    if (!bus || !method) {
        return -1;
    }
    
    handler_entry_t* entry;
    HASH_FIND_STR(bus->handlers, method, entry);
    if (!entry) {
        return -1;
    }
    
    HASH_DEL(bus->handlers, entry);
    if (entry->name) {
        uvrpc_free(entry->name);
    }
    uvrpc_free(entry);
    
    return 0;
}

/* Dispatch request to handler (server side) */
int uvrpc_bus_dispatch_request(uvrpc_bus_t* bus, uvrpc_request_t* req,
                                 void* response_sender) {
    if (!bus || !req || !req->method) {
        return -1;
    }
    
    bus->stats.total_routed++;
    
    /* Find handler */
    handler_entry_t* entry;
    HASH_FIND_STR(bus->handlers, req->method, entry);
    
    if (!entry) {
        /* Handler not found */
        return -2;
    }
    
    bus->stats.handler_hits++;
    
    /* Call handler */
    entry->handler(req, entry->ctx);
    
    return 0;
}

/* Register callback (client side) */
int uvrpc_bus_register_callback(uvrpc_bus_t* bus, uint64_t msgid,
                                 uvrpc_callback_t callback, void* ctx) {
    if (!bus || !callback) {
        return -1;
    }
    
    /* Create new callback entry */
    callback_entry_t* entry = (callback_entry_t*)uvrpc_calloc(1, sizeof(callback_entry_t));
    if (!entry) {
        return -1;
    }
    
    entry->msgid = msgid;
    entry->callback = callback;
    entry->ctx = ctx;
    
    HASH_ADD_INT(bus->callbacks, msgid, entry);
    bus->stats.total_callbacks++;
    
    return 0;
}

/* Unregister callback (client side) */
int uvrpc_bus_unregister_callback(uvrpc_bus_t* bus, uint64_t msgid) {
    if (!bus) {
        return -1;
    }
    
    callback_entry_t* entry;
    HASH_FIND_INT(bus->callbacks, &msgid, entry);
    if (!entry) {
        return -1;
    }
    
    HASH_DEL(bus->callbacks, entry);
    uvrpc_free(entry);
    
    return 0;
}

/* Dispatch response to callback (client side) */
int uvrpc_bus_dispatch_response(uvrpc_bus_t* bus, uvrpc_response_t* resp) {
    if (!bus || !resp) {
        return -1;
    }
    
    bus->stats.total_routed++;
    
    /* Find callback */
    callback_entry_t* entry;
    HASH_FIND_INT(bus->callbacks, &resp->msgid, entry);
    
    if (!entry) {
        /* Callback not found */
        return -2;
    }
    
    bus->stats.callback_hits++;
    
    /* Call callback */
    entry->callback(resp, entry->ctx);
    
    /* Remove callback after delivery */
    HASH_DEL(bus->callbacks, entry);
    uvrpc_free(entry);
    
    return 0;
}

/* Subscribe to topic (pub/sub) */
int uvrpc_bus_subscribe(uvrpc_bus_t* bus, const char* topic,
                         uvrpc_subscribe_callback_t callback, void* ctx) {
    if (!bus || !topic || !callback) {
        return -1;
    }
    
    /* Create new subscription entry */
    subscription_entry_t* entry = (subscription_entry_t*)uvrpc_calloc(1, sizeof(subscription_entry_t));
    if (!entry) {
        return -1;
    }
    
    entry->topic = uvrpc_strdup(topic);
    if (!entry->topic) {
        uvrpc_free(entry);
        return -1;
    }
    
    entry->callback = callback;
    entry->ctx = ctx;
    
    HASH_ADD_STR(bus->subscriptions, entry->topic, entry);
    bus->stats.total_subscriptions++;
    
    return 0;
}

/* Unsubscribe from topic (pub/sub) */
int uvrpc_bus_unsubscribe(uvrpc_bus_t* bus, const char* topic, void* callback) {
    if (!bus || !topic) {
        return -1;
    }
    
    subscription_entry_t* entry;
    HASH_FIND_STR(bus->subscriptions, topic, entry);
    
    if (!entry) {
        return -1;
    }
    
    /* If callback is provided, verify it matches */
    if (callback && entry->callback != callback) {
        return -1;
    }
    
    HASH_DEL(bus->subscriptions, entry);
    if (entry->topic) {
        uvrpc_free(entry->topic);
    }
    uvrpc_free(entry);
    
    return 0;
}

/* Dispatch message to subscribers (pub/sub) */
int uvrpc_bus_dispatch_message(uvrpc_bus_t* bus, const char* topic,
                                 const uint8_t* data, size_t size) {
    if (!bus || !topic) {
        return -1;
    }
    
    bus->stats.total_routed++;
    
    /* Find exact match subscribers */
    subscription_entry_t* entry;
    HASH_FIND_STR(bus->subscriptions, topic, entry);
    
    if (entry) {
        bus->stats.subscription_hits++;
        entry->callback(topic, data, size, entry->ctx);
        return 1;
    }
    
    /* Try wildcard matching */
    int matched = 0;
    for (subscription_entry_t* sub = bus->subscriptions; sub; sub = (subscription_entry_t*)sub->hh.next) {
        /* Simple wildcard matching: "topic.*" matches "topic.subtopic" */
        if (strchr(sub->topic, '*') != NULL) {
            char* wildcard = strchr(sub->topic, '*');
            if (wildcard) {
                size_t prefix_len = wildcard - sub->topic;
                if (strncmp(topic, sub->topic, prefix_len) == 0) {
                    matched++;
                    bus->stats.subscription_hits++;
                    sub->callback(topic, data, size, sub->ctx);
                }
            }
        }
    }
    
    return matched > 0 ? matched : -2;
}

/* Set filter for topic matching */
int uvrpc_bus_set_filter(uvrpc_bus_t* bus, const char* pattern,
                           uvrpc_filter_fn filter, void* ctx) {
    if (!bus || !pattern || !filter) {
        return -1;
    }
    
    /* Create new filter entry */
    filter_entry_t* entry = (filter_entry_t*)uvrpc_calloc(1, sizeof(filter_entry_t));
    if (!entry) {
        return -1;
    }
    
    entry->pattern = uvrpc_strdup(pattern);
    if (!entry->pattern) {
        uvrpc_free(entry);
        return -1;
    }
    
    entry->filter = filter;
    entry->ctx = ctx;
    
    HASH_ADD_STR(bus->filters, entry->pattern, entry);
    
    return 0;
}

/* Get routing statistics */
int uvrpc_bus_get_stats(uvrpc_bus_t* bus, uvrpc_bus_stats_t* stats) {
    if (!bus || !stats) {
        return -1;
    }
    
    memcpy(stats, &bus->stats, sizeof(uvrpc_bus_stats_t));
    return 0;
}

/* Clear routing statistics */
int uvrpc_bus_clear_stats(uvrpc_bus_t* bus) {
    if (!bus) {
        return -1;
    }
    
    memset(&bus->stats, 0, sizeof(uvrpc_bus_stats_t));
    return 0;
}