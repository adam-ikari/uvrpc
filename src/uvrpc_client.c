/**
 * UVRPC Async Client
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include "uvrpc_flatbuffers.h"
#include "uvrpc_transport.h"
#include "uvrpc_msgid.h"
#include <stdlib.h>
#include <string.h>

/* Pending callback - using direct indexing ring buffer */
typedef struct pending_callback {
    uint32_t msgid;          /* Message ID for validation */
    uint32_t generation;     /* Generation counter to detect stale entries */
    uvrpc_callback_t callback;
    void* ctx;
} pending_callback_t;

/* Client structure */
struct uvrpc_client {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    int is_connected;
    uvrpc_msgid_ctx_t* msgid_ctx;  /* Message ID generator */
    uvrpc_perf_mode_t performance_mode;  /* Performance mode: low latency vs high throughput */

    /* User connect callback */
    uvrpc_connect_callback_t user_connect_callback;
    void* user_connect_ctx;

    /* Pending callbacks ring buffer (dynamically allocated based on config) */
    pending_callback_t** pending_callbacks;
    int max_pending_callbacks;  /* Size of ring buffer (must be power of 2) */
    uint32_t generation;  /* Generation counter to detect stale entries */

    /* Concurrency control */
    int max_concurrent;         /* Max concurrent requests */
    int current_concurrent;     /* Current pending request count */
    uint64_t timeout_ms;        /* Default timeout */

    /* Batch processing */
    int batching_enabled;       /* Enable request batching */
    int batch_size;             /* Current batch size */
    int max_batch_size;         /* Max batch size before flush */

    /* Retry configuration */
    int max_retries;            /* Maximum retry attempts (default: 0 = no retry) */
};

/* Transport connect callback */
static void client_connect_callback(int status, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;
    client->is_connected = (status == 0);
    
    /* Call user's connect callback if provided */
    if (client->user_connect_callback) {
        uvrpc_connect_callback_t cb = client->user_connect_callback;
        client->user_connect_callback = NULL;
        cb(status, client->user_connect_ctx);
    }
    
    if (status != 0) {
        fprintf(stderr, "Client connection failed: %d\n", status);
    }
}

