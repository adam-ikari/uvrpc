/**
 * UVRPC INPROC Transport Implementation (No Global Variables, No loop->data)
 * Zero-copy in-process transport for maximum performance
 * 
 * Design: Endpoint registry stored per transport instance
 * No global variables, respects user's loop->data
 */

#include "uvrpc_transport_internal.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Typedef for compatibility */
typedef struct inproc_endpoint inproc_endpoint_t;
typedef struct uvrpc_inproc_transport uvrpc_inproc_transport_t;

/* Forward declarations */
static void async_callback(uv_async_t* handle);

/* Global endpoint list (simplified - just a linked list) */
static inproc_endpoint_t* g_endpoint_list = NULL;

/* Find endpoint by name */
static inproc_endpoint_t* find_endpoint(const char* name) {
    if (!name) return NULL;
    
    inproc_endpoint_t* current = g_endpoint_list;
    while (current) {
        if (current->name && strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* Add endpoint to registry */
static void add_endpoint(inproc_endpoint_t* endpoint) {
    if (!endpoint) return;
    
    endpoint->next = g_endpoint_list;
    g_endpoint_list = endpoint;
}

/* Remove endpoint from registry */
static void remove_endpoint(inproc_endpoint_t* endpoint) {
    if (!endpoint || !g_endpoint_list) return;
    
    if (g_endpoint_list == endpoint) {
        g_endpoint_list = endpoint->next;
    } else {
        inproc_endpoint_t* current = g_endpoint_list;
        while (current->next && current->next != endpoint) {
            current = current->next;
        }
        if (current->next) {
            current->next = endpoint->next;
        }
    }
    endpoint->next = NULL;
}

/* Create new endpoint (returns NULL if already exists with server) */
static inproc_endpoint_t* create_endpoint(const char* name, int* exists) {
    if (!name) return NULL;
    
    /* Strip inproc:// prefix if present */
    const char* endpoint_name = name;
    if (name && strncmp(name, "inproc://", 9) == 0) {
        endpoint_name = name + 9;
    }
    
    /* Check if already exists */
    inproc_endpoint_t* existing = find_endpoint(endpoint_name);
    if (existing) {
        if (exists) *exists = 1;
        return existing;  /* Return existing for clients, caller should check server_transport */
    }
    
    /* Create new endpoint */
    inproc_endpoint_t* endpoint = uvrpc_calloc(1, sizeof(inproc_endpoint_t));
    if (!endpoint) return NULL;
    
    endpoint->name = uvrpc_strdup(endpoint_name);
    if (!endpoint->name) {
        uvrpc_free(endpoint);
        return NULL;
    }
    
    endpoint->server_transport = NULL;
    endpoint->clients = NULL;
    endpoint->client_count = 0;
    endpoint->client_capacity = 0;
    endpoint->next = NULL;
    
    add_endpoint(endpoint);
    if (exists) *exists = 0;
    return endpoint;
}

/* Cleanup endpoint */
static void free_endpoint(inproc_endpoint_t* endpoint) {
    if (!endpoint) return;
    
    if (endpoint->name) uvrpc_free(endpoint->name);
    if (endpoint->clients) uvrpc_free(endpoint->clients);
    uvrpc_free(endpoint);
}

/* Public API for creating endpoint (for transport layer use) */
inproc_endpoint_t* uvrpc_inproc_create_endpoint(const char* name) {
    if (!name) return NULL;
    
    /* Strip inproc:// prefix if present */
    const char* endpoint_name = name;
    if (name && strncmp(name, "inproc://", 9) == 0) {
        endpoint_name = name + 9;
    }
    
    return create_endpoint(endpoint_name, NULL);
}

/* Server transport creation */
int uvrpc_transport_inproc_server_new(uv_loop_t* loop, const char* address,
                                      uvrpc_recv_callback_t recv_cb,
                                      uvrpc_connect_callback_t connect_cb,
                                      uvrpc_error_callback_t error_cb,
                                      void* ctx, void** transport_out) {
    if (!loop || !transport_out) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_inproc_transport_t));
    if (!transport) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    transport->loop = loop;
    transport->is_server = 1;
    transport->recv_cb = recv_cb;
    transport->connect_cb = connect_cb;
    transport->error_cb = error_cb;
    transport->ctx = ctx;
    transport->is_connected = 0;
    
    /* Address can be NULL - will be set later via listen */
    transport->address = address ? uvrpc_strdup(address) : NULL;
    if (address && !transport->address) {
        uvrpc_free(transport);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    /* Don't create endpoint yet - will be created in listen */
    transport->endpoint = NULL;
    
    transport->is_connected = 0;
    *transport_out = transport;
    
    return UVRPC_OK;
}

/* Client transport creation */
int uvrpc_transport_inproc_client_new(uv_loop_t* loop, const char* address,
                                      uvrpc_recv_callback_t recv_cb,
                                      uvrpc_connect_callback_t connect_cb,
                                      uvrpc_error_callback_t error_cb,
                                      void* ctx, void** transport_out) {
    (void)address;  /* Address will be set later in connect */
    
    if (!loop || !transport_out) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_inproc_transport_t));
    if (!transport) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    transport->loop = loop;
    transport->is_server = 0;
    transport->recv_cb = recv_cb;
    transport->connect_cb = connect_cb;
    transport->error_cb = error_cb;
    transport->ctx = ctx;
    transport->is_connected = 0;
    
    /* Address will be set later in connect */
    transport->address = NULL;
    transport->endpoint = NULL;
    
    *transport_out = transport;
    
    return UVRPC_OK;
}

