/**
 * UVRPC Async Server
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include "uvrpc_flatbuffers.h"
#include "uvrpc_transport.h"
#include "uvrpc_transport_internal.h"
#include "uvrpc_msgid.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* Handler registry */
typedef struct handler_entry {
    char* name;
    uvrpc_handler_t handler;
    void* ctx;
    UT_hash_handle hh;  /* uthash handle */
} handler_entry_t;

/* Pending request - for ring buffer */
typedef struct pending_request {
    uint32_t msgid;              /* Message ID */
    uint32_t generation;         /* Generation counter */
    uv_stream_t* client_stream;  /* Client stream for response */
    int in_use;                  /* Flag to indicate if slot is in use */
} pending_request_t;

/* Server structure */
struct uvrpc_server {
    uv_loop_t* loop;
    char* address;
    uvbus_t* uvbus;
    handler_entry_t* handlers;
    int is_running;
    
    /* Pending requests ring buffer */
    pending_request_t** pending_requests;
    int max_pending_requests;    /* Ring buffer size (must be power of 2) */
    uint32_t generation;         /* Generation counter */
    
    /* Statistics */
    uint64_t total_requests;
    uint64_t total_responses;
};

/* Server receive callback */
static void server_recv_callback(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    uvrpc_server_t* server = (uvrpc_server_t*)server_ctx;

    if (!server) return;

    /* Decode request */
    uint32_t msgid = 0;
    char* method = NULL;
    const uint8_t* params = NULL;
    size_t params_size = 0;

    if (uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size) != UVRPC_OK) {
        UVRPC_LOG("Failed to decode request (size=%zu)", size);
        return;
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
        
        /* Create request structure */
        uvrpc_request_t req;
        req.server = server;
        req.msgid = msgid;
        req.method = method;
        req.params = (uint8_t*)params;
        req.params_size = params_size;
        req.client_ctx = client_ctx;
        req.user_data = NULL;
        
        fprintf(stderr, "[SERVER] Calling handler for method='%s', params_size=%zu, handler=%p\n",
                method, params_size, entry->handler);
        fflush(stderr);
        
        /* Call handler */
        entry->handler(&req, entry->ctx);
        
        fprintf(stderr, "[SERVER] Handler returned\n");
        fflush(stderr);

        /* Free decoded method */
        if (method) uvrpc_free(method);
    } else {
        /* Handler not found, send error response */
        fprintf(stderr, "Handler not found: '%s'\n", method);
        uint8_t* resp_data = NULL;
        size_t resp_size = 0;

        uvrpc_encode_error(msgid, 2, "Method not found", &resp_data, &resp_size);

        if (resp_data) {
            uvrpc_free(resp_data);
        }

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
                                   config->max_pending_callbacks : UVRPC_MAX_PENDING_CALLBACKS;
    server->generation = 0;
    
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
            uvrpc_free(server->pending_requests);
            uvrpc_free(server->address);
            uvrpc_free(server);
            return NULL;
    }
    
    uvbus_config_set_transport(bus_config, uvbus_type);
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
        return UVRPC_ERROR_TRANSPORT;
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

/* Send response */
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size) {
    if (!req || !req->server || !req->client_ctx) return;

    uvrpc_server_t* server = req->server;

    /* Encode response */
    uint8_t* resp_data = NULL;
    size_t resp_size = 0;

    if (uvrpc_encode_response(req->msgid, result, result_size,
                              &resp_data, &resp_size) == UVRPC_OK) {
        /* Send response via UVBus */
        uvbus_t* uvbus = server->uvbus;
        if (uvbus && uvbus_send_to(uvbus, resp_data, resp_size, req->client_ctx) == UVBUS_OK) {
            server->total_responses++;
        }
        uvrpc_free(resp_data);
    }
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
    return UVRPC_OK;
}

/* Send error response */
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code, const char* error_message) {
    if (!req || !req->server || !req->client_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Encode error response */
    uint8_t* response_data;
    size_t response_size;

    if (uvrpc_encode_error(req->msgid, error_code, error_message,
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
    return UVRPC_OK;
}