/* Transport receive callback */
static void client_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)client_ctx;  /* Not used for client mode */
    uvrpc_client_t* client = (uvrpc_client_t*)server_ctx;

    /* Get frame type */
    int frame_type = uvrpc_get_frame_type(data, size);
    
    if (frame_type < 0) {
        uvrpc_free(data);
        return;
    }

    uint32_t msgid;

    if (frame_type == 3) {
        /* Decode error frame */
        int32_t error_code = 0;
        char* error_message = NULL;

        if (uvrpc_decode_error(data, size, &msgid, &error_code, &error_message) != UVRPC_OK) {
            uvrpc_free(data);
            return;
        }

        /* Find pending callback using direct indexing with bitmask */
        uint32_t idx = msgid & (client->max_pending_callbacks - 1);
        pending_callback_t* pending = client->pending_callbacks[idx];

        /* Check if callback exists and matches msgid and generation */
        if (pending && pending->msgid == msgid && pending->generation == client->generation) {
            /* Create error response structure */
            uvrpc_response_t resp;
            resp.status = UVRPC_ERROR;
            resp.msgid = msgid;
            resp.error_code = error_code;
            resp.error_message = error_message;
            resp.result = NULL;
            resp.result_size = 0;
            resp.user_data = NULL;

            /* Call callback */
            if (pending->callback) {
                pending->callback(&resp, pending->ctx);
            }

            /* Free error message */
            if (error_message) {
                uvrpc_free(error_message);
            }

            /* Remove from ring buffer */
            client->pending_callbacks[idx] = NULL;
            uvrpc_free(pending);
        }

        uvrpc_free(data);
        return;
    }

    /* Decode response frame */
    const uint8_t* result = NULL;
    size_t result_size = 0;

    if (uvrpc_decode_response(data, size, &msgid, &result, &result_size) != UVRPC_OK) {
        uvrpc_free(data);
        return;
    }

    /* Find pending callback using direct indexing with bitmask */
    uint32_t idx = msgid & (client->max_pending_callbacks - 1);
    pending_callback_t* pending = client->pending_callbacks[idx];

    /* Check if callback exists and matches msgid and generation */
    if (pending && pending->msgid == msgid && pending->generation == client->generation) {
        /* Create response structure */
        uvrpc_response_t resp;
        resp.status = UVRPC_OK;
        resp.msgid = msgid;
        resp.error_code = 0;
        resp.error_message = NULL;

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
        resp.user_data = NULL;

        /* Call callback */
        if (pending->callback) {
            pending->callback(&resp, pending->ctx);
        }

        /* Free copied result */
        if (result_copy) {
            uvrpc_free(result_copy);
        }

        /* Remove from ring buffer */
        client->pending_callbacks[idx] = NULL;
        uvrpc_free(pending);

        /* Decrease concurrent count */
        client->current_concurrent--;
    }

    uvrpc_free(data);
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
    client->performance_mode = config->performance_mode;
    client->user_connect_callback = NULL;
    client->user_connect_ctx = NULL;

    /* Initialize concurrency control */
    client->max_concurrent = config->max_concurrent;
    client->current_concurrent = 0;
    client->timeout_ms = config->timeout_ms;

    /* Initialize ring buffer with runtime size */
    client->max_pending_callbacks = config->max_pending_callbacks;
    client->generation = 0;
    
    /* Allocate ring buffer array */
    client->pending_callbacks = (pending_callback_t**)uvrpc_calloc(
        client->max_pending_callbacks, sizeof(pending_callback_t*));
    if (!client->pending_callbacks) {
        uvrpc_free(client->address);
        uvrpc_free(client);
        return NULL;
    }

    /* Initialize batch processing */
    client->batching_enabled = (config->performance_mode == UVRPC_PERF_HIGH_THROUGHPUT);
    client->batch_size = 0;
    client->max_batch_size = 100;  /* Default batch size */

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
    
    /* Map UVRPC transport type to UVBus transport type */
    uvbus_transport_type_t uvbus_type;
    switch (config->transport) {
        case UVRPC_TRANSPORT_TCP:
            uvbus_type = UVBUS_TRANSPORT_TCP;
            break;
        case UVRPC_TRANSPORT_UDP:
            uvbus_type = UVBUS_TRANSPORT_UDP;
            break;
        case UVRPC_TRANSPORT_IPC:
            uvbus_type = UVBUS_TRANSPORT_IPC;
            break;
        case UVRPC_TRANSPORT_INPROC:
            uvbus_type = UVBUS_TRANSPORT_INPROC;
            break;
        default:
            uvbus_config_free(bus_config);
            uvrpc_msgid_ctx_free(client->msgid_ctx);
            uvrpc_free(client->address);
            uvrpc_free(client);
            return NULL;
    }
    
    uvbus_config_set_transport(bus_config, uvbus_type);
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

    return client;
}

