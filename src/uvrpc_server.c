/**
 * @file uvrpc_server.c
 * @brief UVRPC Async Server Implementation
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
#include "uvrpc_flatbuffers.h"
#include "uvrpc_msgid.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

/* Debug logging macro - compiles out in release builds */
#ifdef UVRPC_DEBUG
#define UVRPC_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define UVRPC_LOG(fmt, ...) ((void)0)
#endif

/* Error logging - always enabled */
#define UVRPC_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

/* Write callback to free buffer */
static void write_callback(uv_write_t* req, int status) {
    (void)status;
    if (req) {
        if (req->data) {
            uvrpc_free(req->data);
        }
        uvrpc_free(req);
    }
}

/* Get current timestamp in milliseconds */
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Handler registry */
typedef struct handler_entry {
    char* name;
    uvrpc_handler_t handler;
    void* ctx;
    UT_hash_handle hh;  /* uthash handle */
} handler_entry_t;

/* Pending request - for ring buffer (used for stream connections) */
typedef struct pending_request {
    uint32_t msgid;              /* Message ID */
    uint32_t generation;         /* Generation counter */
    void* client_ctx;            /* Client context for sending response */
    uint64_t create_time;        /* Creation timestamp (for finding oldest) */
    int in_use;                  /* Flag to indicate if slot is in use */
} pending_request_t;

/* Server structure */
struct uvrpc_server {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    handler_entry_t* handlers;
    int is_running;
    
    /* User-defined context */
    uvrpc_context_t* ctx;
    
    /* Pending requests ring buffer (for stream connections) */
    pending_request_t** pending_requests;
    int max_pending_requests;    /* Ring buffer size (must be power of 2) */
    int current_streams;         /* Current active stream connections */
    int max_streams;             /* Maximum stream connections (default: 1000) */
    uint32_t generation;         /* Generation counter */
    
    /* Statistics */
    uint64_t total_requests;
    uint64_t total_responses;
};

