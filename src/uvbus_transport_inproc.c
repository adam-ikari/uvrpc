/**
 * UVBus INPROC Transport Implementation (In-Process)
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* Thread-safe mutex for protecting global endpoint hash table */
static pthread_mutex_t g_endpoint_mutex = PTHREAD_MUTEX_INITIALIZER;

/* INPROC endpoint */
typedef struct inproc_endpoint {
    char* name;
    void* server_transport;
    void** clients;
    int client_count;
    int client_capacity;
    struct inproc_endpoint* next;
    
    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
} inproc_endpoint_t;

/* INPROC client */
typedef struct inproc_client {
    void* server_endpoint;
    void* client_transport;
    int is_active;
    
    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
} inproc_client_t;

/* Global endpoint list */
static inproc_endpoint_t* g_endpoint_list = NULL;

/* Simple hash table for endpoints */
#define ENDPOINT_HASH_SIZE 32
static inproc_endpoint_t* g_endpoint_hash[UVBUS_HASH_TABLE_SIZE] = {NULL};

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
static inproc_endpoint_t* inproc_find_endpoint(const char* name) {
    pthread_mutex_lock(&g_endpoint_mutex);
    unsigned int hash = hash_string(name);
    inproc_endpoint_t* endpoint = g_endpoint_hash[hash];
    
    while (endpoint) {
        if (strcmp(endpoint->name, name) == 0) {
            pthread_mutex_unlock(&g_endpoint_mutex);
            return endpoint;
        }
        endpoint = endpoint->next;
    }
    
    pthread_mutex_unlock(&g_endpoint_mutex);
    return NULL;
}

/* Add endpoint to hash table */
static void inproc_add_endpoint(inproc_endpoint_t* endpoint) {
    pthread_mutex_lock(&g_endpoint_mutex);
    unsigned int hash = hash_string(endpoint->name);
    endpoint->next = g_endpoint_hash[hash];
    g_endpoint_hash[hash] = endpoint;
    pthread_mutex_unlock(&g_endpoint_mutex);
}

/* Remove endpoint from hash table */
static void inproc_remove_endpoint(inproc_endpoint_t* endpoint) {
    pthread_mutex_lock(&g_endpoint_mutex);
    unsigned int hash = hash_string(endpoint->name);
    inproc_endpoint_t** ptr = &g_endpoint_hash[hash];
    
    while (*ptr) {
        if (*ptr == endpoint) {
            *ptr = endpoint->next;
            pthread_mutex_unlock(&g_endpoint_mutex);
            return;
        }
        ptr = &(*ptr)->next;
    }
    pthread_mutex_unlock(&g_endpoint_mutex);
}

/* Add client to endpoint */
static void inproc_add_client(inproc_endpoint_t* endpoint, void* client) {
    pthread_mutex_lock(&g_endpoint_mutex);
    if (endpoint->client_count >= endpoint->client_capacity) {
        endpoint->client_capacity *= 2;
        endpoint->clients = (void**)uvrpc_realloc(
            endpoint->clients, 
            sizeof(void*) * endpoint->client_capacity
        );
    }
    
    endpoint->clients[endpoint->client_count++] = client;
    pthread_mutex_unlock(&g_endpoint_mutex);
}

