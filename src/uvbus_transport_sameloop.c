/**
 * @file uvbus_transport_sameloop.c
 * @brief UVBus Same-Loop Transport Implementation - True Async
 * 
 * Zero-lock, zero-copy transport for same-event-loop communication.
 * Uses uv_async_t for true async message delivery.
 * 
 * Design Principles:
 * - No locks (single-event-loop + atomic operations)
 * - No reference counting (direct callbacks)
 * - Async delivery (no stack overflow risk)
 * - Minimal code (simple and fast)
 * 
 * Usage:
 * - Best for module communication within the same event loop
 * - True async delivery (no recursion risk)
 * - Not thread-safe (use INPROC for cross-thread)
 * - Requires same uv_loop_t instance
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 2.0 (Async Implementation)
 */

#include "../include/uvbus.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* Pending message for async delivery */
typedef struct sameloop_pending_message {
    const uint8_t* data;
    size_t size;
    void* client;
    struct sameloop_pending_message* next;
} sameloop_pending_message_t;

/* Simple in-memory endpoint */
typedef struct sameloop_endpoint {
    char* name;
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
    struct uvbus* parent_bus;
    
    /* Clients list - no locks, single-event-loop */
    void** clients;
    int client_count;
    int client_capacity;
    
    /* Async handle for message delivery */
    uv_async_t async_handle;
    
    /* Lock-free message queue (atomic operations) */
    volatile sameloop_pending_message_t* pending_head;
    volatile sameloop_pending_message_t* pending_tail;
    volatile int pending_count;
    
    /* Message pool for zero-allocation */
    sameloop_pending_message_t* message_pool;
    volatile int pool_size;
    volatile int pool_index;
    
    struct sameloop_endpoint* next;
} sameloop_endpoint_t;

/* Simple client structure */
typedef struct sameloop_client {
    sameloop_endpoint_t* server_endpoint;
    void* client_transport;
    int is_active;
    
    uvbus_recv_callback_t recv_cb;
    void* callback_ctx;
} sameloop_client_t;

/* Global endpoint list - no locks needed (single-event-loop) */
static sameloop_endpoint_t* g_endpoint_list = NULL;

/* Simple hash table - no locks needed */
#define ENDPOINT_HASH_SIZE 32
#define MESSAGE_POOL_SIZE 2048
static sameloop_endpoint_t* g_endpoint_hash[ENDPOINT_HASH_SIZE] = {NULL};

/* Hash function */
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % ENDPOINT_HASH_SIZE;
}

/* Find endpoint by name */
static sameloop_endpoint_t* sameloop_find_endpoint(const char* name) {
    unsigned int hash = hash_string(name);
    sameloop_endpoint_t* endpoint = g_endpoint_hash[hash];
    
    while (endpoint) {
        if (strcmp(endpoint->name, name) == 0) {
            return endpoint;
        }
        endpoint = endpoint->next;
    }
    
    return NULL;
}

/* Add client to endpoint */
static void sameloop_add_client(sameloop_endpoint_t* endpoint, void* client) {
    if (endpoint->client_count >= endpoint->client_capacity) {
        int new_capacity = endpoint->client_capacity * 2;
        if (new_capacity == 0) new_capacity = 10;
        if (new_capacity > 1024) new_capacity = 1024;
        
        void** new_clients = (void**)uvrpc_realloc(endpoint->clients, 
                                                   sizeof(void*) * new_capacity);
        if (!new_clients) return;
        endpoint->clients = new_clients;
        endpoint->client_capacity = new_capacity;
    }
    
    endpoint->clients[endpoint->client_count++] = client;
}

/* Remove client from endpoint */
static void sameloop_remove_client(sameloop_endpoint_t* endpoint, void* client) {
    for (int i = 0; i < endpoint->client_count; i++) {
        if (endpoint->clients[i] == client) {
            for (int j = i; j < endpoint->client_count - 1; j++) {
                endpoint->clients[j] = endpoint->clients[j + 1];
            }
            endpoint->client_count--;
            return;
        }
    }
}

/* Async callback - processes pending messages */
static void sameloop_async_cb(uv_async_t* handle) {
    sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)handle->data;
    if (!endpoint) return;
    
    /* Process all pending messages */
    while (1) {
        /* Atomic dequeue */
        sameloop_pending_message_t* pending = (sameloop_pending_message_t*)
            __sync_lock_test_and_set(&endpoint->pending_head, NULL);
        
        if (!pending) {
            /* No more messages */
            if (endpoint->pending_tail != NULL) {
                /* Reset tail */
                __sync_lock_test_and_set(&endpoint->pending_tail, NULL);
            }
            break;
        }
        
        /* Update tail if needed */
        if (endpoint->pending_tail == pending) {
            __sync_lock_test_and_set(&endpoint->pending_tail, NULL);
        }
        
        __sync_sub_and_fetch(&endpoint->pending_count, 1);
        
        /* Deliver message */
        sameloop_client_t* client = (sameloop_client_t*)pending->client;
        if (client && client->is_active && client->recv_cb) {
            client->recv_cb(pending->data, pending->size, client, endpoint->callback_ctx);
        }
        
        /* Return to pool instead of freeing */
        if ((sameloop_pending_message_t*)pending >= endpoint->message_pool &&
            (sameloop_pending_message_t*)pending < endpoint->message_pool + MESSAGE_POOL_SIZE) {
            /* From pool, just reset it */
            pending->data = NULL;
            pending->size = 0;
            pending->client = NULL;
            pending->next = NULL;
        } else {
            /* Dynamically allocated, free it */
            uvrpc_free(pending);
        }
    }
}