/* Server receive callback */
static void server_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    uvrpc_server_t* server = (uvrpc_server_t*)server_ctx;

    UVRPC_LOG("Received %zu bytes, server=%p", size, server);

    if (!server) {
        return;
    }

    /* Debug: log client_ctx */
    static int recv_count = 0;
    recv_count++;
    if (recv_count % 1000 == 0) {
        UVRPC_LOG("Received %d requests, client_ctx=%p", recv_count, client_ctx);
    } else {
        UVRPC_LOG("Request #%d, client_ctx=%p", recv_count, client_ctx);
    }

    /* Decode request */
    uint32_t msgid = 0;
    char* method = NULL;
    const uint8_t* params = NULL;
    size_t params_size = 0;

    UVRPC_LOG("Decoding request...");
    
    if (uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size) != UVRPC_OK) {
        UVRPC_LOG("Failed to decode request (size=%zu)", size);
        UVRPC_ERROR("Failed to decode request (size=%zu)", size);
        /* Note: client_ctx is managed by the transport layer, not freed here */
        return;
    }

    UVRPC_LOG("Decoded: msgid=%u, method=%s, params_size=%zu", msgid, method ? method : "(null)", params_size);

    if (recv_count % 1000 == 0) {
        fprintf(stderr, "[SERVER] Decoded request: msgid=%u, method=%s\n", msgid, method ? method : "(null)");
    }

    /* Create lowercase copy for case-insensitive matching */
    char* method_lower = NULL;
    if (method) {
        method_lower = uvrpc_strdup(method);
        if (method_lower) {
            for (char* p = method_lower; *p; p++) {
                *p = tolower((unsigned char)*p);
            }
        }
    }
    
    /* Find handler using lowercase method name */
    handler_entry_t* entry = NULL;
    HASH_FIND_STR(server->handlers, method_lower, entry);
    
    /* Free the lowercase copy */
    if (method_lower) {
        uvrpc_free(method_lower);
    }
    
    if (entry && entry->handler) {
        /* Increment request counter */
        server->total_requests++;

        /* Check if this is a stream request (starts with "sub_") */
        int is_stream = (method && strncmp(method, "sub_", 4) == 0);

        /* If stream and at capacity, disconnect oldest stream */
        if (is_stream && server->current_streams >= server->max_streams) {
            /* Find oldest stream connection */
            int oldest_idx = -1;
            uint64_t oldest_time = UINT64_MAX;
            
            for (int i = 0; i < server->max_pending_requests; i++) {
                pending_request_t* pending = server->pending_requests[i];
                if (pending && pending->in_use && pending->generation == server->generation) {
                    if (pending->create_time < oldest_time) {
                        oldest_time = pending->create_time;
                        oldest_idx = i;
                    }
                }
            }
            
            /* Disconnect oldest stream */
            if (oldest_idx >= 0) {
                pending_request_t* oldest = server->pending_requests[oldest_idx];
                
                /* Send end marker to oldest stream */
                uint8_t* resp_data = NULL;
                size_t resp_size = 0;
                uvrpc_encode_response(oldest->msgid, NULL, 0, &resp_data, &resp_size);
                if (resp_data) {
                    uvbus_send_to(server->uvbus, resp_data, resp_size, oldest->client_ctx);
                    uvrpc_free(resp_data);
                }
                
                /* Free oldest stream */
                uvrpc_free(oldest);
                server->pending_requests[oldest_idx] = NULL;
                server->current_streams--;
            }
        }

        /* Save stream request to pending_requests */
        if (is_stream) {
            /* Find free slot in ring buffer using linear search for streams */
            int free_idx = -1;
            for (int i = 0; i < server->max_pending_requests; i++) {
                pending_request_t* pending = server->pending_requests[i];
                if (!pending || !pending->in_use || pending->generation != server->generation) {
                    free_idx = i;
                    break;
                }
            }
            
            if (free_idx < 0) {
                /* No free slot - cannot accept more streams */
                UVRPC_ERROR("No free slot for stream request (msgid=%u)", msgid);
                /* Send error response */
                uint8_t* resp_data = NULL;
                size_t resp_size = 0;
                uvrpc_encode_response(msgid, NULL, 0, &resp_data, &resp_size);
                if (resp_data) {
                    uvbus_send_to(server->uvbus, resp_data, resp_size, client_ctx);
                    uvrpc_free(resp_data);
                }
                if (method) uvrpc_free(method);
                return;
            }
            
            /* Create new pending request */
            pending_request_t* pending = uvrpc_calloc(1, sizeof(pending_request_t));
            if (pending) {
                pending->msgid = msgid;
                pending->generation = server->generation;
                pending->client_ctx = client_ctx;
                pending->create_time = get_timestamp_ms();
                pending->in_use = 1;
                
                server->pending_requests[free_idx] = pending;
                server->current_streams++;
            }
        }

        /* Create request structure */
        uvrpc_request_t req;
        req.server = server;
        req.msgid = msgid;
        req.method = method;
        req.params = (uint8_t*)params;
        req.params_size = params_size;
        req.client_ctx = client_ctx;
        req.user_data = NULL;

        /* Call handler - handler is responsible for sending response */
        entry->handler(&req, entry->ctx);

        /* Note: client_ctx is managed by the transport layer, not freed here */

        /* Free decoded method */
        if (method) uvrpc_free(method);
    } else {
        /* Handler not found, send error response */
        UVRPC_ERROR("Handler not found: '%s'", method);
        uint8_t* resp_data = NULL;
        size_t resp_size = 0;

        /* Error encoded in response data payload: code + message */
        int32_t error_code = 2;
        const char* error_msg = "Method not found";
        size_t error_msg_len = strlen(error_msg);
        size_t payload_size = sizeof(int32_t) + error_msg_len;
        uint8_t* payload = uvrpc_alloc(payload_size);
        if (payload) {
            memcpy(payload, &error_code, sizeof(int32_t));
            memcpy(payload + sizeof(int32_t), error_msg, error_msg_len);
            uvrpc_encode_response(msgid, payload, payload_size, &resp_data, &resp_size);
            uvrpc_free(payload);
        }

        if (resp_data) {
            /* Send error response via UVBus */
            uvbus_t* uvbus = server->uvbus;
            if (uvbus) {
                uvbus_send_to(uvbus, resp_data, resp_size, client_ctx);
            }
            uvrpc_free(resp_data);
        }

        /* Note: client_ctx is managed by the transport layer, not freed here */

        if (method) uvrpc_free(method);
    }
}

/* Server statistics */
uint64_t uvrpc_server_get_total_requests(uvrpc_server_t* server) {
    if (!server) return 0;
    return server->total_requests;
}

uint64_t uvrpc_server_get_total_responses(uvrpc_server_t* server) {
    if (!server) return 0;
    return server->total_responses;
}

