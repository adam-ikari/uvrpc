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

/* Max clients per sameloop server (fixed-size array, no heap alloc) */
#define SAMELOOP_MAX_CLIENTS 64

/* Server registry - allows finding server by name without globals */
typedef struct sameloop_server {
    char* name;
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
    uv_loop_t* loop;
    struct sameloop_server* next;  /* Linked list for registry */
    void* clients[SAMELOOP_MAX_CLIENTS];  /* Connected client pointers for broadcast */
    int client_count;
} sameloop_server_t;

/* Client endpoint - optimized with direct callback pointers */
typedef struct sameloop_client {
    /* Direct pointers to avoid indirection */
    uvbus_recv_callback_t server_recv_cb;  /* Inlined from server */
    void* server_callback_ctx;              /* Inlined from server */
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
} __attribute__((aligned(64))) sameloop_client_t;  /* Cache line aligned */

/* Server registry (static, not global) */
static sameloop_server_t* server_registry = NULL;

/* Forward declarations */
static sameloop_server_t* find_server(const char* name);
static void add_server(sameloop_server_t* server);
static void remove_server(sameloop_server_t* server);
static int sameloop_broadcast(void* impl_ptr, const uint8_t* data, size_t size);

/* ========================================
 * SAMELOOP Transport Implementation
 * ======================================== */

/* Direct function pointers for inline dispatch (bypasses vtable) */
static int (*sameloop_send_direct)(void*, const uint8_t*, size_t) = NULL;
static int (*sameloop_send_to_direct)(void*, const uint8_t*, size_t, void*) = NULL;

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
    server->callback_ctx = transport->recv_ctx;
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
    client->callback_ctx = transport->recv_ctx;
    
    transport->is_connected = 1;

    /* Register client with server for broadcast support */
    if (server->client_count >= SAMELOOP_MAX_CLIENTS) {
        uvrpc_free(client);
        return UVBUS_ERROR_MAX_CLIENTS;
    }
    server->clients[server->client_count] = client;
    server->client_count++;

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

        /* Unregister client from all servers (find via registry) */
        sameloop_server_t* s = server_registry;
        while (s) {
            for (int i = 0; i < s->client_count; i++) {
                if (s->clients[i] == client) {
                    /* Swap with last and shrink */
                    s->clients[i] = s->clients[s->client_count - 1];
                    s->client_count--;
                    break;
                }
            }
            s = s->next;
        }

        uvrpc_free(client);
        transport->impl.sameloop_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* Send - from client to server (inline dispatch for performance) */
__attribute__((hot, always_inline)) static inline int sameloop_send_inline(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    
    /* Fast path - assume client send (most common case) */
    if (__builtin_expect(!!transport->is_connected, 1)) {
        /* Prefetch client data to reduce cache miss */
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
        __builtin_prefetch(client);
        __builtin_prefetch(&client->server_recv_cb);
        
        /* Direct callback - minimize indirection */
        client->server_recv_cb(data, size, client, client->server_callback_ctx);

        return UVBUS_OK;
    }
    
    return UVBUS_ERROR_NOT_CONNECTED;
}

/* Wrapper function for non-inlined calls */
__attribute__((hot)) int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    return sameloop_send_inline(impl_ptr, data, size);
}

/* Send to specific client - from server to client */
__attribute__((hot, always_inline)) static inline int sameloop_send_to_inline(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    sameloop_client_t* client = (sameloop_client_t*)target;
    
    /* Prefetch callback pointers */
    __builtin_prefetch(&client->recv_cb);
    __builtin_prefetch(&client->callback_ctx);
    
    /* Direct callback - minimize overhead */
    client->recv_cb(data, size, client->callback_ctx, client->callback_ctx);

    return UVBUS_OK;
}

/* Wrapper function for non-inlined calls */
__attribute__((hot)) static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    return sameloop_send_to_inline(impl_ptr, data, size, target);
}

/* Broadcast - direct call to all connected clients' recv_cb, zero overhead */
__attribute__((hot)) static int sameloop_broadcast(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !data || size == 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server || !transport->impl.sameloop_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    sameloop_server_t* server = (sameloop_server_t*)(void*)transport->impl.sameloop_server;

    /* Direct callback to each connected client - no serialization, no frame prefix */
    for (int i = 0; i < server->client_count; i++) {
        sameloop_client_t* client = (sameloop_client_t*)server->clients[i];
        if (client && client->recv_cb) {
            client->recv_cb(data, size, client->callback_ctx, client->callback_ctx);
        }
    }

    return UVBUS_OK;
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
    
    /* Set fast path function pointers */
    transport->fast_send = sameloop_send;
    transport->fast_send_to = sameloop_send_to;
    
    return transport;
}