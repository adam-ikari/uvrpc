/**
 * UVRPC - Ultra-Fast RPC Framework
 * Design: libuv + NNG + msgpack
 * Philosophy: Simple, Fast, Transparent
 */

#ifndef UVRPC_H
#define UVRPC_H

#include <uv.h>
#include <nng/nng.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define UVRPC_OK 0
#define UVRPC_ERROR -1
#define UVRPC_ERROR_INVALID_PARAM -2
#define UVRPC_ERROR_NO_MEMORY -3

/* Configuration */
struct uvrpc_config {
    uv_loop_t* loop;
    char* address;
};
typedef struct uvrpc_config uvrpc_config_t;

uvrpc_config_t* uvrpc_config_new(void);
void uvrpc_config_free(uvrpc_config_t* config);
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);

/* Server */
struct uvrpc_server {
    uv_loop_t* loop;
    char* address;
    nng_socket sock;
    nng_listener listener;
    int has_listener;
    void* services;
};
typedef struct uvrpc_server uvrpc_server_t;

typedef void (*uvrpc_handler_t)(uvrpc_server_t* server, const char* service, const char* method,
                                 const uint8_t* data, size_t size, void* ctx);

uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
int uvrpc_server_start(uvrpc_server_t* server);
void uvrpc_server_stop(uvrpc_server_t* server);
void uvrpc_server_free(uvrpc_server_t* server);
int uvrpc_server_register(uvrpc_server_t* server, const char* name, uvrpc_handler_t handler, void* ctx);
int uvrpc_server_run(uvrpc_server_t* server);

/* Client */
struct uvrpc_client {
    uv_loop_t* loop;
    char* address;
    nng_socket sock;
    nng_dialer dialer;
    int has_dialer;
};
typedef struct uvrpc_client uvrpc_client_t;

typedef void (*uvrpc_callback_t)(int status, const uint8_t* data, size_t size, void* ctx);

uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);
int uvrpc_client_connect(uvrpc_client_t* client);
void uvrpc_client_disconnect(uvrpc_client_t* client);
void uvrpc_client_free(uvrpc_client_t* client);
int uvrpc_client_call(uvrpc_client_t* client, const char* service, const char* method,
                       const uint8_t* data, size_t size, uvrpc_callback_t callback, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */
