/**
 * @file uvrpc_client.c
 * @brief UVRPC Async Client Implementation
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
#include "../include/uvasync.h"
#include "uvrpc_flatbuffers.h"
#include "uvrpc_msgid.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* UVRPC_LOG_DEBUG / UVRPC_LOG_ERROR / UVRPC_LOG are provided by uvrpc.h */

/* Forward declarations */
static int uvrpc_client_call_no_retry_internal(uvrpc_client_t* client, const char* method,
                                                const uint8_t* params, size_t params_size,
                                                uvrpc_callback_t callback, void* ctx);

/* Pending callback - one slot per request awaiting its final response */
typedef struct pending_callback {
    uint32_t msgid;          /* Message ID for validation */
    uvrpc_callback_t callback;
    void* ctx;
    uint64_t deadline_ms;    /* Monotonic ms after which this slot is reclaimed
                              * and the callback is invoked with
                              * UVRPC_ERROR_TIMEOUT; 0 = no deadline */
} pending_callback_t;

/* Client structure */
struct uvrpc_client {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    int is_connected;
    uvrpc_msgid_ctx_t* msgid_ctx;  /* Message ID generator */

    /* User-defined context */
    uvrpc_context_t* ctx;

    /* User connect callback */
    uvrpc_connect_callback_t user_connect_callback;
    void* user_connect_ctx;

    /* Pending callbacks ring buffer (dynamically allocated based on config) */
    pending_callback_t** pending_callbacks;
    int max_pending_callbacks;  /* Size of ring buffer (must be power of 2) */

    /* Concurrency control. current_concurrent counts registered pending
     * callbacks - requests awaiting their final response. Oneway sends are not
     * counted because they never take a slot. */
    int max_concurrent;         /* Max concurrent requests */
    int current_concurrent;     /* Current pending request count */
    int timeout_ms;             /* Request deadline in ms; 0 = disabled */
    /* Release count, not a free count. A connect attempt is asynchronous and
     * its callback carries this pointer, so the struct has to outlive a
     * uvrpc_client_free() issued while the attempt is still in flight -- which
     * is what "connect, give up, free the client" does. */
    int ref_count;

    /* Retry configuration */
    int max_retries;            /* Maximum retry attempts (default: 0 = no retry) */
    uvasync_scheduler_t* scheduler;  /* Async scheduler for request concurrency control */
    /* Owned by the client, borrowed by the scheduler. uvbus/uvrpc keeps no
     * other reference, so whoever created the context destroys it. */
    uvasync_context_t* async_ctx;
};

/* Cleanup pending callback */
static void cleanup_pending_callback(pending_callback_t* pending) {
    if (!pending) return;
    uvrpc_free(pending);
}

/* Monotonic milliseconds, same clock the async layer and uvasync use. Only
 * ever compared against a stored deadline, so it does not have to be the
 * libuv clock. */
static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* How many expired callbacks one sweep pass reclaims and reports. A pass
 * collects this many, hands them all over, and starts again, so the working
 * set stays on the stack no matter how large the ring is. */
#define UVRPC_SWEEP_BATCH 32

typedef struct expired_slot {
    uvrpc_callback_t callback;
    void* ctx;
    uint32_t msgid;
} expired_slot_t;

/* Reclaim pending callbacks whose deadline has passed.
 *
 * A slot is otherwise only released when a response for its msgid arrives, so
 * on a transport that can drop a datagram a lost response consumes it until the
 * client is disconnected -- and the only symptom is UVRPC_ERROR_RATE_LIMITED
 * much later, once the ring is exhausted.
 *
 * Unlink-then-invoke, in that order, on purpose: every expired slot is removed
 * from the table and freed before any callback runs, so a callback that issues
 * another request and re-enters this function cannot find -- let alone free --
 * a slot that is about to be reported.
 */
