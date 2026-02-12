/**
 * UVRPC Server - Complete Implementation
 * libuv + NNG + FlatCC
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include "uvrpc_msgpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uthash.h>

/* NNG initialization */
static int g_nng_initialized = 0;

/* Service registry */
typedef struct service_entry {
    char* name;
    uvrpc_handler_t handler;
    void* ctx;
    UT_hash_handle hh;
} service_entry_t;

static int init_nng(void) {
    if (g_nng_initialized) return 0;
    
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    params.num_task_threads = 0;
    params.max_task_threads = 0;
    
    if (nng_init(&params) != 0) return -1;
    
    g_nng_initialized = 1;
    return 0;
}

uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    if (init_nng() != 0) return NULL;
    
    uvrpc_server_t* server = (uvrpc_server_t*)calloc(1, sizeof(uvrpc_server_t));
    if (!server) return NULL;
    
    server->loop = config->loop;
    server->address = strdup(config->address);
    server->has_listener = 0;
    server->has_poll = 0;
    server->owns_loop = 0;
    
    if (nng_rep0_open(&server->sock) != 0) {
        free(server->address);
        free(server);
        return NULL;
    }
    
    return server;
}

int uvrpc_server_start(uvrpc_server_t* server) {
    if (!server || !server->address) return -1;
    
    int rv;
    if ((rv = nng_listener_create(&server->listener, server->sock, server->address)) != 0) {
        fprintf(stderr, "nng_listener_create failed: %d\n", rv);
        return -2;
    }
    
    if ((rv = nng_listener_start(server->listener, 0)) != 0) {
        fprintf(stderr, "nng_listener_start failed: %d\n", rv);
        nng_listener_close(server->listener);
        return -3;
    }
    
    server->has_listener = 1;
    printf("Server started on %s\n", server->address);
    return 0;
}

void uvrpc_server_stop(uvrpc_server_t* server) {
    if (!server) return;
    
    if (server->has_listener) {
        nng_listener_close(server->listener);
        server->has_listener = 0;
    }
}

void uvrpc_server_free(uvrpc_server_t* server) {
    if (!server) return;
    
    uvrpc_server_stop(server);
    nng_socket_close(server->sock);
    
    if (server->owns_loop) {
        uv_loop_close(server->loop);
        free(server->loop);
    }
    
    /* Free services */
    service_entry_t* entry, *tmp;
    HASH_ITER(hh, (service_entry_t*)server->services, entry, tmp) {
        service_entry_t* serv = (service_entry_t*)server->services; HASH_DEL(serv, entry); server->services = serv;
        free(entry->name);
        free(entry);
    }
    
    free(server->address);
    free(server);
}

int uvrpc_server_register(uvrpc_server_t* server, const char* name, uvrpc_handler_t handler, void* ctx) {
    if (!server || !name || !handler) return -1;
    
    service_entry_t* entry = NULL;
    service_entry_t* serv = (service_entry_t*)server->services; HASH_FIND_STR(serv, name, entry);
    if (entry) return -2;
    
    entry = (service_entry_t*)calloc(1, sizeof(service_entry_t));
    if (!entry) return -3;
    
    entry->name = strdup(name);
    entry->handler = handler;
    entry->ctx = ctx;
    
    HASH_ADD_STR(serv, name, entry); server->services = serv;
    
    return 0;
}

/* Process incoming messages (called from event loop) */
void uvrpc_server_process(uvrpc_server_t* server) {
    if (!server || !server->has_listener) return;
    
    nng_msg* msg = NULL;
    while (nng_recvmsg(server->sock, &msg, NNG_FLAG_NONBLOCK) == 0) {
        /* Process request */
        size_t msg_size = nng_msg_len(msg);
        const char* msg_buf = (const char*)nng_msg_body(msg);
        
        char* service = NULL;
        char* method = NULL;
        const uint8_t* data = NULL;
        size_t data_size = 0;
        
        if (uvrpc_unpack_request(msg_buf, msg_size, &service, &method, &data, &data_size) == 0) {
            /* Find handler */
            service_entry_t* entry = NULL;
            service_entry_t* serv = (service_entry_t*)server->services;
            HASH_FIND_STR(serv, service, entry);
            
            if (entry && entry->handler) {
                entry->handler(server, service, method, data, data_size, entry->ctx);
                
                /* Send response */
                size_t resp_size = 0;
                char* resp_buf = uvrpc_pack_response(0, data, data_size, &resp_size);
                if (resp_buf) {
                    nng_msg* reply = NULL;
                    nng_msg_alloc(&reply, resp_size);
                    memcpy(nng_msg_body(reply), resp_buf, resp_size);
                    nng_sendmsg(server->sock, reply, 0);
                    free(resp_buf);
                }
            }
            
            if (service) free(service);
            if (method) free(method);
        }
        
        nng_msg_free(msg);
    }
}
