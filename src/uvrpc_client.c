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

/* Pending response context */
typedef struct pending_response {
    uvrpc_callback_t callback;
    void* ctx;
    struct pending_response* next;
} pending_response_t;

static pending_response_t* g_pending_list = NULL;

/* AIO context */
typedef struct aio_ctx {
    uvrpc_client_t* client;
    nng_aio* aio;
} aio_ctx_t;

/* AIO callback for receive */
void aio_recv_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    int rv = nng_aio_result(ctx->aio);
    if (rv == 0) {
        nng_msg* reply = nng_aio_get_msg(ctx->aio);
        if (reply) {
            /* Unpack response */
            int resp_status = 0;
            const uint8_t* resp_data = NULL;
            size_t resp_size = 0;
            
            size_t reply_size = nng_msg_len(reply);
            const char* reply_buf = (const char*)nng_msg_body(reply);
            
            if (uvrpc_unpack_response(reply_buf, reply_size, &resp_status, &resp_data, &resp_size) == 0) {
                /* Find matching pending response */
                pending_response_t** p = &g_pending_list;
                while (*p) {
                    if ((*p)->callback) {
                        (*p)->callback(resp_status, resp_data, resp_size, (*p)->ctx);
                        break;
                    }
                    p = &(*p)->next;
                }
            }
            
            nng_msg_free(reply);
        }
    }
}

/* AIO callback for send */
void aio_send_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    /* Start receive after send */
    nng_recv_aio(ctx->client->sock, ctx->aio);
}

static int init_nng(void) {
    if (g_nng_initialized) return 0;
    
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    /* Enable task threads for AIO */
    params.num_task_threads = 2;
    params.max_task_threads = 2;
    
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
    
    /* Add to pending list */
    pending_response_t* pending = (pending_response_t*)calloc(1, sizeof(pending_response_t));
    pending->callback = callback;
    pending->ctx = ctx;
    pending->next = g_pending_list;
    g_pending_list = pending;
    
    /* Create AIO context */
    aio_ctx_t* aio_ctx = (aio_ctx_t*)calloc(1, sizeof(aio_ctx_t));
    aio_ctx->client = client;
    
    if (nng_aio_alloc(&aio_ctx->aio, aio_send_callback, aio_ctx) != 0) {
        free(pending);
        free(buffer);
        return -3;
    }
    
    /* Prepare message */
    nng_msg* msg = NULL;
    nng_msg_alloc(&msg, buf_size);
    memcpy(nng_msg_body(msg), buffer, buf_size);
    free(buffer);
    
    /* Send message */
    nng_aio_set_msg(aio_ctx->aio, msg);
    nng_send_aio(client->sock, aio_ctx->aio);
    
    return 0;
}

/* Process pending responses (called from event loop) */
void uvrpc_client_process(uvrpc_client_t* client) {
    if (!client) return;
    
    /* Just run event loop to process callbacks */
    (void)client;
}