static void sweep_expired_pending(uvrpc_client_t* client) {
    if (client->timeout_ms <= 0) return;

    expired_slot_t batch[UVRPC_SWEEP_BATCH];

    for (;;) {
        uint64_t now = monotonic_ms();
        int found = 0;

        for (int i = 0; i < client->max_pending_callbacks; i++) {
            pending_callback_t* pending = client->pending_callbacks[i];
            if (!pending || pending->deadline_ms == 0 ||
                pending->deadline_ms > now) {
                continue;
            }

            client->pending_callbacks[i] = NULL;
            if (client->current_concurrent > 0) {
                client->current_concurrent--;
            }
            batch[found].callback = pending->callback;
            batch[found].ctx = pending->ctx;
            batch[found].msgid = pending->msgid;
            found++;
            cleanup_pending_callback(pending);

            if (found == UVRPC_SWEEP_BATCH) break;
        }

        if (found == 0) return;

        /* The table is consistent again; now tell the application. */
        for (int i = 0; i < found; i++) {
            uvrpc_response_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.status = UVRPC_ERROR_TIMEOUT;
            resp.msgid = batch[i].msgid;
            resp.error_code = UVRPC_ERROR_TIMEOUT;
            resp.error_message = "request timed out: no response received";
            if (batch[i].callback) {
                batch[i].callback(&resp, batch[i].ctx);
            }
        }
    }
}

/* Transport connect callback */
static void client_destroy(uvrpc_client_t* client);

static void client_connect_callback(int status, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;

    UVRPC_LOG_DEBUG("client_connect_callback: status=%d, client=%p, user_ctx=%p",
              status, (void*)client, (void*)(client ? client->user_connect_ctx : NULL));

    if (client) {
        client->is_connected = (status == 0);

        /* Set bus->is_active for synchronous transports (INPROC, SAMELOOP) */
        if (client->uvbus && status == 0) {
            client->uvbus->is_active = 1;
        }

        /* Call user's connect callback if provided */
        if (client->user_connect_callback) {
            uvrpc_connect_callback_t cb = client->user_connect_callback;
            client->user_connect_callback = NULL;
            UVRPC_LOG_DEBUG("Calling user connect callback");
            cb(status, client->user_connect_ctx);
        } else {
            /* Normal path: uvrpc_client_connect() (no callback) was used */
            UVRPC_LOG_DEBUG("No user connect callback registered (connection status=%d)", status);
        }
    } else {
        UVRPC_LOG_ERROR("client_connect_callback: client is NULL");
    }

    if (status != 0) {
        UVRPC_LOG_ERROR("Client connection failed: %d", status);
    }

    /* Drop the reference taken when the connect started. If the client was
     * freed while the attempt was in flight this is the last one, so the
     * teardown happens here rather than in uvrpc_client_free. */
    if (--client->ref_count == 0) {
        client_destroy(client);
    }
}