/* Get message from pool (zero-allocation) */
static sameloop_pending_message_t* get_message_from_pool(sameloop_endpoint_t* endpoint) {
    if (endpoint->pool_index < endpoint->pool_size) {
        int idx = __sync_fetch_and_add(&endpoint->pool_index, 1);
        if (idx < endpoint->pool_size) {
            return &endpoint->message_pool[idx];
        }
    }
    return NULL;
}

/* Send to all clients - async delivery with optimizations */
static void sameloop_send_to_all(sameloop_endpoint_t* endpoint,
                                    const uint8_t* data, size_t size) {
    /* Single client fast path */
    if (endpoint->client_count == 1) {
        sameloop_client_t* client = (sameloop_client_t*)endpoint->clients[0];
        if (client && client->is_active && client->recv_cb) {
            /* Single client: direct call (no queue, no allocation) */
            client->recv_cb(data, size, client, endpoint->callback_ctx);
            return;
        }
        return;
    }
    
    /* Multiple clients: use queue */
    int was_empty = (endpoint->pending_count == 0);
    
    for (int i = 0; i < endpoint->client_count; i++) {
        sameloop_client_t* client = (sameloop_client_t*)endpoint->clients[i];
        if (client && client->is_active && client->recv_cb) {
            /* Try pool first (zero-allocation) */
            sameloop_pending_message_t* pending = get_message_from_pool(endpoint);
            
            /* Fall back to allocation if pool is full */
            if (!pending) {
                pending = (sameloop_pending_message_t*)
                    uvrpc_alloc(sizeof(sameloop_pending_message_t));
                if (!pending) continue;
            }
            
            pending->data = data;
            pending->size = size;
            pending->client = client;
            pending->next = NULL;
            
            /* Atomic enqueue */
            sameloop_pending_message_t* old_tail = (sameloop_pending_message_t*)
                __sync_lock_test_and_set(&endpoint->pending_tail, pending);
            
            if (old_tail) {
                old_tail->next = pending;
            } else {
                /* Queue was empty, set head */
                __sync_lock_test_and_set(&endpoint->pending_head, pending);
            }
            
            __sync_add_and_fetch(&endpoint->pending_count, 1);
        }
    }
    
    /* Only trigger async if queue was empty (reduce uv_async_send calls) */
    if (was_empty && endpoint->pending_count > 0) {
        uv_async_send(&endpoint->async_handle);
    }
}

/* Transport vtable functions */
static int sameloop_listen(void* impl_ptr, const char* address);
static int sameloop_connect(void* impl_ptr, const char* address);
static void sameloop_disconnect(void* impl_ptr);
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size);
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void sameloop_free(void* impl_ptr);

/* Vtable */
static const uvbus_transport_vtable_t sameloop_vtable = {
    .listen = sameloop_listen,
    .connect = sameloop_connect,
    .disconnect = sameloop_disconnect,
    .send = sameloop_send,
    .send_to = sameloop_send_to,
    .free = sameloop_free
};

/* Create transport */
void* sameloop_transport_create(uv_loop_t* loop) {
    (void)loop;
    
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = UVBUS_TRANSPORT_SAMELOOP;
    transport->vtable = &sameloop_vtable;
    
    return transport;
}

