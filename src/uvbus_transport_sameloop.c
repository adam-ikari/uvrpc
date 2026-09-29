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
#include "uvbus_loop_registry.h"
#include <string.h>
#include <stdlib.h>

/* ========================================
 * Data Structures
 * ======================================== */

/* Max clients per sameloop server (fixed-size array, no heap alloc) */
#define SAMELOOP_MAX_CLIENTS 64

/* Server registry entry - stored in the per-loop registry (uvbus_loop_registry_t)
 * so server and client find each other by name with no file-scope global. */
typedef struct sameloop_server {
    char* name;
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
    uv_loop_t* loop;
    struct sameloop_server* next;  /* Linked list within the per-loop registry */
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
    sameloop_server_t* server;  /* Back-pointer: avoids scanning the registry on disconnect */
} __attribute__((aligned(64))) sameloop_client_t;  /* Cache line aligned */

/* Forward declarations */
static sameloop_server_t* find_server(uvbus_loop_registry_t* reg, const char* name);
static void add_server(uvbus_loop_registry_t* reg, sameloop_server_t* server);
static void remove_server(uvbus_loop_registry_t* reg, sameloop_server_t* server);
static int sameloop_broadcast(void* impl_ptr, const uint8_t* data, size_t size);

/* ========================================
 * SAMELOOP Transport Implementation
 * ======================================== */

/* Find server by name in the per-loop registry (no lock: single-threaded). */
static sameloop_server_t* find_server(uvbus_loop_registry_t* reg, const char* name) {
    if (!reg) return NULL;
    sameloop_server_t* server = (sameloop_server_t*)reg->sameloop_servers;
    while (server) {
        if (strcmp(server->name, name) == 0) {
            return server;
        }
        server = server->next;
    }
    return NULL;
}

/* Add server to the per-loop registry (no lock). */
static void add_server(uvbus_loop_registry_t* reg, sameloop_server_t* server) {
    server->next = (sameloop_server_t*)reg->sameloop_servers;
    reg->sameloop_servers = server;
}

/* Remove server from the per-loop registry (no lock). */
static void remove_server(uvbus_loop_registry_t* reg, sameloop_server_t* server) {
    sameloop_server_t** ptr = (sameloop_server_t**)&reg->sameloop_servers;
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

    /* The registry is supplied by the caller; there is no hidden per-loop or
     * process-wide state to fall back to, so a missing one is a usage error
     * worth naming. */
    if (!transport->registry) {
        UVBUS_LOG_ERROR("sameloop needs a registry: create one with "
                        "uvbus_loop_registry_new() and pass it to every config "
                        "that should see each other via "
                        "uvbus_config_set_loop_registry()");
        return UVBUS_ERROR_INVALID_PARAM;
    }
    uvbus_loop_registry_t* reg = uvbus_loop_registry_retain(transport->registry);
    if (!reg) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Check if server already exists */
    sameloop_server_t* existing = find_server(reg, address);
    if (existing) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_ALREADY_EXISTS;
    }

    /* Create server */
    sameloop_server_t* server = (sameloop_server_t*)uvrpc_alloc(sizeof(sameloop_server_t));
    if (!server) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NO_MEMORY;
    }

    server->name = uvrpc_strdup(address);
    if (!server->name) {
        uvrpc_free(server);
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NO_MEMORY;
    }

    server->recv_cb = transport->recv_cb;
    server->callback_ctx = transport->recv_ctx;
    server->loop = transport->loop;
    server->client_count = 0;
    server->next = NULL;

    /* Add to per-loop registry */
    add_server(reg, server);

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

    /* Obtain the per-loop registry (retained; released in sameloop_disconnect). */
    uvbus_loop_registry_t* reg = uvbus_loop_registry_retain(transport->registry);
    if (!reg) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Find server in registry */
    sameloop_server_t* server = find_server(reg, address);
    if (!server) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NOT_FOUND;
    }

    /* Allocate client inline in transport impl to avoid separate heap allocation */
    transport->impl.sameloop_client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!transport->impl.sameloop_client) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Inline server callbacks to avoid pointer indirection */
    sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
    client->server_recv_cb = server->recv_cb;
    client->server_callback_ctx = server->callback_ctx;
    client->server = server;  /* back-pointer for O(1) unregister on disconnect */

    client->recv_cb = transport->recv_cb;
    client->callback_ctx = transport->recv_ctx;

    transport->is_connected = 1;

    /* Register client with server for broadcast support */
    if (server->client_count >= SAMELOOP_MAX_CLIENTS) {
        uvrpc_free(client);
        transport->impl.sameloop_client = NULL;
        uvbus_loop_registry_release(transport->registry);
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

    uvbus_loop_registry_t* reg = transport->registry;

    if (transport->is_server && transport->impl.sameloop_server) {
        sameloop_server_t* server = (sameloop_server_t*)(void*)transport->impl.sameloop_server;
        if (reg) {
            remove_server(reg, server);
        }
        /* Clear the back-pointer on every connected client so a later client
         * disconnect does not dereference this server (which we are about to
         * free). The client transports themselves remain owned by their buses. */
        for (int i = 0; i < server->client_count; i++) {
            sameloop_client_t* client = (sameloop_client_t*)server->clients[i];
            if (client) {
                client->server = NULL;
            }
            server->clients[i] = NULL;
        }
        server->client_count = 0;
        uvrpc_free(server->name);
        uvrpc_free(server);
        transport->impl.sameloop_server = NULL;
        /* Release the per-loop registry reference taken in listen. */
        uvbus_loop_registry_release(transport->registry);
    } else if (!transport->is_server && transport->impl.sameloop_client) {
        sameloop_client_t* client = (sameloop_client_t*)(void*)transport->impl.sameloop_client;

        /* Unregister client from its server via the back-pointer (no registry scan). */
        sameloop_server_t* server = client->server;
        if (server) {
            for (int i = 0; i < server->client_count; i++) {
                if (server->clients[i] == client) {
                    /* Swap with last and shrink */
                    server->clients[i] = server->clients[server->client_count - 1];
                    server->client_count--;
                    break;
                }
            }
        }

        uvrpc_free(client);
        transport->impl.sameloop_client = NULL;
        /* Release the per-loop registry reference taken in connect. */
        uvbus_loop_registry_release(transport->registry);
    }

    transport->is_connected = 0;
}

