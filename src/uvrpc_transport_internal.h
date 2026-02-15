/**
 * UVRPC Transport Layer Internal Interface
 * Strategy pattern implementation for multi-protocol support
 */

#ifndef UVRPC_TRANSPORT_INTERNAL_H
#define UVRPC_TRANSPORT_INTERNAL_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../deps/uthash/include/uthash.h"
#include "../include/uvrpc_allocator.h"
#include "../include/uvrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct uvrpc_transport uvrpc_transport_t;
typedef struct client_connection client_connection_t;
typedef struct udp_peer udp_peer_t;
typedef struct inproc_virtual_stream inproc_virtual_stream_t;
typedef struct inproc_endpoint inproc_endpoint_t;
typedef struct inproc_registry inproc_registry_t;

/* Complete transport structure definition for transport implementations */
struct uvrpc_transport {
    uv_loop_t* loop;
    int type;

    /* Address (for INPROC) */
    char* address;

    /* Client connection tracking (for TCP/IPC servers) */
    void* client_connections;
    
    /* Transport implementation */
    void* impl;
    
    /* Virtual function table */
    const void* vtable;
    
    /* Callbacks */
    void (*recv_cb)(uint8_t*, size_t, void*);
    void (*connect_cb)(int, void*);
    void (*error_cb)(int, const char*, void*);
    void* ctx;
    
    /* Flags */
    int is_server;
    int is_connected;
    
    /* Timeout */
    uint64_t timeout_ms;
    int timeout_enabled;
    
    /* TCP handles */
    uv_tcp_t tcp_handle;
    uv_tcp_t listen_handle;
    uv_connect_t connect_req;
    
    /* UDP handles */
    uv_udp_t udp_handle;
    
    /* IPC handles */
    uv_pipe_t pipe_handle;
    uv_pipe_t listen_pipe;
    
    /* Async handle for triggering callbacks */
    uv_async_t async_handle;

    /* Timeout timer */
    uv_timer_t timeout_timer;
    
    /* Read buffer */
    uint8_t read_buffer[8192];
    size_t read_pos;

    /* UDP peer address tracking */
    struct udp_peer* udp_peers;
};

typedef struct uvrpc_transport uvrpc_transport_t;

/* Callback types (same as public API) */
typedef void (*uvrpc_recv_callback_t)(uint8_t* data, size_t size, void* ctx);
typedef void (*uvrpc_connect_callback_t)(int status, void* ctx);
typedef void (*uvrpc_error_callback_t)(uvrpc_error_t error_code, const char* error_msg, void* ctx);

/* Transport virtual function table */
typedef struct uvrpc_transport_vtable {
    /* Server operations */
    int (*listen)(void* impl, const char* address,
                  uvrpc_recv_callback_t recv_cb, void* ctx);

    /* Client operations */
    int (*connect)(void* impl, const char* address,
                   uvrpc_connect_callback_t connect_cb,
                   uvrpc_recv_callback_t recv_cb, void* ctx);
    void (*disconnect)(void* impl);

    /* Send operations */
    void (*send)(void* impl, const uint8_t* data, size_t size);
    void (*send_to)(void* impl, const uint8_t* data, size_t size, void* target);

    /* Cleanup */
    void (*free)(void* impl);

    /* Optional: transport-specific operations */
    int (*set_timeout)(void* impl, uint64_t timeout_ms);
} uvrpc_transport_vtable_t;

/* Common utility structures */

/* Client connection for TCP/IPC servers */
struct client_connection {
    union {
        uv_tcp_t tcp_handle;
        uv_pipe_t pipe_handle;
    } handle;
    int is_tcp;
    struct client_connection* next;

    /* Read buffer */
    uint8_t read_buffer[65536];
    size_t read_pos;

    /* Callbacks */
    uvrpc_recv_callback_t recv_cb;
    void* recv_ctx;

    /* Server reference */
    void* server;
};