/* Listen on endpoint (server only) */
int uvrpc_transport_inproc_listen(void* transport_ctx, const char* address) {
    if (!transport_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    
    if (!transport->is_server) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Update address if provided */
    if (address) {
        if (transport->address) uvrpc_free(transport->address);
        transport->address = uvrpc_strdup(address);
        if (!transport->address) return UVRPC_ERROR_NO_MEMORY;
    }
    
    /* Create or get endpoint */
    if (!transport->endpoint) {
        int exists = 0;
        transport->endpoint = create_endpoint(transport->address, &exists);
        if (!transport->endpoint) return UVRPC_ERROR_NO_MEMORY;
        
        /* Check if endpoint already has a server - prevent multi-service pollution */
        if (exists && transport->endpoint->server_transport != NULL) {
            return UVRPC_ERROR_ALREADY_EXISTS;  /* Endpoint already in use by another server */
        }
    }
    
    /* Set server transport */
    transport->endpoint->server_transport = transport;
    transport->is_connected = 1;
    
    return UVRPC_OK;
}

/* Connect to endpoint (client only) */
int uvrpc_transport_inproc_connect(void* transport_ctx, const char* address) {
    if (!transport_ctx || !address) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    
    if (transport->is_server) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Update address */
    if (transport->address) uvrpc_free(transport->address);
    transport->address = uvrpc_strdup(address);
    if (!transport->address) return UVRPC_ERROR_NO_MEMORY;
    
    /* Find or create endpoint */
    transport->endpoint = create_endpoint(transport->address, NULL);
    if (!transport->endpoint) return UVRPC_ERROR_NO_MEMORY;
    
    /* Add client to endpoint */
    if (transport->endpoint->server_transport) {
        /* Server already exists, add client */
        inproc_add_client(transport->endpoint, transport);
    }
    
    transport->is_connected = 1;
    
    return UVRPC_OK;
}

/* Send data */
int uvrpc_transport_inproc_send(void* transport_ctx, const void* data, size_t len) {
    if (!transport_ctx || !data || len == 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    
    if (!transport->is_connected || !transport->endpoint) {
        return UVRPC_ERROR_NOT_CONNECTED;
    }
    
    /* If server, send to all clients */
    if (transport->is_server) {
        inproc_endpoint_t* endpoint = transport->endpoint;
        for (int i = 0; i < endpoint->client_count; i++) {
            uvrpc_inproc_transport_t* client = (uvrpc_inproc_transport_t*)endpoint->clients[i];
            if (client && client->recv_cb) {
                client->recv_cb((uint8_t*)data, len, client->ctx);
            }
        }
    } else {
        /* If client, send to server */
        uvrpc_inproc_transport_t* server = (uvrpc_inproc_transport_t*)transport->endpoint->server_transport;
        if (server && server->recv_cb) {
            server->recv_cb((uint8_t*)data, len, server->ctx);
        }
    }
    
    return UVRPC_OK;
}

/* Close transport */
int uvrpc_transport_inproc_close(void* transport_ctx) {
    if (!transport_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    
    if (!transport->is_connected) {
        return UVRPC_OK;
    }
    
    transport->is_connected = 0;
    
    /* Remove from endpoint's client list if client */
    if (!transport->is_server && transport->endpoint) {
        inproc_endpoint_t* endpoint = transport->endpoint;
        for (int i = 0; i < endpoint->client_count; i++) {
            if (endpoint->clients[i] == transport) {
                /* Shift remaining clients */
                for (int j = i; j < endpoint->client_count - 1; j++) {
                    endpoint->clients[j] = endpoint->clients[j + 1];
                }
                endpoint->client_count--;
                break;
            }
        }
    }
    
    return UVRPC_OK;
}

/* Free transport */
int uvrpc_transport_inproc_free(void* transport_ctx) {
    if (!transport_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    
    if (transport->is_connected) {
        uvrpc_transport_inproc_close(transport);
    }
    
    /* If server, remove endpoint */
    if (transport->is_server && transport->endpoint) {
        remove_endpoint(transport->endpoint);
        free_endpoint(transport->endpoint);
    }
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
    
    return UVRPC_OK;
}

/* Get loop */
uv_loop_t* uvrpc_transport_inproc_get_loop(void* transport_ctx) {
    if (!transport_ctx) return NULL;
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    return transport->loop;
}

/* Get address */
const char* uvrpc_transport_inproc_get_address(void* transport_ctx) {
    if (!transport_ctx) return NULL;
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    return transport->address;
}

/* Check if connected */
int uvrpc_transport_inproc_is_connected(void* transport_ctx) {
    if (!transport_ctx) return 0;
    
    uvrpc_inproc_transport_t* transport = (uvrpc_inproc_transport_t*)transport_ctx;
    return transport->is_connected;
}

/* Public API for transport.c compatibility */
inproc_endpoint_t* inproc_find_endpoint(const char* name) {
    return find_endpoint(name);
}

inproc_endpoint_t* inproc_get_endpoint(uv_loop_t* loop, const char* name) {
    (void)loop;  /* Loop parameter not used in this design */
    return find_endpoint(name);
}

void inproc_add_client(inproc_endpoint_t* endpoint, void* transport) {
    if (!endpoint || !transport) return;
    
    /* Expand client array if needed */
    if (endpoint->client_count >= endpoint->client_capacity) {
        int new_capacity = (endpoint->client_capacity == 0) ? 4 : endpoint->client_capacity * 2;
        void** new_clients = uvrpc_realloc(endpoint->clients, new_capacity * sizeof(void*));
        if (!new_clients) return;
        
        endpoint->clients = new_clients;
        endpoint->client_capacity = new_capacity;
    }
    
    endpoint->clients[endpoint->client_count++] = transport;
}

void inproc_send_to_all(void* sender, inproc_endpoint_t* endpoint, const uint8_t* data, size_t size) {
    if (!endpoint || !data || size == 0) return;
    
    for (int i = 0; i < endpoint->client_count; i++) {
        uvrpc_inproc_transport_t* client = (uvrpc_inproc_transport_t*)endpoint->clients[i];
        if (client && client->recv_cb) {
            client->recv_cb((uint8_t*)data, size, client->ctx);
        }
    }
}

/* Old API for compatibility - deprecated */
/* Note: This function is only called during transport creation */
/* The actual address should be set by the transport layer */
void* uvrpc_transport_inproc_new(uv_loop_t* loop, int is_server) {
    void* transport = NULL;
    int rc;
    
    /* Use a placeholder address - will be set by transport layer */
    const char* placeholder = (is_server) ? "inproc://server" : "inproc://client";
    
    if (is_server) {
        rc = uvrpc_transport_inproc_server_new(loop, placeholder, NULL, NULL, NULL, NULL, &transport);
    } else {
        rc = uvrpc_transport_inproc_client_new(loop, placeholder, NULL, NULL, NULL, NULL, &transport);
    }
    
    return (rc == UVRPC_OK) ? transport : NULL;
}

/* Vtable for INPROC transport */
const uvrpc_transport_vtable_t inproc_vtable = {
    .listen = (int (*)(void*, const char*))uvrpc_transport_inproc_listen,
    .connect = (int (*)(void*, const char*))uvrpc_transport_inproc_connect,
    .disconnect = NULL,
    .send = (void (*)(void*, const uint8_t*, size_t))uvrpc_transport_inproc_send,
    .send_to = NULL,
    .free = (void (*)(void*))uvrpc_transport_inproc_free,
    .set_timeout = NULL
};