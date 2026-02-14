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

/* Pending callback - using ring buffer array for O(1) lookup */
typedef struct pending_callback {
    uint32_t msgid;  /* For validation */
    uvrpc_callback_t callback;
    void* ctx;
} pending_callback_t;

/* Client structure */
struct uvrpc_client {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_t* transport;
    int is_connected;
    uvrpc_msgid_ctx_t* msgid_ctx;  /* 消息ID生成器 */

    /* User connect callback */
    uvrpc_connect_callback_t user_connect_callback;
    void* user_connect_ctx;

    /* Pending callbacks ring buffer array (O(1) lookup) */
    pending_callback_t* pending_callbacks[UVRPC_MAX_PENDING_CALLBACKS];
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
static void client_recv_callback(uint8_t* data, size_t size, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;
    
    /* Decode response */
    uint32_t msgid;
    int32_t error_code = 0;
    const uint8_t* result = NULL;
    size_t result_size = 0;
    
    if (uvrpc_decode_response(data, size, &msgid, &error_code, &result, &result_size) != UVRPC_OK) {
        uvrpc_free(data);
        return;
    }
    
    /* Find pending callback using ring buffer array for O(1) lookup */
    uint32_t idx = msgid % UVRPC_MAX_PENDING_CALLBACKS;
    pending_callback_t* pending = client->pending_callbacks[idx];

    if (pending && pending->msgid == msgid) {
        /* Create response structure */
        uvrpc_response_t resp;
        resp.status = (error_code != 0) ? UVRPC_ERROR : UVRPC_OK;
        resp.msgid = msgid;
        resp.error_code = error_code;
        
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
        
        /* Remove from ring buffer array */
        client->pending_callbacks[idx] = NULL;
        uvrpc_free(pending);
    }
    
    free(data);
}

/* Create client */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    uvrpc_client_t* client = calloc(1, sizeof(uvrpc_client_t));
    if (!client) return NULL;
    
    client->loop = config->loop;
    client->address = strdup(config->address);
    client->is_connected = 0;
    client->user_connect_callback = NULL;
    client->user_connect_ctx = NULL;

    /* Initialize ring buffer array */
    memset(client->pending_callbacks, 0, sizeof(client->pending_callbacks));
    
    /* Create message ID generator */
    client->msgid_ctx = uvrpc_msgid_ctx_new();
    
    /* Create transport with specified type */
    client->transport = uvrpc_transport_client_new(config->loop, config->transport);
    if (!client->transport) {
        uvrpc_msgid_ctx_free(client->msgid_ctx);
        uvrpc_free(client->address);
            uvrpc_free(client);        return NULL;
    }
    
    return client;
}

/* Connect to server */
int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client || !client->transport) return UVRPC_ERROR_INVALID_PARAM;
    
    if (client->is_connected) return UVRPC_OK;
    
    return uvrpc_transport_connect(client->transport, client->address,
                                    client_connect_callback,
                                    client_recv_callback, client);
}

/* Connect to server with callback */
int uvrpc_client_connect_with_callback(uvrpc_client_t* client, 
                                         uvrpc_connect_callback_t callback, void* ctx) {
    if (!client || !client->transport) return UVRPC_ERROR_INVALID_PARAM;
    
    if (client->is_connected) return UVRPC_OK;
    
    /* Store user callback */
    client->user_connect_callback = callback;
    client->user_connect_ctx = ctx;
    
    return uvrpc_transport_connect(client->transport, client->address,
                                    client_connect_callback,
                                    client_recv_callback, client);
}

/* Disconnect from server */
void uvrpc_client_disconnect(uvrpc_client_t* client) {
    if (!client) return;
    
    if (client->transport) {
        uvrpc_transport_disconnect(client->transport);
    }
    
    client->is_connected = 0;
}

/* Free client */
void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) return;
    
    uvrpc_client_disconnect(client);
    
    /* Free transport */
    if (client->transport) {
        uvrpc_transport_free(client->transport);
    }
    
    /* Free message ID context */
    if (client->msgid_ctx) {
        uvrpc_msgid_ctx_free(client->msgid_ctx);
    }
    
client->is_connected = 0;

    /* Free all pending callbacks from ring buffer array */
    for (int i = 0; i < UVRPC_MAX_PENDING_CALLBACKS; i++) {
        if (client->pending_callbacks[i]) {
            uvrpc_free(client->pending_callbacks[i]);
            client->pending_callbacks[i] = NULL;
        }
    }
    
    uvrpc_free(client->address);
    uvrpc_free(client);
}

/* Call remote method */
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx) {
    if (!client || !method) return UVRPC_ERROR_INVALID_PARAM;
    
    if (!client->is_connected) {
        return UVRPC_ERROR_NOT_CONNECTED;
    }
    
    /* Generate message ID using context */
    uint64_t msgid = uvrpc_msgid_next(client->msgid_ctx);
    
    /* Encode request */
    uint8_t* req_data = NULL;
    size_t req_size = 0;
    
    if (uvrpc_encode_request(msgid, method, params, params_size,
                              &req_data, &req_size) != UVRPC_OK) {
        return UVRPC_ERROR;
    }
    
    /* Register callback in ring buffer array */
    if (callback) {
        pending_callback_t* pending = uvrpc_calloc(1, sizeof(pending_callback_t));
        if (!pending) {
            uvrpc_free(req_data);
            return UVRPC_ERROR_NO_MEMORY;
        }

        uint32_t idx = (uint32_t)msgid % UVRPC_MAX_PENDING_CALLBACKS;
        if (client->pending_callbacks[idx] != NULL) {
            /* Collision detected - should not happen with sequential msgids */
            uvrpc_free(pending);
            uvrpc_free(req_data);
            return UVRPC_ERROR_CALLBACK_LIMIT;
        }

        pending->msgid = (uint32_t)msgid;
        pending->callback = callback;
        pending->ctx = ctx;
        client->pending_callbacks[idx] = pending;
    }
    
    /* Send request */
    if (client->transport) {
        uvrpc_transport_send(client->transport, req_data, req_size);
    }
    
    uvrpc_free(req_data);
    
    return UVRPC_OK;
}

/* Free response */
void uvrpc_response_free(uvrpc_response_t* resp) {
    if (!resp) return;
    /* Note: error and result point to frame data, don't free them here */
}