/* Connect to server */
int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client || !client->uvbus) return UVRPC_ERROR_INVALID_PARAM;
    
    if (client->is_connected) return UVRPC_OK;
    
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
    if (!client || !client->uvbus) return UVRPC_ERROR_INVALID_PARAM;
    
    if (client->is_connected) return UVRPC_OK;
    
    fprintf(stderr, "[DEBUG] uvrpc_client_connect_with_callback: Starting connection\n");
    
    /* Store user callback */
    client->user_connect_callback = callback;
    client->user_connect_ctx = ctx;
    
    /* Update UVBus config with callback */
    uvbus_t* uvbus = client->uvbus;
    uvbus->config.connect_cb = client_connect_callback;
    uvbus->config.callback_ctx = client;
    
    fprintf(stderr, "[DEBUG] uvrpc_client_connect_with_callback: Calling uvbus_connect\n");
    uvbus_error_t err = uvbus_connect(uvbus);
    if (err != UVBUS_OK) {
        fprintf(stderr, "[DEBUG] uvrpc_client_connect_with_callback: uvbus_connect returned error %d\n", err);
        return UVRPC_ERROR_TRANSPORT;
    }
    
    fprintf(stderr, "[DEBUG] uvrpc_client_connect_with_callback: uvbus_connect returned OK\n");
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
void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) return;

    uvrpc_client_disconnect(client);

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
    
    if (!client->is_connected) {
        return UVRPC_ERROR_NOT_CONNECTED;
    }
    
    /* Generate message ID using context */
    uint32_t msgid = uvrpc_msgid_next(client->msgid_ctx);
    
    /* Encode request */
    uint8_t* req_data = NULL;
    size_t req_size = 0;
    
    if (uvrpc_encode_request(msgid, method, params, params_size,
                              &req_data, &req_size) != UVRPC_OK) {
        return UVRPC_ERROR;
    }
    
    /* Register callback using direct indexing */
    if (callback) {
        pending_callback_t* pending = uvrpc_calloc(1, sizeof(pending_callback_t));
        if (!pending) {
            uvrpc_free(req_data);
            return UVRPC_ERROR_NO_MEMORY;
        }

        pending->msgid = msgid;
        pending->generation = client->generation;
        pending->callback = callback;
        pending->ctx = ctx;

        /* Direct indexing with bitmask - O(1) */
        uint32_t idx = msgid & (client->max_pending_callbacks - 1);
        pending_callback_t* existing = client->pending_callbacks[idx];

        if (existing == NULL) {
            /* Slot is empty - insert directly */
            client->pending_callbacks[idx] = pending;
        } else if (existing->generation != client->generation) {
            /* Stale entry from previous generation - replace it */
            uvrpc_free(existing);
            client->pending_callbacks[idx] = pending;
        } else {
            /* Slot is occupied by a valid entry - ring buffer is effectively full */
            /* Do NOT block the event loop - return error and let caller handle it */
            uvrpc_free(pending);
            uvrpc_free(req_data);
            return UVRPC_ERROR_CALLBACK_LIMIT;
        }
    }

    /* Send request */
    if (client->uvbus) {
        fprintf(stderr, "[DEBUG] client_call: Sending request via uvbus\n");
        fflush(stderr);
        uvbus_send(client->uvbus, req_data, req_size);
        fprintf(stderr, "[DEBUG] client_call: uvbus_send returned\n");
        fflush(stderr);
    }

    /* Send request */
    if (client->uvbus) {
        uvbus_send(client->uvbus, req_data, req_size);
    }

    uvrpc_free(req_data);

    fprintf(stderr, "[DEBUG] client_call: Returning OK\n");
    fflush(stderr);
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
        if (callbacks[i]) {
            pending_callback_t* pending = uvrpc_calloc(1, sizeof(pending_callback_t));
            if (!pending) {
                uvrpc_free(req_data);
                return UVRPC_ERROR_NO_MEMORY;
            }

            uint32_t idx = (uint32_t)msgid % client->max_pending_callbacks;
            if (client->pending_callbacks[idx] != NULL) {
                uvrpc_free(pending);
                uvrpc_free(req_data);
                return UVRPC_ERROR_CALLBACK_LIMIT;
            }

            pending->msgid = (uint32_t)msgid;
            pending->callback = callbacks[i];
            pending->ctx = contexts[i];
            client->pending_callbacks[idx] = pending;

            /* Increase concurrent count */
            client->current_concurrent++;
        }

        /* Send request (no flush for batch except last) */
        if (client->uvbus) {
            uvbus_send(client->uvbus, req_data, req_size);
        }

        uvrpc_free(req_data);
    }

    return UVRPC_OK;
}