/* UDP peer tracking */
struct udp_peer {
    struct sockaddr_storage addr;
    struct udp_peer* next;
};

/* Virtual stream for INPROC */
struct inproc_virtual_stream {
    uv_handle_t handle;
    void* server;  /* Server context */
    void* client_transport;  /* Client transport for sending response */
    int is_active;
};

/* INPROC endpoint */
struct inproc_endpoint {
    char* name;
    void* server_transport;
    void** clients;
    int client_count;
    int client_capacity;
    struct inproc_endpoint* next;  /* Linked list instead of hash table */
};

/* INPRO C utility functions */
struct inproc_endpoint* inproc_find_endpoint(const char* name);
void inproc_add_client(struct inproc_endpoint* endpoint, void* client);
void inproc_remove_client(struct inproc_endpoint* endpoint, void* client);
void inproc_send_to_all(void* sender, struct inproc_endpoint* endpoint,
                         const uint8_t* data, size_t size);

/* Common constants */
#define UVRPC_MAX_FRAME_SIZE (10 * 1024 * 1024)  /* 10MB max frame */
#define UVRPC_MAX_UDP_PEERS 1000  /* Maximum UDP peers */

/* Utility functions (shared across transports) */

/* Parse 4-byte length prefix */
static inline int parse_frame_length(const uint8_t* buffer, size_t buffer_pos, size_t* frame_size) {
    if (buffer_pos < 4) return 0;

    uint32_t len = (buffer[0] << 24) |
                   (buffer[1] << 16) |
                   (buffer[2] << 8) |
                   buffer[3];

    if (len > UVRPC_MAX_FRAME_SIZE) return -1;

    *frame_size = len;
    return 1;
}

/* Create length-prefixed buffer */
static inline uint8_t* create_length_prefixed_buffer(const uint8_t* data, size_t size, size_t* total_size) {
    *total_size = 4 + size;
    uint8_t* buffer = (uint8_t*)uvrpc_alloc(*total_size);
    if (!buffer) return NULL;

    buffer[0] = (size >> 24) & 0xFF;
    buffer[1] = (size >> 16) & 0xFF;
    buffer[2] = (size >> 8) & 0xFF;
    buffer[3] = size & 0xFF;
    memcpy(buffer + 4, data, size);

    return buffer;
}

/* INPRO C utility functions */
struct inproc_endpoint* inproc_find_endpoint(const char* name);
void inproc_add_client(struct inproc_endpoint* endpoint, void* client);
void inproc_remove_client(struct inproc_endpoint* endpoint, void* client);
void inproc_send_to_all(void* sender, struct inproc_endpoint* endpoint,
                         const uint8_t* data, size_t size);
inproc_endpoint_t* uvrpc_inproc_create_endpoint(const char* name);

/* INPROC transport structure definition */
struct uvrpc_inproc_transport {
    uv_loop_t* loop;
    int is_server;

    /* Endpoint info */
    char* address;
    struct inproc_endpoint* endpoint;

    /* Callbacks */
    uvrpc_recv_callback_t recv_cb;
    uvrpc_connect_callback_t connect_cb;
    uvrpc_error_callback_t error_cb;
    void* ctx;

    /* Status */
    int is_connected;
};

/* Protocol-specific creation functions */
uvrpc_transport_t* uvrpc_transport_tcp_new(uv_loop_t* loop, int is_server);
uvrpc_transport_t* uvrpc_transport_ipc_new(uv_loop_t* loop, int is_server);
uvrpc_transport_t* uvrpc_transport_udp_new(uv_loop_t* loop, int is_server);
void* uvrpc_transport_inproc_new(uv_loop_t* loop, int is_server);
inproc_endpoint_t* uvrpc_inproc_create_endpoint(const char* name);

/* INPROC vtable (for transport setup) */
extern const uvrpc_transport_vtable_t inproc_vtable;

/* INPROC implementation type (opaque) */
typedef struct uvrpc_inproc_transport uvrpc_inproc_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_TRANSPORT_INTERNAL_H */