/* Create server */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    uvrpc_server_t* server = uvrpc_calloc(1, sizeof(uvrpc_server_t));
    if (!server) return NULL;

    server->loop = config->loop;
    server->address = uvrpc_strdup(config->address);
    if (!server->address) {
        uvrpc_free(server);
        return NULL;
    }
    server->handlers = NULL;
    server->is_running = 0;
    
    /* Initialize ring buffer */
    server->max_pending_requests = (config->max_pending_callbacks > 0) ? 
                                   config->max_pending_callbacks : UVRPC_DEFAULT_PENDING_CALLBACKS;
    server->generation = 0;
    server->current_streams = 0;
    server->max_streams = 1000;  /* Default max stream connections */
    
    /* Allocate ring buffer array */
    server->pending_requests = (pending_request_t**)uvrpc_calloc(
        server->max_pending_requests, sizeof(pending_request_t*));
    if (!server->pending_requests) {
        uvrpc_free(server->address);
        uvrpc_free(server);
        return NULL;
    }

    /* Create UVBus configuration */
    uvbus_config_t* bus_config = uvbus_config_new();
    if (!bus_config) {
        uvrpc_free(server->pending_requests);
        uvrpc_free(server->address);
        uvrpc_free(server);
        return NULL;
    }
    
    uvbus_config_set_loop(bus_config, config->loop);
    
    /* Transport type is now uvbus_transport_type_t, no mapping needed */
    uvbus_config_set_transport(bus_config, config->transport);
    uvbus_config_set_address(bus_config, server->address);
    
    /* Set up receive callback */
    uvbus_config_set_recv_callback(bus_config, server_recv_callback, server);
    
    /* Create UVBus server */
    server->uvbus = uvbus_server_new(bus_config);
    if (!server->uvbus) {
        uvbus_config_free(bus_config);
        uvrpc_free(server->pending_requests);
        uvrpc_free(server->address);
        uvrpc_free(server);
        return NULL;
    }
    
    uvbus_config_free(bus_config);
    
    return server;
}

/* Start server */
int uvrpc_server_start(uvrpc_server_t* server) {
    if (!server || !server->uvbus) return UVRPC_ERROR_INVALID_PARAM;

    if (server->is_running) return UVRPC_OK;

    uvbus_error_t err = uvbus_listen(server->uvbus);
    if (err != UVBUS_OK) {
        /* Map UVBus errors to UVRPC errors */
        switch (err) {
            case UVBUS_ERROR_ALREADY_EXISTS:
                return UVRPC_ERROR_ALREADY_EXISTS;
            case UVBUS_ERROR_NO_MEMORY:
                return UVRPC_ERROR_NO_MEMORY;
            case UVBUS_ERROR_INVALID_PARAM:
                return UVRPC_ERROR_INVALID_PARAM;
            default:
                return UVRPC_ERROR_TRANSPORT;
        }
    }
    
    server->is_running = 1;
    
    printf("Server started on %s\n", server->address);
    
    return UVRPC_OK;
}

/* Stop server */
void uvrpc_server_stop(uvrpc_server_t* server) {
    if (!server) return;
    
    server->is_running = 0;
}

/* Free server */
void uvrpc_server_free(uvrpc_server_t* server) {
    if (!server) return;
    
    uvrpc_server_stop(server);
    
    /* Free UVBus */
    if (server->uvbus) {
        uvbus_free(server->uvbus);
    }
    
    /* Free handlers */
    handler_entry_t* entry, *tmp;
    HASH_ITER(hh, server->handlers, entry, tmp) {
        HASH_DEL(server->handlers, entry);
        uvrpc_free(entry->name);
        uvrpc_free(entry);
    }
    
    /* Free pending requests ring buffer */
    if (server->pending_requests) {
        for (int i = 0; i < server->max_pending_requests; i++) {
            if (server->pending_requests[i]) {
                uvrpc_free(server->pending_requests[i]);
            }
        }
        uvrpc_free(server->pending_requests);
    }

    uvrpc_free(server->address);
    uvrpc_free(server);
}

