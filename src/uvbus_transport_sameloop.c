/**
 * UVBus INPROC Transport Implementation (Same-Loop)
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* Thread-safe rwlock for protecting global endpoint hash table */
static pthread_rwlock_t g_sameloop_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* INPROC endpoint */
typedef struct sameloop_endpoint {
    char* name;
    void* server_transport;
    void** clients;
    int client_count;
    int client_capacity;
    struct sameloop_endpoint* next;
    
    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
} sameloop_endpoint_t;

/* INPROC client */
typedef struct sameloop_client {
    void* server_endpoint;
    void* client_transport;
    int is_active;
    volatile int ref_count;  /* Atomic reference count for thread safety */
    
    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
} sameloop_client_t;

/* Global endpoint list */
static sameloop_endpoint_t* g_endpoint_list = NULL;

/* Simple hash table for endpoints */
#define ENDPOINT_HASH_SIZE 32
static sameloop_endpoint_t* g_endpoint_hash[UVBUS_HASH_TABLE_SIZE] = {NULL};

/* Hash function */
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % UVBUS_HASH_TABLE_SIZE;
}

/* Find endpoint by name */
static sameloop_endpoint_t* sameloop_find_endpoint(const char* name) {
    pthread_rwlock_rdlock(&g_sameloop_rwlock);
    unsigned int hash = hash_string(name);
    sameloop_endpoint_t* endpoint = g_endpoint_hash[hash];

    while (endpoint) {
        if (strcmp(endpoint->name, name) == 0) {
            pthread_rwlock_unlock(&g_sameloop_rwlock);
            return endpoint;
        }
        endpoint = endpoint->next;
    }

    pthread_rwlock_unlock(&g_sameloop_rwlock);
    return NULL;
}

/* Add endpoint to hash table */
static void sameloop_add_endpoint(sameloop_endpoint_t* endpoint) {
    pthread_rwlock_wrlock(&g_sameloop_rwlock);
    unsigned int hash = hash_string(endpoint->name);
    endpoint->next = g_endpoint_hash[hash];
    g_endpoint_hash[hash] = endpoint;
    pthread_rwlock_unlock(&g_sameloop_rwlock);
}

/* Remove endpoint from hash table */
static void sameloop_remove_endpoint(sameloop_endpoint_t* endpoint) {
    pthread_rwlock_wrlock(&g_sameloop_rwlock);
    unsigned int hash = hash_string(endpoint->name);
    sameloop_endpoint_t** ptr = &g_endpoint_hash[hash];
    
    while (*ptr) {
        if (*ptr == endpoint) {
            *ptr = endpoint->next;
            pthread_rwlock_unlock(&g_sameloop_rwlock);
            return;
        }
        ptr = &(*ptr)->next;
    }
    pthread_rwlock_unlock(&g_sameloop_rwlock);
}

/* Add client to endpoint */
static void sameloop_add_client(sameloop_endpoint_t* endpoint, void* client) {
    pthread_rwlock_wrlock(&g_sameloop_rwlock);
    if (endpoint->client_count >= endpoint->client_capacity) {
        endpoint->client_capacity *= 2;
        endpoint->clients = (void**)uvrpc_realloc(
            endpoint->clients,
            sizeof(void*) * endpoint->client_capacity
        );
    }

    endpoint->clients[endpoint->client_count++] = client;
    pthread_rwlock_unlock(&g_sameloop_rwlock);
}

/* Remove client from endpoint */
static void sameloop_remove_client(sameloop_endpoint_t* endpoint, void* client) {
    pthread_rwlock_wrlock(&g_sameloop_rwlock);
    for (int i = 0; i < endpoint->client_count; i++) {
        if (endpoint->clients[i] == client) {
            /* Shift remaining clients */
            for (int j = i; j < endpoint->client_count - 1; j++) {
                endpoint->clients[j] = endpoint->clients[j + 1];
            }
            endpoint->client_count--;
            pthread_rwlock_unlock(&g_sameloop_rwlock);
            return;
        }
    }
    pthread_rwlock_unlock(&g_sameloop_rwlock);
}

/* Send to all clients */
static void sameloop_send_to_all(sameloop_endpoint_t* endpoint,
                         const uint8_t* data, size_t size) {
    pthread_rwlock_rdlock(&g_sameloop_rwlock);
    int client_count = endpoint->client_count;
    void** clients = (void**)uvrpc_alloc(sizeof(void*) * client_count);
    if (clients) {
        memcpy(clients, endpoint->clients, sizeof(void*) * client_count);
    }
    pthread_rwlock_unlock(&g_sameloop_rwlock);
    
    if (!clients) return;
    
    for (int i = 0; i < client_count; i++) {
        sameloop_client_t* client = (sameloop_client_t*)clients[i];
        if (client && client->is_active && client->recv_cb) {
            client->recv_cb(data, size, client, client->callback_ctx);
        }
    }
    
    uvrpc_free(clients);
}

/* INPROC vtable functions */
static int sameloop_listen(void* impl_ptr, const char* address);
static int sameloop_connect(void* impl_ptr, const char* address);
static void sameloop_disconnect(void* impl_ptr);
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size);
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void sameloop_free(void* impl_ptr);

/* Global vtable for INPROC */
static const uvbus_transport_vtable_t sameloop_vtable = {
    .listen = sameloop_listen,
    .connect = sameloop_connect,
    .disconnect = sameloop_disconnect,
    .send = sameloop_send,
    .send_to = sameloop_send_to,
    .free = sameloop_free
};