/* Send to all connected clients of a server, or client->server for a client.
 * Shared by send() and broadcast(), which are the same operation. */
static int sameloop_send_to_all_clients(uvbus_transport_t* transport, const uint8_t* data, size_t size) {
    if (!transport->is_server || !transport->impl.sameloop_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    sameloop_server_t* server = (sameloop_server_t*)(void*)transport->impl.sameloop_server;
    for (int i = 0; i < server->client_count; i++) {
        sameloop_client_t* client = (sameloop_client_t*)server->clients[i];
        if (client && client->recv_cb) {
            client->recv_cb(data, size, client->callback_ctx, client->callback_ctx);
        }
    }
    return UVBUS_OK;
}

/* Send - client to server (inline dispatch for performance)
 *
 * A server sending means "to every one of its clients", which is what
 * broadcast() does and what the INPROC transport already does for send(). Only
 * the client direction is inlined hot: a server bus has no sameloop_client, so
 * taking the fast path unconditionally dereferenced NULL -- the two in-process
 * transports disagreed about the same call, and the disagreement was a
 * segfault. */
__attribute__((hot, always_inline)) static inline int sameloop_send_inline(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;

    if (__builtin_expect(!!transport->is_server, 0)) {
        return sameloop_send_to_all_clients(transport, data, size);
    }

    /* Fast path - client send (most common case) */
    if (__builtin_expect(!!transport->is_connected, 1)) {
        /* Prefetch client data to reduce cache miss */
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
        if (__builtin_expect(!!client, 0)) {
            __builtin_prefetch(client);
            __builtin_prefetch(&client->server_recv_cb);

            /* Direct callback - minimize indirection */
            client->server_recv_cb(data, size, client, client->server_callback_ctx);

            return UVBUS_OK;
        }
    }

    return UVBUS_ERROR_NOT_CONNECTED;
}

/* Wrapper function for non-inlined calls */
__attribute__((hot)) int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    return sameloop_send_inline(impl_ptr, data, size);
}

/* Send to specific client - from server to client */
__attribute__((hot, always_inline)) static inline int sameloop_send_to_inline(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    (void)impl_ptr;  /* Client context (target) carries everything needed */
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
    return sameloop_send_to_all_clients(transport, data, size);
}

/* Free implementation */
static void sameloop_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) return;
    /* Disconnect unconditionally so the per-loop registry reference and any
     * registered server/client entries are released even if the bus was never
     * marked active. sameloop_disconnect is a no-op when impl is NULL. */
    sameloop_disconnect(transport);

    if (transport->address) {
        uvrpc_free(transport->address);
    }
    uvrpc_free(transport);
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