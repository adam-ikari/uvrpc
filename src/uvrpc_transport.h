/**
 * UVRPC Transport Layer - libuv Multi-Protocol
 * Supports TCP, UDP, IPC, INPROC
 */

#ifndef UVRPC_TRANSPORT_H
#define UVRPC_TRANSPORT_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct uvrpc_transport uvrpc_transport_t;

/* Callback types */
typedef void (*uvrpc_recv_callback_t)(uint8_t* data, size_t size, void* ctx);
typedef void (*uvrpc_connect_callback_t)(int status, void* ctx);
typedef void (*uvrpc_close_callback_t)(void* ctx);
typedef void (*uvrpc_error_callback_t)(int error_code, const char* error_msg, void* ctx);

/* Transport creation */
uvrpc_transport_t* uvrpc_transport_server_new(uv_loop_t* loop, int transport_type);
uvrpc_transport_t* uvrpc_transport_client_new(uv_loop_t* loop, int transport_type);
void uvrpc_transport_free(uvrpc_transport_t* transport);

/* Server operations */
int uvrpc_transport_listen(uvrpc_transport_t* transport, const char* address,
                            uvrpc_recv_callback_t recv_cb, void* ctx);
void uvrpc_transport_send(uvrpc_transport_t* transport, const uint8_t* data, size_t size);

/* Client operations */
int uvrpc_transport_connect(uvrpc_transport_t* transport, const char* address,
                             uvrpc_connect_callback_t connect_cb,
                             uvrpc_recv_callback_t recv_cb, void* ctx);
void uvrpc_transport_disconnect(uvrpc_transport_t* transport);

/* Error handling */
void uvrpc_transport_set_error_callback(uvrpc_transport_t* transport, uvrpc_error_callback_t error_cb);

/* Connection timeout */
void uvrpc_transport_set_timeout(uvrpc_transport_t* transport, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_TRANSPORT_H */