/**
 * @file uvbus_transport_sameloop.c
 * @brief UVBus Same-Loop Transport - Zero-Overhead Direct Callback
 * 
 * Design:
 * - Server has endpoint with server_recv_cb
 * - Client has client_recv_cb
 * - Send directly calls the other's recv_cb
 * - No queue, no lock, no message pool
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 5.0
 */

#include "../include/uvbus.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* Server endpoint */
typedef struct sameloop_endpoint {
    char* name;
    uvbus_recv_callback_t server_recv_cb;  /* Server's callback (receives requests) */
    void* server_callback_ctx;
    struct uvbus* parent_bus;
    
    struct sameloop_client* client;  /* Reference to client */
    struct sameloop_endpoint* next;
} sameloop_endpoint_t;

/* Client */
typedef struct sameloop_client {
    sameloop_endpoint_t* server_endpoint;
    uvbus_recv_callback_t client_recv_cb;  /* Client's callback (receives responses) */
    void* client_callback_ctx;
} sameloop_client_t;

/* Global registry */
static sameloop_endpoint_t* g_endpoint_list = NULL;
#define ENDPOINT_HASH_SIZE 32
static sameloop_endpoint_t* g_endpoint_hash[ENDPOINT_HASH_SIZE] = {NULL};

/* Hash */
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash % ENDPOINT_HASH_SIZE;
}

/* Find endpoint */
static sameloop_endpoint_t* sameloop_find_endpoint(const char* name) {
    unsigned int hash = hash_string(name);
    sameloop_endpoint_t* endpoint = g_endpoint_hash[hash];
    while (endpoint) {
        if (strcmp(endpoint->name, name) == 0) return endpoint;
        endpoint = endpoint->next;
    }
    return NULL;
}

/* Vtable */
static int sameloop_listen(void* impl_ptr, const char* address);
static int sameloop_connect(void* impl_ptr, const char* address);
static void sameloop_disconnect(void* impl_ptr);
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size);
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void sameloop_free(void* impl_ptr);

static const uvbus_transport_vtable_t sameloop_vtable = {
    .listen = sameloop_listen,
    .connect = sameloop_connect,
    .disconnect = sameloop_disconnect,
    .send = sameloop_send,
    .send_to = sameloop_send_to,
    .free = sameloop_free
};

/* Create */
void* create_sameloop_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    (void)loop;
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) return NULL;
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->vtable = &sameloop_vtable;
    transport->is_connected = 0;
    return transport;
}

/* Listen - create server endpoint */
static int sameloop_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) return UVBUS_ERROR_INVALID_PARAM;
    
    const char* name = address;
    if (strncmp(address, "sameloop://", 12) == 0) name += 12;
    
    if (sameloop_find_endpoint(name)) return UVBUS_ERROR_ALREADY_EXISTS;
    
    sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)uvrpc_alloc(sizeof(sameloop_endpoint_t));
    if (!endpoint) return UVBUS_ERROR_NO_MEMORY;
    memset(endpoint, 0, sizeof(sameloop_endpoint_t));
    
    endpoint->name = uvrpc_strdup(name);
    if (!endpoint->name) {
        uvrpc_free(endpoint);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    /* Store server's recv_cb */
    endpoint->server_recv_cb = transport->recv_cb;
    endpoint->server_callback_ctx = transport->callback_ctx;
    endpoint->parent_bus = transport->parent_bus;
    
    /* Register */
    unsigned int hash = hash_string(name);
    endpoint->next = g_endpoint_hash[hash];
    g_endpoint_hash[hash] = endpoint;
    endpoint->next = g_endpoint_list;
    g_endpoint_list = endpoint;
    
    transport->impl.inproc_server = endpoint;
    transport->is_server = 1;
    transport->is_connected = 1;
    
    return UVBUS_OK;
}

/* Connect - link to server */
static int sameloop_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) return UVBUS_ERROR_INVALID_PARAM;
    
    const char* name = address;
    if (strncmp(address, "sameloop://", 12) == 0) name += 12;
    
    sameloop_endpoint_t* endpoint = sameloop_find_endpoint(name);
    if (!endpoint) return UVBUS_ERROR_NOT_FOUND;
    
    sameloop_client_t* client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!client) return UVBUS_ERROR_NO_MEMORY;
    memset(client, 0, sizeof(sameloop_client_t));
    
    client->server_endpoint = endpoint;
    
    /* Store client's recv_cb */
    client->client_recv_cb = transport->recv_cb;
    client->client_callback_ctx = transport->callback_ctx;
    
    /* Link */
    endpoint->client = client;
    transport->impl.inproc_client = client;
    transport->is_server = 0;
    transport->is_connected = 1;
    
    /* Trigger callback */
    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* Disconnect */
static void sameloop_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) return;
    
    if (transport->is_server && transport->impl.inproc_server) {
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.inproc_server;
        if (endpoint->client) {
            uvrpc_free(endpoint->client);
            endpoint->client = NULL;
        }
        if (endpoint->name) uvrpc_free(endpoint->name);
        uvrpc_free(endpoint);
        transport->impl.inproc_server = NULL;
    } else if (!transport->is_server && transport->impl.inproc_client) {
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.inproc_client;
        if (client->server_endpoint) {
            client->server_endpoint->client = NULL;
        }
        uvrpc_free(client);
        transport->impl.inproc_client = NULL;
    }
    transport->is_connected = 0;
}

/* Send - direct callback */
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    if (!impl_ptr) return UVBUS_ERROR_INVALID_PARAM;
    
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport->is_connected) return UVBUS_ERROR_NOT_CONNECTED;
    
    if (transport->is_server && transport->impl.inproc_server) {
        /* Server sending -> call client's recv_cb */
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.inproc_server;
        sameloop_client_t* client = endpoint->client;
        
        if (client && client->client_recv_cb) {
            client->client_recv_cb(data, size, transport, client->client_callback_ctx);
        }
        return UVBUS_OK;
    }
    
    if (!transport->is_server && transport->impl.inproc_client) {
        /* Client sending -> call server's recv_cb */
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.inproc_client;
        sameloop_endpoint_t* endpoint = client->server_endpoint;
        
        if (endpoint && endpoint->server_recv_cb) {
            endpoint->server_recv_cb(data, size, client, endpoint->server_callback_ctx);
        }
        return UVBUS_OK;
    }
    
    return UVBUS_ERROR_INVALID_PARAM;
}

/* Send to */
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    return sameloop_send(impl_ptr, data, size);
}

/* Free */
static void sameloop_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) return;
    sameloop_disconnect(transport);
    if (transport->address) uvrpc_free(transport->address);
    uvrpc_free(transport);
}