/* INPROC listen implementation */
static int sameloop_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "sameloop://", 9) == 0) {
        name += 9;
    }

    /* Check if endpoint already exists */
    sameloop_endpoint_t* endpoint = sameloop_find_endpoint(name);
    if (endpoint) {
        return UVBUS_ERROR_ALREADY_EXISTS;
    }
    
    /* Create endpoint */
    endpoint = (sameloop_endpoint_t*)uvrpc_alloc(sizeof(sameloop_endpoint_t));
    if (!endpoint) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    endpoint->name = uvrpc_strdup(name);
    endpoint->server_transport = transport;
    endpoint->clients = (void**)uvrpc_alloc(sizeof(void*) * UVBUS_INITIAL_CLIENT_CAPACITY);
    endpoint->client_capacity = UVBUS_INITIAL_CLIENT_CAPACITY;
    endpoint->client_count = 0;
    endpoint->next = NULL;
    
    /* Set callbacks */
    endpoint->recv_cb = transport->recv_cb;
    endpoint->error_cb = transport->error_cb;
    endpoint->callback_ctx = transport->callback_ctx;
    
    /* Add to global list */
    sameloop_add_endpoint(endpoint);

    transport->impl.sameloop_server = (void*)endpoint;
    transport->is_connected = 1;

    /* Set bus as active */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }

    return UVBUS_OK;
}

/* INPROC connect implementation */
static int sameloop_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "sameloop://", 9) == 0) {
        name += 9;
    }

    /* Find endpoint */
    sameloop_endpoint_t* endpoint = sameloop_find_endpoint(name);
    if (!endpoint) {
        /* INPROC is for in-process communication only.
         * If the endpoint is not found, it means the server is not running
         * in the same process. */
        fprintf(stderr, "[INPROC] ERROR: Endpoint '%s' not found.\n", name);
        fprintf(stderr, "[INPROC] INPROC transport is for in-process communication only.\n");
        fprintf(stderr, "[INPROC] Make sure the server is running in the same process as the client.\n");
        return UVBUS_ERROR_NOT_FOUND;
    }

    
    /* Create client */
    sameloop_client_t* client = (sameloop_client_t*)uvrpc_alloc(sizeof(sameloop_client_t));
    if (!client) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    client->server_endpoint = endpoint;
    client->client_transport = transport;
    client->is_active = 1;
    client->ref_count = 1;  /* Initialize reference count */
    
    /* Set client's own callbacks */
    client->recv_cb = transport->recv_cb;
    client->error_cb = transport->error_cb;
    client->callback_ctx = transport->callback_ctx;
    
    /* Add to endpoint */
    sameloop_add_client(endpoint, client);
    
    transport->impl.sameloop_client = (void*)client;
    transport->is_connected = 1;
    
    /* Set bus as active */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }
    
    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* INPROC disconnect implementation */
static void sameloop_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.sameloop_server) {
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.sameloop_server;
        /* Remove endpoint from global list */
        sameloop_remove_endpoint(endpoint);
        
        /* Free all clients */
        for (int i = 0; i < endpoint->client_count; i++) {
            if (endpoint->clients[i]) {
                sameloop_client_t* client = (sameloop_client_t*)endpoint->clients[i];
                client->is_active = 0;
                /* Decrement reference count, free if reaches zero */
                if (__sync_sub_and_fetch(&client->ref_count, 1) == 0) {
                    uvrpc_free(client);
                }
            }
        }
        
        /* Free endpoint */
        uvrpc_free(endpoint->name);
        uvrpc_free(endpoint->clients);
        uvrpc_free(endpoint);
        transport->impl.sameloop_server = NULL;
    } else if (!transport->is_server && transport->impl.sameloop_client) {
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
        /* Remove from endpoint */
        if (client->server_endpoint) {
            sameloop_remove_client(client->server_endpoint, client);
        }
        
        client->is_active = 0;
        /* Decrement reference count, free if reaches zero */
        if (__sync_sub_and_fetch(&client->ref_count, 1) == 0) {
            uvrpc_free(client);
        }
        transport->impl.sameloop_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* INPROC send implementation */
static int sameloop_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    if (transport->is_server && transport->impl.sameloop_server) {
        sameloop_endpoint_t* endpoint = (sameloop_endpoint_t*)transport->impl.sameloop_server;
        sameloop_send_to_all(endpoint, data, size);
    } else if (!transport->is_server && transport->impl.sameloop_client) {
        sameloop_client_t* client = (sameloop_client_t*)transport->impl.sameloop_client;
        if (client->server_endpoint) {
            sameloop_endpoint_t* endpoint = client->server_endpoint;
            if (endpoint->recv_cb) {
                void* server_ctx = NULL;
                if (endpoint->server_transport) {
                    server_ctx = ((uvbus_transport_t*)endpoint->server_transport)->callback_ctx;
                }
                if (!server_ctx) {
                    server_ctx = endpoint->callback_ctx;
                }
                endpoint->recv_cb(data, size, client, server_ctx);
            }
        }
    }
    
    return UVBUS_OK;
}

/* INPROC send to specific client implementation */
static int sameloop_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    sameloop_client_t* client = (sameloop_client_t*)target;
    if (!client || !client->is_active) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Call the client's callback, not the server's callback */
    if (client->recv_cb) {
        /* Pass client as client_ctx, and client's callback_ctx as server_ctx */
        client->recv_cb(data, size, client, client->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* INPROC free implementation */
static void sameloop_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    sameloop_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}

/* Export function to create INPROC transport */
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