/* Transport receive callback */
static void client_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)client_ctx;  /* Not used for client mode */
    uvrpc_client_t* client = (uvrpc_client_t*)server_ctx;

    UVRPC_LOG("Received %zu bytes", size);

    if (!client || !client->uvbus) {
        UVRPC_LOG("Invalid client or uvbus");
        uvrpc_free((void*)data);
        return;
    }

    /* Get frame type */
    int frame_type = uvrpc_get_frame_type(data, size);
    
    UVRPC_LOG("Frame type: %d", frame_type);
    
    if (frame_type < 0) {
        UVRPC_LOG("Invalid frame type");
        uvrpc_free((void*)data);
        return;
    }

    /* Only handle Response frames (type=1), ignore Request frames (type=0) */
    if (frame_type != 1 && frame_type != 2) {
        UVRPC_LOG("Not a response frame (type=%d), ignoring", frame_type);
        uvrpc_free((void*)data);
        return;
    }

    /* Decode response frame */
    uint32_t msgid;
    const uint8_t* result = NULL;
    size_t result_size = 0;

    if (uvrpc_decode_response(data, size, &msgid, &result, &result_size) != UVRPC_OK) {
        uvrpc_free((void*)data);
        return;
    }

    /* Find pending callback using direct indexing with bitmask */
    uint32_t idx = msgid & (client->max_pending_callbacks - 1);
    pending_callback_t* pending = client->pending_callbacks[idx];

    UVRPC_LOG("Received response: msgid=%u, idx=%u, pending=%p", msgid, idx, (void*)pending);

    /* Check if callback exists and matches msgid */
    if (pending && pending->msgid == msgid) {
        UVRPC_LOG("Found pending callback for msgid=%u (idx=%u)", msgid, idx);
        
        /* Create response structure */
        uvrpc_response_t resp;
        resp.status = UVRPC_OK;
        resp.msgid = msgid;
        resp.error_code = 0;
        resp.error_message = NULL;
        resp.user_data = NULL;
        resp.frame_type = frame_type;  // Store frame type for ResponseEnd detection

        /* Copy result data to avoid use-after-free */
        uint8_t* result_copy = NULL;
        if (result && result_size > 0) {
            result_copy = uvrpc_alloc(result_size);
            if (result_copy) {
                memcpy(result_copy, result, result_size);
            }
        }
        resp.result = result_copy;
        resp.result_size = result_size;

        /* Call callback */
        if (pending->callback) {
            pending->callback(&resp, pending->ctx);
        }

        /* Free copied result AFTER callback returns */
        if (result_copy) {
            uvrpc_free(result_copy);
        }
/* Get frame type from the response data */
        int frame_type = -1;
        if (data && size > 0) {
            frame_type = uvrpc_get_frame_type(data, size);
        }

        /* Cleanup pending callback only on Response (type=1, last response)
         * This allows multiple responses for the same msgid (stream mode)
         * type=1: Response (last) - cleanup callback
         * type=2: ResponseMore (more to come) - keep callback alive */
        if (frame_type == 1) {
            /* Response (last) - cleanup pending callback */
            client->pending_callbacks[idx] = NULL;
            cleanup_pending_callback(pending);
            client->current_concurrent--;
        }    }

    /* NOTE: data is freed by the transport layer (uvbus_transport_tcp.c:181)
     * after the callback returns. Do NOT free it here to avoid double free. */
}

/* Create client */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;

    uvrpc_client_t* client = uvrpc_calloc(1, sizeof(uvrpc_client_t));
    if (!client) return NULL;

    client->loop = config->loop;
    client->address = uvrpc_strdup(config->address);
    if (!client->address) {
        uvrpc_free(client);
        return NULL;
    }
    client->is_connected = 0;
    client->user_connect_callback = NULL;
    client->user_connect_ctx = NULL;

    /* Initialize concurrency control */
    client->max_concurrent = config->max_concurrent;
    client->current_concurrent = 0;
    client->timeout_ms = config->timeout_ms;
    client->ref_count = 1;

    /* Initialize ring buffer with runtime size */
    client->max_pending_callbacks = config->max_pending_callbacks;
    
    /* Allocate ring buffer array */
    client->pending_callbacks = (pending_callback_t**)uvrpc_calloc(
        client->max_pending_callbacks, sizeof(pending_callback_t*));
    if (!client->pending_callbacks) {
        uvrpc_free(client->address);
        uvrpc_free(client);
        return NULL;
    }

    /* Initialize retry configuration */
    client->max_retries = 0;  /* Default: no retry */

    /* Create message ID generator with unique offset from config */
    client->msgid_ctx = uvrpc_msgid_ctx_new();
    if (client->msgid_ctx) {
        /* Use msgid_offset from config if set, otherwise use default offset */
        uint32_t offset = (config->msgid_offset > 0) ? config->msgid_offset : 1;
        uvrpc_msgid_ctx_set_start(client->msgid_ctx, offset);
    }

    /* Create UVBus configuration */
    uvbus_config_t* bus_config = uvbus_config_new();
    if (!bus_config) {
        uvrpc_msgid_ctx_free(client->msgid_ctx);
        uvrpc_free(client->address);
        uvrpc_free(client);
        return NULL;
    }
    
    uvbus_config_set_loop(bus_config, config->loop);
    uvbus_config_set_loop_registry(bus_config, config->registry);
    
    /* Transport type is now uvbus_transport_type_t, no mapping needed */
    uvbus_config_set_transport(bus_config, config->transport);
    uvbus_config_set_address(bus_config, client->address);
    uvbus_config_set_recv_callback(bus_config, client_recv_callback, client);
    uvbus_config_set_connect_callback(bus_config, client_connect_callback, client);
    
    /* Create UVBus client */
    client->uvbus = uvbus_client_new(bus_config);
    if (!client->uvbus) {
        uvbus_config_free(bus_config);
        uvrpc_msgid_ctx_free(client->msgid_ctx);
        uvrpc_free(client->address);
        uvrpc_free(client);
        return NULL;
    }
    
    uvbus_config_free(bus_config);

    /* Initialize uvasync scheduler for concurrency control */
    if (client->max_concurrent > 0) {
        client->async_ctx = uvasync_context_create(client->loop);
        if (client->async_ctx) {
            client->scheduler = uvasync_scheduler_create(client->async_ctx,
                                                         client->max_concurrent);
            if (!client->scheduler) {
                uvasync_context_destroy(client->async_ctx);
                client->async_ctx = NULL;
            }
        }
    } else {
        client->scheduler = NULL;
        client->async_ctx = NULL;
    }

    return client;
}