/* Remove client from endpoint */
static void inproc_remove_client(inproc_endpoint_t* endpoint, void* client) {
    pthread_mutex_lock(&g_endpoint_mutex);
    for (int i = 0; i < endpoint->client_count; i++) {
        if (endpoint->clients[i] == client) {
            /* Shift remaining clients */
            for (int j = i; j < endpoint->client_count - 1; j++) {
                endpoint->clients[j] = endpoint->clients[j + 1];
            }
            endpoint->client_count--;
            pthread_mutex_unlock(&g_endpoint_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&g_endpoint_mutex);
}

/* Send to all clients */
static void inproc_send_to_all(inproc_endpoint_t* endpoint,
                         const uint8_t* data, size_t size) {
    pthread_mutex_lock(&g_endpoint_mutex);
    /* Copy client count to avoid holding lock during callbacks */
    int client_count = endpoint->client_count;
    void** clients = (void**)uvrpc_alloc(sizeof(void*) * client_count);
    if (clients) {
        memcpy(clients, endpoint->clients, sizeof(void*) * client_count);
    }
    pthread_mutex_unlock(&g_endpoint_mutex);
    
    if (!clients) return;
    
    /* Trigger callbacks without holding the lock */
    for (int i = 0; i < client_count; i++) {
        inproc_client_t* client = (inproc_client_t*)clients[i];
        if (client && client->is_active) {
            /* Use client's own callback instead of endpoint's */
            if (client->recv_cb) {
                client->recv_cb(data, size, client->callback_ctx);
            }
        }
    }
    
    uvrpc_free(clients);
}

/* INPROC vtable functions */
static int inproc_listen(void* impl_ptr, const char* address);
static int inproc_connect(void* impl_ptr, const char* address);
static void inproc_disconnect(void* impl_ptr);
static int inproc_send(void* impl_ptr, const uint8_t* data, size_t size);
static int inproc_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void inproc_free(void* impl_ptr);

/* Global vtable for INPROC */
static const uvbus_transport_vtable_t inproc_vtable = {
    .listen = inproc_listen,
    .connect = inproc_connect,
    .disconnect = inproc_disconnect,
    .send = inproc_send,
    .send_to = inproc_send_to,
    .free = inproc_free
};

/* INPROC listen implementation */
static int inproc_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "inproc://", 9) == 0) {
        name += 9;
    }
    
    /* Check if endpoint already exists */
    inproc_endpoint_t* endpoint = inproc_find_endpoint(name);
    if (endpoint) {
        return UVBUS_ERROR_ALREADY_EXISTS;
    }
    
    /* Create endpoint */
    endpoint = (inproc_endpoint_t*)uvrpc_alloc(sizeof(inproc_endpoint_t));
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
    inproc_add_endpoint(endpoint);
    
    transport->impl.inproc_server = (void*)endpoint;
    transport->is_connected = 1;
    
    return UVBUS_OK;
}

/* INPROC connect implementation */
static int inproc_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "inproc://", 9) == 0) {
        name += 9;
    }
    
    /* Find endpoint */
    inproc_endpoint_t* endpoint = inproc_find_endpoint(name);
    if (!endpoint) {
        return UVBUS_ERROR_NOT_FOUND;
    }
    
    /* Create client */
    inproc_client_t* client = (inproc_client_t*)uvrpc_alloc(sizeof(inproc_client_t));
    if (!client) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    client->server_endpoint = endpoint;
    client->client_transport = transport;
    client->is_active = 1;
    
    /* Set client's own callbacks */
    client->recv_cb = transport->recv_cb;
    client->error_cb = transport->error_cb;
    client->callback_ctx = transport->callback_ctx;
    
    /* Add to endpoint */
    inproc_add_client(endpoint, client);
    
    transport->impl.inproc_client = (void*)client;
    transport->is_connected = 1;
    
    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* INPROC disconnect implementation */
static void inproc_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.inproc_server) {
        inproc_endpoint_t* endpoint = (inproc_endpoint_t*)transport->impl.inproc_server;
        /* Remove endpoint from global list */
        inproc_remove_endpoint(endpoint);
        
        /* Free all clients */
        for (int i = 0; i < endpoint->client_count; i++) {
            if (endpoint->clients[i]) {
                inproc_client_t* client = (inproc_client_t*)endpoint->clients[i];
                client->is_active = 0;
            }
        }
        
        /* Free endpoint */
        uvrpc_free(endpoint->name);
        uvrpc_free(endpoint->clients);
        uvrpc_free(endpoint);
        transport->impl.inproc_server = NULL;
    } else if (!transport->is_server && transport->impl.inproc_client) {
        inproc_client_t* client = (inproc_client_t*)transport->impl.inproc_client;
        /* Remove from endpoint */
        if (client->server_endpoint) {
            inproc_remove_client(client->server_endpoint, client);
        }
        
        client->is_active = 0;
        uvrpc_free(client);
        transport->impl.inproc_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* INPROC send implementation */
static int inproc_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (transport->is_server && transport->impl.inproc_server) {
        inproc_endpoint_t* endpoint = (inproc_endpoint_t*)transport->impl.inproc_server;
        /* Broadcast to all clients */
        inproc_send_to_all(endpoint, data, size);
    } else if (!transport->is_server && transport->impl.inproc_client) {
        inproc_client_t* client = (inproc_client_t*)transport->impl.inproc_client;
        if (client->server_endpoint) {
            /* Send to server */
            inproc_endpoint_t* endpoint = client->server_endpoint;
            if (transport->recv_cb) {
                transport->recv_cb(data, size, transport->callback_ctx);
            }
        }
    }
    
    return UVBUS_OK;
}

/* INPROC send to specific client implementation */
static int inproc_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    inproc_client_t* client = (inproc_client_t*)target;
    if (!client || !client->is_active) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->recv_cb) {
        transport->recv_cb(data, size, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* INPROC free implementation */
static void inproc_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    inproc_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}

/* Export function to create INPROC transport */
uvbus_transport_t* create_inproc_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &inproc_vtable;
    
    return transport;
}