/* Register handler */
int uvrpc_server_register(uvrpc_server_t* server, const char* method,
                          uvrpc_handler_t handler, void* ctx) {
    if (!server || !method || !handler) return UVRPC_ERROR_INVALID_PARAM;
    
    
    /* Check if handler already exists */
    handler_entry_t* entry = NULL;
    HASH_FIND_STR(server->handlers, method, entry);
    if (entry) {
        return UVRPC_ERROR; /* Handler already registered */
    }
    
    /* Create new entry */
    entry = uvrpc_calloc(1, sizeof(handler_entry_t));
    if (!entry) return UVRPC_ERROR_NO_MEMORY;

    /* Create lowercase copy for case-insensitive lookup */
    char* method_lower = uvrpc_strdup(method);
    if (!method_lower) {
        uvrpc_free(entry);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    for (char* p = method_lower; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
    
    entry->name = method_lower;  /* Store lowercase name in hash table */
    entry->handler = handler;
    entry->ctx = ctx;
    
    HASH_ADD_STR(server->handlers, name, entry);
    
    return UVRPC_OK;
}

/* Set context */
void uvrpc_server_set_context(uvrpc_server_t* server, uvrpc_context_t* ctx) {
    if (server) {
        server->ctx = ctx;
    }
}

/* Get context */
uvrpc_context_t* uvrpc_server_get_context(uvrpc_server_t* server) {
    return server ? server->ctx : NULL;
}

/* Send response */
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size) {
    static int send_count = 0;
    send_count++;

    if (!req || !req->server || !req->client_ctx) {
        fprintf(stderr, "[SERVER] Invalid request or client_ctx is NULL (req=%p, server=%p, client_ctx=%p)\n",
                req, req ? req->server : NULL, req ? req->client_ctx : NULL);
        return;
    }

    uvrpc_server_t* server = req->server;

    /* Encode response */
    uint8_t* resp_data = NULL;
    size_t resp_size = 0;

    if (uvrpc_encode_response(req->msgid, result, result_size,
                              &resp_data, &resp_size) == UVRPC_OK) {
        /* Send response via UVBus */
        uvbus_t* uvbus = server->uvbus;
        uvbus_error_t err = uvbus_send_to(uvbus, resp_data, resp_size, req->client_ctx);
        if (err == UVBUS_OK) {
            server->total_responses++;
            if (send_count % 1000 == 0) {
                fprintf(stderr, "[SERVER] Sent %d responses, client_ctx=%p\n", send_count, req->client_ctx);
            }
        } else {
            UVRPC_ERROR("Failed to send response: %d (client_ctx=%p)", err, req->client_ctx);
        }
        uvrpc_free(resp_data);
    }
    
    /* Note: client_ctx is managed by the transport layer, not freed here
     * TCP: client_ctx is a long-lived client structure
     * UDP: client_ctx is a pointer to the address in the receive buffer
     * IPC: client_ctx is a long-lived client structure
     * INPROC: client_ctx is a long-lived endpoint structure
     */
}

/* Free request */
void uvrpc_request_free(uvrpc_request_t* req) {
    if (!req) return;
    /* Note: method and params point to frame data, don't free them here */
}

/* Send response */
int uvrpc_response_send(uvrpc_request_t* req, const uint8_t* result, size_t result_size) {
    if (!req || !req->server || !req->client_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Encode response */
    uint8_t* response_data;
    size_t response_size;

    if (uvrpc_encode_response(req->msgid, result, result_size,
                              &response_data, &response_size) != UVRPC_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    /* Send response via UVBus */
    uvbus_t* uvbus = req->server->uvbus;
    uvbus_error_t err = uvbus_send_to(uvbus, response_data,
                                       response_size, req->client_ctx);

    uvrpc_free(response_data);

    if (err != UVBUS_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    req->server->total_responses++;
    
    /* Note: client_ctx is managed by the transport layer, not freed here */

    return UVRPC_OK;
}

/* Send error response */
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code, const char* error_message) {
    if (!req || !req->server || !req->client_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Encode error in response data payload: code + message */
    size_t error_msg_len = error_message ? strlen(error_message) : 0;
    size_t payload_size = sizeof(int32_t) + error_msg_len;
    uint8_t* payload = uvrpc_alloc(payload_size);
    if (!payload) {
        return UVRPC_ERROR_TRANSPORT;
    }

    memcpy(payload, &error_code, sizeof(int32_t));
    if (error_message && error_msg_len > 0) {
        memcpy(payload + sizeof(int32_t), error_message, error_msg_len);
    }

    /* Encode response with error payload */
    uint8_t* response_data;
    size_t response_size;

    int ret = uvrpc_encode_response(req->msgid, payload, payload_size,
                                     &response_data, &response_size);
    uvrpc_free(payload);

    if (ret != UVRPC_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    /* Send response via UVBus */
    uvbus_t* uvbus = req->server->uvbus;
    uvbus_error_t err = uvbus_send_to(uvbus, response_data,
                                       response_size, req->client_ctx);

    uvrpc_free(response_data);

    if (err != UVBUS_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    req->server->total_responses++;
    
    /* Note: client_ctx is managed by the transport layer, not freed here */

    return UVRPC_OK;
}

/* Send streaming chunk - now implemented as multiple Response frames */
int uvrpc_response_send_stream(uvrpc_request_t* req, const uint8_t* chunk, 
                                size_t chunk_size, int is_last) {
    if (!req || !req->server || !req->client_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Streaming is now implemented using multiple Response frames.
     * The caller should use uvrpc_response_send() for each chunk.
     * The is_last flag is handled by the application protocol.
     */
    return uvrpc_response_send(req, chunk, chunk_size);
}