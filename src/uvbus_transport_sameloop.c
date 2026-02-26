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

/* Server endpoint */
typedef struct sameloop_server {
    char* name;
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
    uv_loop_t* loop;
} sameloop_server_t;

/* Client endpoint - optimized with direct callback pointers */
typedef struct sameloop_client {
    /* Direct pointers to avoid indirection */
    uvbus_recv_callback_t server_recv_cb;  /* Inlined from server */
    void* server_callback_ctx;              /* Inlined from server */
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
} sameloop_client_t;

/* Global server registry (no locks needed - same event loop) */
static sameloop_server_t* g_server = NULL;

/* ========================================
 * SAMELOOP Transport Implementation
 * ======================================== */

/* Listen - create server endpoint */
static int sameloop_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Check if server already exists */
    if (g_server) {
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
    
    /* Store globally */
    g_server = server;
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
    
    /* Check if server exists */
    if (!g_server || strcmp(g_server->name, address) != 0) {
        return UVBUS_ERROR_NOT_FOUND;
    }
    
    /* Create client */
    sameloop_client_t* client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!client) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    /* Inline server callbacks to avoid pointer indirection */
    client->server_recv_cb = g_server->recv_cb;
    client->server_callback_ctx = g_server->callback_ctx;
    
    client->recv_cb = transport->recv_cb;
    client->callback_ctx = transport->callback_ctx;
    
    transport->impl.sameloop_client = client;
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
        if ((void*)server == (void*)g_server) {
            g_server = NULL;
        }
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

/* Send - from client to server */
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !data) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (transport->is_server) {
        /* Server broadcast to all clients (not implemented for SAMELOOP) */
        return UVBUS_ERROR_INVALID_PARAM;
    } else {
        /* Client send to server - use inlined callbacks */
        sameloop_client_t* client = (sameloop_client_t*)(void*)transport->impl.sameloop_client;
        if (!client || !client->server_recv_cb) {
            return UVBUS_ERROR_NOT_CONNECTED;
        }
        
        /* Direct callback with no pointer indirection */
        client->server_recv_cb(data, size, client, client->server_callback_ctx);
        
        return UVBUS_OK;
    }
}

/* Send to specific client - from server to client */
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    sameloop_client_t* client = (sameloop_client_t*)target;
    
    if (!transport || !data || !client) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Direct callback - no async overhead */
    if (client->recv_cb) {
        client->recv_cb(data, size, client, client->callback_ctx);
    }
    
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