/* Connect to server */
int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client || !client->uvbus) return UVRPC_ERROR_INVALID_PARAM;
    
    if (client->is_connected) return UVRPC_OK;
    
    /* Hold the client until the connect callback has run: it carries
     * this pointer, and uvbus_connect is asynchronous on the socket
     * transports. Without this, connecting and then giving up frees the
     * client out from under the pending callback. */
    client->ref_count++;

    uvbus_error_t err = uvbus_connect(client->uvbus);
    if (err != UVBUS_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }
    
    /* Note: is_connected will be set in the async callback */
    return UVRPC_OK;
}

/* Connect to server with callback */
int uvrpc_client_connect_with_callback(uvrpc_client_t* client,
                                         uvrpc_connect_callback_t callback, void* ctx) {
    UVRPC_LOG_DEBUG("connect_with_callback: client=%p, ctx=%p",
            (void*)client, ctx);

    if (!client || !client->uvbus) return UVRPC_ERROR_INVALID_PARAM;

    if (client->is_connected) return UVRPC_OK;

    /* Store user callback */
    client->user_connect_callback = callback;
    client->user_connect_ctx = ctx;

    /* Update UVBus config and transport callbacks */
    uvbus_t* uvbus = client->uvbus;
    uvbus->config.connect_cb = client_connect_callback;
    uvbus->config.callback_ctx = client;

    /* Also update transport callbacks directly for INPROC */
    if (uvbus->transport) {
        uvbus->transport->connect_cb = client_connect_callback;
        uvbus->transport->callback_ctx = client;
    }

    /* Hold the client until the connect callback has run: it carries
     * this pointer, and uvbus_connect is asynchronous on the socket
     * transports. Without this, connecting and then giving up frees the
     * client out from under the pending callback. */
    client->ref_count++;

    uvbus_error_t err = uvbus_connect(uvbus);
    if (err != UVBUS_OK) {
        UVRPC_LOG_ERROR("uvbus_connect failed: %d", err);
        return UVRPC_ERROR_TRANSPORT;
    }

    UVRPC_LOG_DEBUG("uvbus_connect returned OK");
    /* Note: is_connected will be set in the async callback */
    return UVRPC_OK;
}

/* Disconnect from server */
void uvrpc_client_disconnect(uvrpc_client_t* client) {
    if (!client) return;
    
    if (client->uvbus) {
        uvbus_disconnect(client->uvbus);
    }
    
    client->is_connected = 0;
}

/* Free client */
/* Release one reference; the last one performs the teardown. */
void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) return;

    uvrpc_client_disconnect(client);

    if (--client->ref_count == 0) {
        client_destroy(client);
    }
}