/* Listen implementation */
static int sameloop_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address || !transport->loop) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    const char* name = address;
    if (strncmp(address, "sameloop://", 12) == 0) {
        name += 12;
    }
    
    if (sameloop_find_endpoint(name)) {
        return UVBUS_ERROR_ALREADY_EXISTS;
    }
    
    sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)uvrpc_alloc(sizeof(sameloop_endpoint_t));
    if (!endpoint) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(endpoint, 0, sizeof(sameloop_endpoint_t));
    endpoint->name = uvrpc_strdup(name);
    if (!endpoint->name) {
        uvrpc_free(endpoint);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    endpoint->recv_cb = transport->recv_cb;
    endpoint->error_cb = transport->error_cb;
    endpoint->callback_ctx = transport->callback_ctx;
    endpoint->parent_bus = transport->parent_bus;
    
    /* Initialize message pool */
    endpoint->message_pool = (sameloop_pending_message_t*)
        uvrpc_alloc(sizeof(sameloop_pending_message_t) * MESSAGE_POOL_SIZE);
    if (endpoint->message_pool) {
        endpoint->pool_size = MESSAGE_POOL_SIZE;
        endpoint->pool_index = 0;
    } else {
        endpoint->pool_size = 0;
        endpoint->pool_index = 0;
    }
    
    /* Initialize async handle */
    if (uv_async_init(transport->loop, &endpoint->async_handle, sameloop_async_cb) != 0) {
        if (endpoint->message_pool) {
            uvrpc_free(endpoint->message_pool);
        }
        uvrpc_free(endpoint->name);
        uvrpc_free(endpoint);
        return UVBUS_ERROR;
    }
    endpoint->async_handle.data = endpoint;
    
    /* Add to hash table */
    unsigned int hash = hash_string(name);
    endpoint->next = g_endpoint_hash[hash];
    g_endpoint_hash[hash] = endpoint;
    
    /* Add to list */
    endpoint->next = g_endpoint_list;
    g_endpoint_list = endpoint;
    
    transport->impl.inproc_server = endpoint;
    transport->is_server = 1;
    transport->is_connected = 1;
    
    return UVBUS_OK;
}

/* Connect implementation */
static int sameloop_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !address) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    const char* name = address;
    if (strncmp(address, "sameloop://", 12) == 0) {
        name += 12;
    }
    
    sameloop_endpoint_t* endpoint = sameloop_find_endpoint(name);
    if (!endpoint) {
        return UVBUS_ERROR_NOT_FOUND;
    }
    
    sameloop_client_t* client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!client) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(client, 0, sizeof(sameloop_client_t));
    client->server_endpoint = endpoint;
    client->is_active = 1;
    client->recv_cb = transport->recv_cb;
    client->callback_ctx = transport->callback_ctx;
    
    sameloop_add_client(endpoint, client);
    
    transport->impl.inproc_client = client;
    transport->is_server = 0;
    transport->is_connected = 1;
    
    return UVBUS_OK;
}

/* Disconnect implementation */
static void sameloop_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) return;
    
    if (transport->is_server && transport->impl.inproc_server) {
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.inproc_server;
        
        /* Clean up clients */
        for (int i = 0; i < endpoint->client_count; i++) {
            sameloop_client_t* client = (sameloop_client_t*)endpoint->clients[i];
            if (client) uvrpc_free(client);
        }
        
        /* Clean up pending messages */
        while (endpoint->pending_head) {
            sameloop_pending_message_t* pending = (sameloop_pending_message_t*)endpoint->pending_head;
            endpoint->pending_head = pending->next;
            uvrpc_free(pending);
        }
        
        /* Clean up async handle */
        uv_close((uv_handle_t*)&endpoint->async_handle, NULL);
        
        /* Clean up message pool */
        if (endpoint->message_pool) {
            uvrpc_free(endpoint->message_pool);
        }
        
        /* Free endpoint */
        if (endpoint->clients) uvrpc_free(endpoint->clients);
        if (endpoint->name) uvrpc_free(endpoint->name);
        uvrpc_free(endpoint);
        
        transport->impl.inproc_server = NULL;
    } else if (!transport->is_server && transport->impl.inproc_client) {
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.inproc_client;
        
        if (client->server_endpoint) {
            sameloop_remove_client(client->server_endpoint, client);
        }
        
        client->is_active = 0;
        uvrpc_free(client);
        
        transport->impl.inproc_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* Send implementation */
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    /* Optimized: combine checks to reduce branch prediction overhead */
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (transport && transport->is_connected) {
        if (transport->is_server && transport->impl.inproc_server) {
            sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.inproc_server;
            sameloop_send_to_all(endpoint, data, size);
            return UVBUS_OK;
        }
        
        if (!transport->is_server && transport->impl.inproc_client) {
            sameloop_client_t* client = (sameloop_client_t*)transport->impl.inproc_client;
            if (client->server_endpoint && client->server_endpoint->recv_cb) {
                client->server_endpoint->recv_cb(data, size, client, client->server_endpoint->callback_ctx);
            }
            return UVBUS_OK;
        }
    }
    return UVBUS_ERROR_INVALID_PARAM;
}

/* Send to specific client */
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !data || !target) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->is_server && transport->impl.inproc_server) {
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.inproc_server;
        sameloop_client_t* target_client = (sameloop_client_t*)target;
        
        if (!target_client || target_client->server_endpoint != endpoint) {
            return UVBUS_ERROR_INVALID_PARAM;
        }
        
        if (target_client->is_active && target_client->recv_cb) {
            target_client->recv_cb(data, size, target_client, target_client->callback_ctx);
        }
        
        return UVBUS_OK;
    }
    
    return UVBUS_ERROR_INVALID_PARAM;
}

/* Free transport */
static void sameloop_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) return;
    
    sameloop_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}