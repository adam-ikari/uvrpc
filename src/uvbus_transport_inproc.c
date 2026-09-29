/**
 * UVBus INPROC Transport Implementation (In-Process)
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include "uvbus_loop_registry.h"
#include <string.h>
#include <stdlib.h>

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
    int ref_count;  /* Reference count for cleanup */

    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
} inproc_client_t;

/* Hash function (per-loop registry stores endpoints in a hashed linked list) */
#define UVBUS_INPROC_HASH_SIZE UVBUS_HASH_TABLE_SIZE
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % UVBUS_INPROC_HASH_SIZE;
}

/* Per-loop endpoint buckets. The registry (uvbus_loop_registry_t) holds an
 * array of inproc_endpoint_t* list heads; this returns the head for a name. */
static inproc_endpoint_t** inproc_bucket(uvbus_loop_registry_t* reg, const char* name) {
    /* inproc_endpoints is stored as an array of UVBUS_INPROC_HASH_SIZE list heads. */
    inproc_endpoint_t** buckets = (inproc_endpoint_t**)reg->inproc_endpoints;
    return &buckets[hash_string(name)];
}

/* Ensure the per-loop registry has an INPROC bucket array. */
static inproc_endpoint_t** inproc_buckets_ensure(uvbus_loop_registry_t* reg) {
    if (!reg->inproc_endpoints) {
        reg->inproc_endpoints = uvrpc_calloc(UVBUS_INPROC_HASH_SIZE, sizeof(inproc_endpoint_t*));
    }
    return (inproc_endpoint_t**)reg->inproc_endpoints;
}

/* Find endpoint by name in the per-loop registry (no lock: single-threaded). */
static inproc_endpoint_t* inproc_find_endpoint(uvbus_loop_registry_t* reg, const char* name) {
    if (!reg) return NULL;
    inproc_endpoint_t** buckets = (inproc_endpoint_t**)reg->inproc_endpoints;
    if (!buckets) return NULL;
    inproc_endpoint_t* endpoint = buckets[hash_string(name)];
    while (endpoint) {
        if (strcmp(endpoint->name, name) == 0) {
            return endpoint;
        }
        endpoint = endpoint->next;
    }
    return NULL;
}

/* Add endpoint to the per-loop registry (no lock). */
static void inproc_add_endpoint(uvbus_loop_registry_t* reg, inproc_endpoint_t* endpoint) {
    inproc_endpoint_t** head = inproc_bucket(reg, endpoint->name);
    endpoint->next = *head;
    *head = endpoint;
}

/* Remove endpoint from the per-loop registry (no lock). */
static void inproc_remove_endpoint(uvbus_loop_registry_t* reg, inproc_endpoint_t* endpoint) {
    inproc_endpoint_t** head = inproc_bucket(reg, endpoint->name);
    inproc_endpoint_t** ptr = head;
    while (*ptr) {
        if (*ptr == endpoint) {
            *ptr = endpoint->next;
            endpoint->next = NULL;
            return;
        }
        ptr = &(*ptr)->next;
    }
}

/* Add client to endpoint (no lock: single-threaded; the endpoint is owned by
 * this loop and the caller holds no recv_cb re-entrancy here). */
static void inproc_add_client(inproc_endpoint_t* endpoint, void* client) {
    if (endpoint->client_count >= endpoint->client_capacity) {
        endpoint->client_capacity *= 2;
        endpoint->clients = (void**)uvrpc_realloc(
            endpoint->clients,
            sizeof(void*) * endpoint->client_capacity
        );
    }
    endpoint->clients[endpoint->client_count++] = client;
}

