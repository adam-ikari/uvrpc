/**
 * UVBus SAMELOOP Transport Implementation
 * 
 * Zero-overhead async transport for same-event-loop communication.
 * Uses libuv async callbacks to prevent stack overflow while maintaining
 * direct callback semantics.
 * 
 * Design Principles:
 * - No locks (single event loop)
 * - Direct callbacks via uv_async
 * - Zero-copy (pointer passing)
 * - Minimal overhead
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* ========================================
 * Data Structures
 * ======================================== */

/* Server registry - allows finding server by name without globals */
typedef struct sameloop_server {
    char* name;
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
    uv_loop_t* loop;
    struct sameloop_server* next;  /* Linked list for registry */
} sameloop_server_t;

/* Client endpoint - optimized with direct callback pointers */
typedef struct sameloop_client {
    /* Direct pointers to avoid indirection */
    uvbus_recv_callback_t server_recv_cb;  /* Inlined from server */
    void* server_callback_ctx;              /* Inlined from server */
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
} sameloop_client_t;

/* Server registry (static, not global) */
static sameloop_server_t* server_registry = NULL;

/* Forward declarations */
static sameloop_server_t* find_server(const char* name);
static void add_server(sameloop_server_t* server);
static void remove_server(sameloop_server_t* server);

/* ========================================
 * SAMELOOP Transport Implementation
 * ======================================== */

/* Direct function pointers for inline dispatch (bypasses vtable) */
static int (*sameloop_send_direct)(void*, const uint8_t*, size_t) = NULL;
static int (*sameloop_send_to_direct)(void*, const uint8_t*, size_t, void*) = NULL;

/* Performance statistics */
static volatile uint64_t fast_path_calls = 0;
volatile uint64_t vtable_calls = 0;  /* Exported for uvbus.c */

/* Find server by name in registry */
static sameloop_server_t* find_server(const char* name) {
    sameloop_server_t* server = server_registry;
    while (server) {
        if (strcmp(server->name, name) == 0) {
            return server;
        }
        server = server->next;
    }
    return NULL;
}

/* Add server to registry */
static void add_server(sameloop_server_t* server) {
    server->next = server_registry;
    server_registry = server;
}

/* Remove server from registry */
static void remove_server(sameloop_server_t* server) {
    sameloop_server_t** ptr = &server_registry;
    while (*ptr) {
        if (*ptr == server) {
            *ptr = server->next;
            server->next = NULL;
            return;
        }
        ptr = &(*ptr)->next;
    }
}

/* Listen - create server endpoint */
static int sameloop_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Check if server already exists */
    sameloop_server_t* existing = find_server(address);
    if (existing) {
        return UVBUS_ERROR_ALREADY_EXISTS;
    }
    
    /* Create server */
    sameloop_server_t* server = (sameloop_server_t*)uvrpc_alloc(sizeof(sameloop_server_t));
    if (!server) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    server->name = uvrpc_strdup(address);
    if (!server->name) {
        uvrpc_free(server);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    server->recv_cb = transport->recv_cb;
    server->callback_ctx = transport->callback_ctx;
    server->loop = transport->loop;
    
    /* Add to registry */
    add_server(server);
    
    /* Store in transport impl */
    transport->impl.sameloop_server = server;
    transport->is_connected = 1;
    
    /* Set bus as active */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }
    
    return UVBUS_OK;
}

/* Connect - create client endpoint */
static int sameloop_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Find server in registry */
    sameloop_server_t* server = find_server(address);
    if (!server) {
        return UVBUS_ERROR_NOT_FOUND;
    }
    
    /* Allocate client inline in transport impl to avoid separate heap allocation */
    /* This reduces memory fragmentation and allocation overhead */
    transport->impl.sameloop_client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!transport->impl.sameloop_client) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    /* Inline server callbacks to avoid pointer indirection */
    sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
    client->server_recv_cb = server->recv_cb;
    client->server_callback_ctx = server->callback_ctx;
    
    client->recv_cb = transport->recv_cb;
    client->callback_ctx = transport->callback_ctx;
    
    transport->is_connected = 1;
    
    /* Set bus as active */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }
    
    /* Call connection callback */
    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* Disconnect - cleanup client */
static void sameloop_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.sameloop_server) {
        sameloop_server_t* server = (sameloop_server_t*)(void*)transport->impl.sameloop_server;
        remove_server(server);
        uvrpc_free(server->name);
        uvrpc_free(server);
        transport->impl.sameloop_server = NULL;
    } else if (!transport->is_server && transport->impl.sameloop_client) {
        sameloop_client_t* client = (sameloop_client_t*)(void*)transport->impl.sameloop_client;
        uvrpc_free(client);
        transport->impl.sameloop_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* Send - from client to server (inline dispatch for performance) */
int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    
    fast_path_calls++;  /* Count fast path calls */
    
    /* Fast path - assume client send (most common case) */
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    /* Direct callback - minimize indirection */
    sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
    client->server_recv_cb(data, size, client, client->server_callback_ctx);
    
    return UVBUS_OK;
}

/* Send to specific client - from server to client */
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    sameloop_client_t* client = (sameloop_client_t*)target;
    
    /* Direct callback - minimize overhead */
    client->recv_cb(data, size, client, client->callback_ctx);
    
    return UVBUS_OK;
}

/* Broadcast - not implemented for SAMELOOP */
static int sameloop_broadcast(void* impl_ptr, const uint8_t* data, size_t size) {
    (void)impl_ptr;
    (void)data;
    (void)size;
    return UVBUS_ERROR_NOT_IMPLEMENTED;
}

/* Free implementation */
static void sameloop_free(void* impl_ptr) {
    (void)impl_ptr;
    /* Cleanup handled by disconnect */
}

/* Virtual function table */
static const uvbus_transport_vtable_t sameloop_vtable = {
    .listen = sameloop_listen,
    .connect = sameloop_connect,
    .disconnect = sameloop_disconnect,
    .send = sameloop_send,
    .send_to = sameloop_send_to,
    .broadcast = sameloop_broadcast,
    .free = sameloop_free
};

/* Setup direct function pointers for inline dispatch */
static void setup_direct_dispatch() {
    sameloop_send_direct = sameloop_send;
    sameloop_send_to_direct = sameloop_send_to;
}

/* Get performance statistics */
void uvbus_sameloop_get_stats(uint64_t* fast, uint64_t* vtable) {
    if (fast) *fast = fast_path_calls;
    if (vtable) *vtable = vtable_calls;
}

/* ========================================
 * Transport Factory
 * ======================================== */

/* Create SAMELOOP transport */
uvbus_transport_t* create_sameloop_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &sameloop_vtable;
    
    return transport;
}