static void client_destroy(uvrpc_client_t* client) {
    if (!client) return;

    uvrpc_client_disconnect(client);

    /* Free uvasync scheduler */
    if (client->scheduler) {
        uvasync_scheduler_destroy(client->scheduler);
        client->scheduler = NULL;
    }

    /* Then the context it borrowed. The scheduler destructor reads
     * scheduler->ctx->loop, so the order matters. */
    if (client->async_ctx) {
        uvasync_context_destroy(client->async_ctx);
        client->async_ctx = NULL;
    }

    /* Free UVBus */
    if (client->uvbus) {
        uvbus_free(client->uvbus);
    }

    /* Free message ID context */
    if (client->msgid_ctx) {
        uvrpc_msgid_ctx_free(client->msgid_ctx);
    }

    client->is_connected = 0;

    /* Free all pending callbacks from ring buffer array */
    for (int i = 0; i < client->max_pending_callbacks; i++) {
        if (client->pending_callbacks[i]) {
            uvrpc_free(client->pending_callbacks[i]);
            client->pending_callbacks[i] = NULL;
        }
    }
    
    /* Free ring buffer array itself */
    if (client->pending_callbacks) {
        uvrpc_free(client->pending_callbacks);
        client->pending_callbacks = NULL;
    }

    uvrpc_free(client->address);
    uvrpc_free(client);
}

/* Get event loop */
uv_loop_t* uvrpc_client_get_loop(uvrpc_client_t* client) {
    if (!client) return NULL;
    return client->loop;
}

/* Set context */
void uvrpc_client_set_context(uvrpc_client_t* client, uvrpc_context_t* ctx) {
    if (client) {
        client->ctx = ctx;
    }
}

/* Get context */
uvrpc_context_t* uvrpc_client_get_context(uvrpc_client_t* client) {
    return client ? client->ctx : NULL;
}

/* Set max retries */
int uvrpc_client_set_max_retries(uvrpc_client_t* client, int max_retries) {
    if (!client) return UVRPC_ERROR_INVALID_PARAM;
    if (max_retries < 0) return UVRPC_ERROR_INVALID_PARAM;
    client->max_retries = max_retries;
    return UVRPC_OK;
}

/* Get max retries */
int uvrpc_client_get_max_retries(uvrpc_client_t* client) {
    if (!client) return 0;
    return client->max_retries;
}

/* Call remote method without retry (internal) */
static int uvrpc_client_call_no_retry_internal(uvrpc_client_t* client, const char* method,
                                                const uint8_t* params, size_t params_size,
                                                uvrpc_callback_t callback, void* ctx) {
    if (!client || !method) return UVRPC_ERROR_INVALID_PARAM;
    
    UVRPC_LOG("Calling method '%s', is_connected=%d", method, client->is_connected);
    
    if (!client->is_connected) {
        UVRPC_LOG("Not connected!");
        return UVRPC_ERROR_NOT_CONNECTED;
    }
    
    /* Generate message ID using context */
    uint32_t msgid = uvrpc_msgid_next(client->msgid_ctx);
    
    UVRPC_LOG("Generated msgid=%u", msgid);
    
    /* Encode request */
    uint8_t* req_data = NULL;
    size_t req_size = 0;
    
    if (uvrpc_encode_request(msgid, method, params, params_size,
                              &req_data, &req_size) != UVRPC_OK) {
        UVRPC_LOG("Failed to encode request");
        return UVRPC_ERROR;
    }
    
    UVRPC_LOG("Encoded request: %zu bytes", req_size);
    
    /* Register callback using direct indexing */
    uint32_t idx = 0;
    int slot_taken = 0;
    if (callback) {
        /* One in-flight request == one registered pending callback, so the
         * concurrency quota is checked wherever a slot is taken. */
        /* Reclaim anything already past its deadline first, so an
         * expired slot is returned rather than counted against the quota
         * and reported as RATE_LIMITED. */
        sweep_expired_pending(client);

        if (client->max_concurrent > 0 &&
            client->current_concurrent + 1 > client->max_concurrent) {
            uvrpc_free_encoded(req_data);
            return UVRPC_ERROR_RATE_LIMITED;
        }

        pending_callback_t* pending = uvrpc_calloc(1, sizeof(pending_callback_t));
        if (!pending) {
            uvrpc_free_encoded(req_data);
            return UVRPC_ERROR_NO_MEMORY;
        }

        pending->msgid = msgid;
        pending->deadline_ms = client->timeout_ms > 0
                                 ? monotonic_ms() + (uint64_t)client->timeout_ms
                                 : 0;
        pending->callback = callback;
        pending->ctx = ctx;

        /* Direct indexing with bitmask - O(1) */
        idx = msgid & (client->max_pending_callbacks - 1);
        if (client->pending_callbacks[idx] != NULL) {
            /* Slot is occupied by a live entry - the ring buffer is effectively
             * full. Do NOT block the event loop; let the caller back off. */
            uvrpc_free(pending);
            uvrpc_free_encoded(req_data);
            return UVRPC_ERROR_CALLBACK_LIMIT;
        }
        client->pending_callbacks[idx] = pending;
        client->current_concurrent++;
        slot_taken = 1;
    }

    /* Send request (must be after callback registration to avoid race conditions) */
    if (client->uvbus) {
        UVRPC_LOG("Sending %zu bytes via uvbus...", req_size);
        
        uvbus_error_t send_err = uvbus_send(client->uvbus, req_data, req_size);
        
        UVRPC_LOG("uvbus_send returned: %d", send_err);
        
        if (send_err != UVBUS_OK) {
            /* Send failed - roll back the slot we took. The ownership check also
             * covers a transport that completed the round trip synchronously and
             * already released the slot. */
            if (slot_taken && client->pending_callbacks[idx] &&
                client->pending_callbacks[idx]->msgid == msgid) {
                cleanup_pending_callback(client->pending_callbacks[idx]);
                client->pending_callbacks[idx] = NULL;
                client->current_concurrent--;
            }
            uvrpc_free_encoded(req_data);

            if (send_err == UVBUS_ERROR_BUFFER_FULL) {
                return UVRPC_ERROR_TRANSPORT_BUSY;
            }
            return UVRPC_ERROR_TRANSPORT;
        }
        
        UVRPC_LOG("Send successful");
    }

    uvrpc_free_encoded(req_data);
    
    return UVRPC_OK;
}

