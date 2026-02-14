/**
 * UVRPC Async Server
 * Zero threads, Zero locks, Zero global variables
 * All I/O managed by libuv event loop
 */

#include "../include/uvrpc.h"
#include "uvrpc_flatbuffers.h"
#include "uvrpc_transport.h"
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
            free(req->data);
        }
        free(req);
    }
}

/* Handler registry */
typedef struct handler_entry {
    char* name;
    uvrpc_handler_t handler;
    void* ctx;
    UT_hash_handle hh;
} handler_entry_t;

/* Pending request */
typedef struct pending_request {
    uint64_t msgid;
    char* method;
    uint8_t* params;
    size_t params_size;
    uv_stream_t* client;
    struct pending_request* next;
} pending_request_t;

/* Server structure */
struct uvrpc_server {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_t* transport;
    handler_entry_t* handlers;
    int is_running;
    
    /* Pending requests queue */
    pending_request_t* pending_requests;
};

/* Transport receive callback */
static void server_recv_callback(uint8_t* data, size_t size, void* ctx) {
    uv_stream_t* client_stream = (uv_stream_t*)ctx;
    
    /* Get client connection from stream data */
    typedef struct {
        union {
            uv_tcp_t tcp_handle;
            uv_pipe_t pipe_handle;
        } handle;
        int is_tcp;
        struct { } *next;
        uint8_t read_buffer[65536];
        size_t read_pos;
        void (*recv_cb)(uint8_t*, size_t, void*);
        void* recv_ctx;
        void* server;
    } client_connection_t;
    
    client_connection_t* conn = (client_connection_t*)client_stream->data;
    uvrpc_server_t* server = (uvrpc_server_t*)conn->server;
    
    /* Decode request */
    uint32_t msgid;
    char* method = NULL;
    const uint8_t* params = NULL;
    size_t params_size = 0;
    
    if (uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size) != UVRPC_OK) {
        return;
    }
    
    /* Find handler */
    handler_entry_t* entry = NULL;
    HASH_FIND_STR(server->handlers, method, entry);
    
    if (entry && entry->handler) {
        /* Create request structure */
        uvrpc_request_t req;
        req.server = server;
        req.msgid = msgid;
        req.method = method;
        req.params = (uint8_t*)params;
        req.params_size = params_size;
        req.client_stream = client_stream;  /* Set client stream for response */
        req.user_data = NULL;
        
        /* Call handler */
        entry->handler(&req, entry->ctx);
        
        /* Free decoded method */
        if (method) free(method);
    } else {
        /* Handler not found, send error response */
        fprintf(stderr, "Handler not found: '%s'\n", method);
        uint8_t* resp_data = NULL;
        size_t resp_size = 0;
        
        uvrpc_encode_response(msgid, 6, NULL, 0, &resp_data, &resp_size);
        
        if (resp_data && client_stream) {
            /* Send error response to client */
            size_t total_size = 4 + resp_size;
            uint8_t* buffer = malloc(total_size);
            if (buffer) {
                buffer[0] = (resp_size >> 24) & 0xFF;
                buffer[1] = (resp_size >> 16) & 0xFF;
                buffer[2] = (resp_size >> 8) & 0xFF;
                buffer[3] = resp_size & 0xFF;
                memcpy(buffer + 4, resp_data, resp_size);
                
                uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
                uv_write_t* write_req = malloc(sizeof(uv_write_t));
                if (write_req) {
                    write_req->data = buffer;
                    uv_write(write_req, client_stream, &buf, 1, write_callback);
                } else {
                    free(buffer);
                }
            }
            free(resp_data);
        }
        
        if (method) free(method);
    }
}

/* Create server */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    uvrpc_server_t* server = calloc(1, sizeof(uvrpc_server_t));
    if (!server) return NULL;
    
    server->loop = config->loop;
    server->address = strdup(config->address);
    server->handlers = NULL;
    server->is_running = 0;
    server->pending_requests = NULL;
    
    /* Create transport with specified type */
    server->transport = uvrpc_transport_server_new(config->loop, config->transport);
    if (!server->transport) {
        free(server->address);
        free(server);
        return NULL;
    }
    
    return server;
}

/* Start server */
int uvrpc_server_start(uvrpc_server_t* server) {
    if (!server || !server->transport) return UVRPC_ERROR_INVALID_PARAM;
    
    if (server->is_running) return UVRPC_OK;
    
    int rv = uvrpc_transport_listen(server->transport, server->address,
                                     server_recv_callback, server);
    if (rv != UVRPC_OK) return rv;
    
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
    
    /* Free transport */
    if (server->transport) {
        uvrpc_transport_free(server->transport);
    }
    
    /* Free handlers */
    handler_entry_t* entry, *tmp;
    HASH_ITER(hh, server->handlers, entry, tmp) {
        HASH_DEL(server->handlers, entry);
        free(entry->name);
        free(entry);
    }
    
    /* Free pending requests */
    pending_request_t* req = server->pending_requests;
    while (req) {
        pending_request_t* next = req->next;
        if (req->method) free(req->method);
        if (req->params) free(req->params);
        free(req);
        req = next;
    }
    
    free(server->address);
    free(server);
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
    entry = calloc(1, sizeof(handler_entry_t));
    if (!entry) return UVRPC_ERROR_NO_MEMORY;
    
    entry->name = strdup(method);
    if (!entry->name) {
        free(entry);
        return UVRPC_ERROR_NO_MEMORY;
    }
    entry->handler = handler;
    entry->ctx = ctx;
    
    HASH_ADD_STR(server->handlers, name, entry);
    
    
    /* Verify registration */
    handler_entry_t* verify = NULL;
    HASH_FIND_STR(server->handlers, method, verify);
    
    return UVRPC_OK;
}

/* Send response */
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size) {
    if (!req || !req->server) return;
    
    uvrpc_server_t* server = req->server;
    
    /* Encode response */
    uint8_t* resp_data = NULL;
    size_t resp_size = 0;
    
    int32_t error_code = (status == UVRPC_OK) ? 0 : 1;
    
    if (uvrpc_encode_response(req->msgid, error_code, result, result_size,
                              &resp_data, &resp_size) == UVRPC_OK) {
        /* Send response to client stream */
        if (req->client_stream) {
            /* Allocate buffer with 4-byte length prefix */
            size_t total_size = 4 + resp_size;
            uint8_t* buffer = malloc(total_size);
            if (buffer) {
                buffer[0] = (resp_size >> 24) & 0xFF;
                buffer[1] = (resp_size >> 16) & 0xFF;
                buffer[2] = (resp_size >> 8) & 0xFF;
                buffer[3] = resp_size & 0xFF;
                memcpy(buffer + 4, resp_data, resp_size);
                
                uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
                uv_write_t* write_req = malloc(sizeof(uv_write_t));
                if (write_req) {
                    write_req->data = buffer;  /* Store buffer for cleanup */
                    uv_write(write_req, req->client_stream, &buf, 1, write_callback);
                } else {
                    fprintf(stderr, "Failed to allocate write request for error response\n");
                    free(buffer);
                }
            } else {
            }
        } else {
            fprintf(stderr, "Cannot send response: no client stream\n");
        }
        free(resp_data);
    } else {
    }
}

/* Free request */
void uvrpc_request_free(uvrpc_request_t* req) {
    if (!req) return;
    /* Note: method and params point to frame data, don't free them here */
}