/* Remove client from endpoint (no lock). */
static void inproc_remove_client(inproc_endpoint_t* endpoint, void* client) {
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

/* Send to all clients. Snapshots the client list before delivering, because a
 * recv_cb runs inline and could disconnect a client (mutating the list). No
 * lock is needed — the loop is single-threaded — but the snapshot guards
 * against re-entrant mutation. Returns UVBUS_ERROR_NO_MEMORY if the snapshot
 * allocation fails (so callers can report it); UVBUS_OK otherwise. */
static uvbus_error_t inproc_send_to_all(inproc_endpoint_t* endpoint,
                         const uint8_t* data, size_t size) {
    int client_count = endpoint->client_count;
    if (client_count == 0) return UVBUS_OK;
    void** clients = (void**)uvrpc_alloc(sizeof(void*) * client_count);
    if (!clients) return UVBUS_ERROR_NO_MEMORY;
    memcpy(clients, endpoint->clients, sizeof(void*) * client_count);

    for (int i = 0; i < client_count; i++) {
        inproc_client_t* client = (inproc_client_t*)clients[i];
        if (client && client->is_active && client->recv_cb) {
            client->recv_cb(data, size, client->callback_ctx, client->callback_ctx);
        }
    }
    uvrpc_free(clients);
    return UVBUS_OK;
}

/* INPROC vtable functions */
static int inproc_listen(void* impl_ptr, const char* address);
static int inproc_connect(void* impl_ptr, const char* address);
static void inproc_disconnect(void* impl_ptr);
static int inproc_send(void* impl_ptr, const uint8_t* data, size_t size);
static int inproc_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static int inproc_broadcast(void* impl_ptr, const uint8_t* data, size_t size);
static void inproc_free(void* impl_ptr);

/* Global vtable for INPROC */
static const uvbus_transport_vtable_t inproc_vtable = {
    .listen = inproc_listen,
    .connect = inproc_connect,
    .disconnect = inproc_disconnect,
    .send = inproc_send,
    .send_to = inproc_send_to,
    .broadcast = inproc_broadcast,
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

    /* The registry is supplied by the caller; there is no hidden per-loop or
     * process-wide state to fall back to, so a missing one is a usage error
     * worth naming. */
    if (!transport->registry) {
        UVBUS_LOG_ERROR("inproc needs a registry: create one with "
                        "uvbus_loop_registry_new() and pass it to every config "
                        "that should see each other via "
                        "uvbus_config_set_loop_registry()");
        return UVBUS_ERROR_INVALID_PARAM;
    }
    uvbus_loop_registry_t* reg = uvbus_loop_registry_retain(transport->registry);
    if (!reg) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    if (!inproc_buckets_ensure(reg)) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "inproc://", 9) == 0) {
        name += 9;
    }

    /* Check if endpoint already exists */
    inproc_endpoint_t* endpoint = inproc_find_endpoint(reg, name);
    if (endpoint) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_ALREADY_EXISTS;
    }

    /* Create endpoint */
    endpoint = (inproc_endpoint_t*)uvrpc_alloc(sizeof(inproc_endpoint_t));
    if (!endpoint) {
        uvbus_loop_registry_release(transport->registry);
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
    endpoint->callback_ctx = transport->recv_ctx;

    /* Add to per-loop registry */
    inproc_add_endpoint(reg, endpoint);

    transport->impl.inproc_server = (void*)endpoint;
    transport->is_connected = 1;

    /* Set bus as active */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }

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

    /* Obtain the per-loop registry (retained so it stays alive for this
     * client's lifetime; released in inproc_free). */
    uvbus_loop_registry_t* reg = uvbus_loop_registry_retain(transport->registry);
    if (!reg) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Skip protocol prefix */
    const char* name = address;
    if (strncmp(address, "inproc://", 9) == 0) {
        name += 9;
    }

    /* Find endpoint */
    inproc_endpoint_t* endpoint = inproc_find_endpoint(reg, name);
    if (!endpoint) {
        /* INPROC is for in-process communication only.
         * If the endpoint is not found, it means the server is not running
         * in the same process/loop. */
        UVBUS_LOG_ERROR("Endpoint '%s' not found. INPROC transport is for "
                        "in-process communication only; the server must run in "
                        "the same process (and same loop) as the client.", name);
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NOT_FOUND;
    }


    /* Create client */
    inproc_client_t* client = (inproc_client_t*)uvrpc_alloc(sizeof(inproc_client_t));
    if (!client) {
        uvbus_loop_registry_release(transport->registry);
        return UVBUS_ERROR_NO_MEMORY;
    }

    client->server_endpoint = endpoint;
    client->client_transport = transport;
    client->is_active = 1;
    client->ref_count = 1;  /* Initialize reference count */

    /* Set client's own callbacks */
    client->recv_cb = transport->recv_cb;
    client->error_cb = transport->error_cb;
    client->callback_ctx = transport->recv_ctx;

    /* Add to endpoint */
    inproc_add_client(endpoint, client);

    transport->impl.inproc_client = (void*)client;
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
static void inproc_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }

    uvbus_loop_registry_t* reg = transport->registry;

    if (transport->is_server && transport->impl.inproc_server) {
        inproc_endpoint_t* endpoint = (inproc_endpoint_t*)transport->impl.inproc_server;
        /* Remove endpoint from per-loop registry */
        if (reg) {
            inproc_remove_endpoint(reg, endpoint);
        }

        /* Detach all clients from this endpoint so a later client disconnect
         * does not dereference the endpoint we are about to free. We do NOT
         * free the client structs here — each client transport owns its own
         * inproc_client_t and will free it (and release its registry ref) when
         * it is disconnected. This avoids both a double-free and a registry
         * refcount imbalance when the server is freed before its clients. */
        for (int i = 0; i < endpoint->client_count; i++) {
            inproc_client_t* client = (inproc_client_t*)endpoint->clients[i];
            if (client) {
                client->is_active = 0;
                client->server_endpoint = NULL;  /* detach; endpoint is going away */
            }
        }

        /* Free endpoint */
        uvrpc_free(endpoint->name);
        uvrpc_free(endpoint->clients);
        uvrpc_free(endpoint);
        transport->impl.inproc_server = NULL;

        /* Release the per-loop registry reference taken in listen. */
        uvbus_loop_registry_release(transport->registry);
    } else if (!transport->is_server && transport->impl.inproc_client) {
        inproc_client_t* client = (inproc_client_t*)transport->impl.inproc_client;
        /* Remove from endpoint */
        if (client->server_endpoint) {
            inproc_remove_client(client->server_endpoint, client);
        }

        client->is_active = 0;
        /* Decrement reference count, free if reaches zero */
        if (--client->ref_count == 0) {
            uvrpc_free(client);
        }
        transport->impl.inproc_client = NULL;

        /* Release the per-loop registry reference taken in connect. */
        uvbus_loop_registry_release(transport->registry);
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
        return inproc_send_to_all(endpoint, data, size);
    } else if (!transport->is_server && transport->impl.inproc_client) {
        inproc_client_t* client = (inproc_client_t*)transport->impl.inproc_client;
        if (client->server_endpoint) {
            inproc_endpoint_t* endpoint = client->server_endpoint;
            if (endpoint->recv_cb) {
                void* server_ctx = NULL;
                if (endpoint->server_transport) {
                    server_ctx = ((uvbus_transport_t*)endpoint->server_transport)->recv_ctx;
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
    
    /* Call the client's callback, not the server's callback */
    if (client->recv_cb) {
        /* Pass client's callback_ctx as both client_ctx and server_ctx */
        client->recv_cb(data, size, client->callback_ctx, client->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* INPROC broadcast implementation - zero-copy, pointer-passing to all clients */
static int inproc_broadcast(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport || !data || size == 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server || !transport->impl.inproc_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    inproc_endpoint_t* endpoint = (inproc_endpoint_t*)transport->impl.inproc_server;

    /* Zero-copy broadcast: snapshot the client list, then deliver directly.
     * The snapshot guards against re-entrant mutation (a recv_cb running
     * inline could disconnect a client mid-iteration). No lock is needed — the
     * loop is single-threaded by the framework's design contract. */
    return inproc_send_to_all(endpoint, data, size);
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
    
    /* Set fast path function pointers */
    transport->fast_send = inproc_send;
    transport->fast_send_to = inproc_send_to;
    
    return transport;
}