/* Call remote method with retry */
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx) {
    if (!client || !method) return UVRPC_ERROR_INVALID_PARAM;
    
    /* If retry is disabled, call directly */
    if (client->max_retries <= 0) {
        return uvrpc_client_call_no_retry_internal(client, method, params, params_size, callback, ctx);
    }
    
    /* Retry logic - just retry without running event loop */
    /* The event loop is driven externally, retries will be handled naturally */
    int ret;
    int retries = 0;
    
    do {
        ret = uvrpc_client_call_no_retry_internal(client, method, params, params_size, callback, ctx);
        
        if (ret == UVRPC_OK) {
            break;  /* Success - exit retry loop */
        }
        
        retries++;
        
    } while (retries < client->max_retries);
    
    return ret;
}

/* Call remote method without retry (public API) */
int uvrpc_client_call_no_retry(uvrpc_client_t* client, const char* method,
                                const uint8_t* params, size_t params_size,
                                uvrpc_callback_t callback, void* ctx) {
    return uvrpc_client_call_no_retry_internal(client, method, params, params_size, callback, ctx);
}

/* Call RPC method with oneway mode (zero overhead: no callback, no response) */
int uvrpc_client_call_oneway(uvrpc_client_t* client, const char* method,
                              const uint8_t* params, size_t params_size) {
    if (!client || !method) return UVRPC_ERROR_INVALID_PARAM;
    
    if (!client->is_connected) {
        return UVRPC_ERROR_NOT_CONNECTED;
    }
    
    /* Generate message ID */
    uint32_t msgid = uvrpc_msgid_next(client->msgid_ctx);
    
    /* Encode request */
    uint8_t* req_data = NULL;
    size_t req_size = 0;
    
    if (uvrpc_encode_request(msgid, method, params, params_size,
                              &req_data, &req_size) != UVRPC_OK) {
        return UVRPC_ERROR;
    }
    
    /* Send immediately */
    if (client->uvbus) {
        uvbus_error_t send_err = uvbus_send(client->uvbus, req_data, req_size);
        if (send_err != UVBUS_OK) {
            uvrpc_free_encoded(req_data);
            if (send_err == UVBUS_ERROR_BUFFER_FULL) {
                return UVRPC_ERROR_TRANSPORT_BUSY;
            }
            return UVRPC_ERROR_TRANSPORT;
        }
    }
    
    uvrpc_free_encoded(req_data);
    
    return UVRPC_OK;
}

/* Free response */
void uvrpc_response_free(uvrpc_response_t* resp) {
    if (!resp) return;
    /* Note: error and result point to frame data, don't free them here */
}

