/**
 * UVRPC Client - Complete Implementation
 * libuv + NNG + FlatCC
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include "uvrpc_msgpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NNG initialization */
static int g_nng_initialized = 0;

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

uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) return NULL;
    
    if (init_nng() != 0) return NULL;
    
    uvrpc_client_t* client = (uvrpc_client_t*)calloc(1, sizeof(uvrpc_client_t));
    if (!client) return NULL;
    
    client->loop = config->loop;
    client->address = strdup(config->address);
    client->has_dialer = 0;
    client->has_poll = 0;
    client->owns_loop = 0;
    client->pending_callback = NULL;
    client->pending_ctx = NULL;
    
    if (nng_req0_open(&client->sock) != 0) {
        free(client->address);
        free(client);
        return NULL;
    }
    
    return client;
}

int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client || !client->address) return -1;
    
    if (nng_dialer_create(&client->dialer, client->sock, client->address) != 0) {
        return -2;
    }
    
    if (nng_dialer_start(client->dialer, 0) != 0) {
        nng_dialer_close(client->dialer);
        return -3;
    }
    
    client->has_dialer = 1;
    client->has_poll = 1;
    return 0;
}

void uvrpc_client_disconnect(uvrpc_client_t* client) {
    if (!client) return;
    
    if (client->has_dialer) {
        nng_dialer_close(client->dialer);
        client->has_dialer = 0;
    }
    
    client->has_poll = 0;
}

void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) return;
    
    uvrpc_client_disconnect(client);
    nng_socket_close(client->sock);
    
    if (client->owns_loop) {
        uv_loop_close(client->loop);
        free(client->loop);
    }
    
    free(client->address);
    free(client);
}

int uvrpc_client_call(uvrpc_client_t* client, const char* service, const char* method,
                       const uint8_t* data, size_t size, uvrpc_callback_t callback, void* ctx) {
    if (!client || !service || !method) return -1;
    
    /* Pack request */
    size_t buf_size = 0;
    char* buffer = uvrpc_pack_request(service, method, data, size, &buf_size);
    if (!buffer) return -2;
    
    /* Store callback for response */
    client->pending_callback = callback;
    client->pending_ctx = ctx;
    
    /* Send request */
    nng_msg* msg = NULL;
    nng_msg_alloc(&msg, buf_size);
    memcpy(nng_msg_body(msg), buffer, buf_size);
    free(buffer);
    
    int rv = nng_sendmsg(client->sock, msg, NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        nng_msg_free(msg);
        client->pending_callback = NULL;
        client->pending_ctx = NULL;
        return -3;
    }
    
    return 0;
}

/* Process incoming responses (called from event loop) */
void uvrpc_client_process(uvrpc_client_t* client) {
    if (!client || !client->has_dialer) return;
    
    nng_msg* reply = NULL;
    while (nng_recvmsg(client->sock, &reply, NNG_FLAG_NONBLOCK) == 0) {
        /* Unpack response */
        int resp_status = 0;
        const uint8_t* resp_data = NULL;
        size_t resp_size = 0;
        
        size_t reply_size = nng_msg_len(reply);
        const char* reply_buf = (const char*)nng_msg_body(reply);
        
        if (uvrpc_unpack_response(reply_buf, reply_size, &resp_status, &resp_data, &resp_size) == 0) {
            if (client->pending_callback) {
                client->pending_callback(resp_status, resp_data, resp_size, client->pending_ctx);
                client->pending_callback = NULL;
                client->pending_ctx = NULL;
            }
        }
        
        nng_msg_free(reply);
    }
}