/* Set max concurrent requests */
int uvrpc_client_set_max_concurrent(uvrpc_client_t* client, int max_concurrent) {
    if (!client) return UVRPC_ERROR_INVALID_PARAM;
    client->max_concurrent = (max_concurrent > 0) ? max_concurrent : UVRPC_MAX_CONCURRENT_REQUESTS;
    return UVRPC_OK;
}

/* Set the request deadline */
int uvrpc_client_set_timeout(uvrpc_client_t* client, int timeout_ms) {
    if (!client) return UVRPC_ERROR_INVALID_PARAM;
    client->timeout_ms = (timeout_ms > 0) ? timeout_ms : 0;
    return UVRPC_OK;
}

/* Get pending request count */
int uvrpc_client_get_pending_count(uvrpc_client_t* client) {
    if (!client) return 0;
    return client->current_concurrent;
}

/* Batch call - send multiple requests efficiently */
int uvrpc_client_call_batch(uvrpc_client_t* client,
                             const char** methods,
                             const uint8_t** params_array,
                             size_t* params_sizes,
                             uvrpc_callback_t* callbacks,
                             void** contexts,
                             int count) {
    if (!client || !methods || !params_array || !params_sizes || !callbacks || !contexts) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        return UVRPC_ERROR_NOT_CONNECTED;
    }

    if (count <= 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Check concurrency limit */
    if (client->max_concurrent > 0 &&
        (client->current_concurrent + count) > client->max_concurrent) {
        return UVRPC_ERROR_RATE_LIMITED;
    }

    /* Send all requests */
    for (int i = 0; i < count; i++) {
        if (!methods[i]) {
            return UVRPC_ERROR_INVALID_PARAM;
        }

        /* Generate message ID */
        uint32_t msgid = uvrpc_msgid_next(client->msgid_ctx);

        /* Encode request */
        uint8_t* req_data = NULL;
        size_t req_size = 0;

        if (uvrpc_encode_request(msgid, methods[i], params_array[i], params_sizes[i],
                                  &req_data, &req_size) != UVRPC_OK) {
            return UVRPC_ERROR;
        }

        /* Register callback */
        if (client->timeout_ms > 0) sweep_expired_pending(client);
        if (callbacks[i]) {
            pending_callback_t* pending = uvrpc_calloc(1, sizeof(pending_callback_t));
            if (!pending) {
                uvrpc_free_encoded(req_data);
                return UVRPC_ERROR_NO_MEMORY;
            }

            uint32_t idx = msgid & (client->max_pending_callbacks - 1);
            if (client->pending_callbacks[idx] != NULL) {
                uvrpc_free(pending);
                uvrpc_free_encoded(req_data);
                return UVRPC_ERROR_CALLBACK_LIMIT;
            }

            pending->msgid = (uint32_t)msgid;
            pending->deadline_ms = client->timeout_ms > 0
                                     ? monotonic_ms() + (uint64_t)client->timeout_ms
                                     : 0;
            pending->callback = callbacks[i];
            pending->ctx = contexts[i];
            client->pending_callbacks[idx] = pending;
            client->current_concurrent++;
        }

        /* Send request */
        if (client->uvbus) {
            uvbus_error_t send_err = uvbus_send(client->uvbus, req_data, req_size);
            if (send_err != UVBUS_OK) {
                /* Roll back this entry's slot so a dropped send does not leak a
                 * pending callback that no response will ever release. */
                uint32_t idx = msgid & (client->max_pending_callbacks - 1);
                if (callbacks[i] && client->pending_callbacks[idx] &&
                    client->pending_callbacks[idx]->msgid == msgid) {
                    cleanup_pending_callback(client->pending_callbacks[idx]);
                    client->pending_callbacks[idx] = NULL;
                    client->current_concurrent--;
                }
                uvrpc_free_encoded(req_data);
                return (send_err == UVBUS_ERROR_BUFFER_FULL) ? UVRPC_ERROR_TRANSPORT_BUSY
                                                             : UVRPC_ERROR_TRANSPORT;
            }
        }

        uvrpc_free_encoded(req_data);
    }

    return UVRPC_OK;
}

