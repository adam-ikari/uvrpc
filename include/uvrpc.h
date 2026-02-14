/**
 * UVRPC - Ultra-Fast RPC Framework
 * Design: libuv + FlatCC
 * Philosophy: Zero threads, Zero locks, Zero global variables
 *             All I/O managed by libuv event loop
 */

#ifndef UVRPC_H
#define UVRPC_H

#include <uv.h>
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
#define UVRPC_ERROR_NOT_CONNECTED -4
#define UVRPC_ERROR_TIMEOUT -5
#define UVRPC_ERROR_TRANSPORT -6
#define UVRPC_ERROR_CALLBACK_LIMIT -7

/* Configuration constants */
#ifndef UVRPC_MAX_PENDING_CALLBACKS
#define UVRPC_MAX_PENDING_CALLBACKS 10000  /* Max concurrent pending callbacks (ring buffer size) */
#endif

/* Transport types */
typedef enum {
    UVRPC_TRANSPORT_TCP = 0,    /* TCP network transport */
    UVRPC_TRANSPORT_UDP = 1,    /* UDP transport */
    UVRPC_TRANSPORT_IPC = 2,    /* Unix domain socket (IPC) */
    UVRPC_TRANSPORT_INPROC = 3  /* In-process transport */
} uvrpc_transport_type;

/* Communication types */
typedef enum {
    UVRPC_COMM_SERVER_CLIENT = 0,  /* Server/Client (request-response) */
    UVRPC_COMM_BROADCAST = 1        /* Broadcast (pub-sub) */
} uvrpc_comm_type_t;


/* Forward declarations */
typedef struct uvrpc_config uvrpc_config_t;
typedef struct uvrpc_server uvrpc_server_t;
typedef struct uvrpc_client uvrpc_client_t;
typedef struct uvrpc_publisher uvrpc_publisher_t;
typedef struct uvrpc_subscriber uvrpc_subscriber_t;
typedef struct uvrpc_request uvrpc_request_t;
typedef struct uvrpc_response uvrpc_response_t;

/* Callback types */
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);
typedef void (*uvrpc_callback_t)(uvrpc_response_t* resp, void* ctx);
typedef void (*uvrpc_connect_callback_t)(int status, void* ctx);

/* Broadcast callback types */
typedef void (*uvrpc_publish_callback_t)(int status, void* ctx);
typedef void (*uvrpc_subscribe_callback_t)(const char* topic, const uint8_t* data, size_t size, void* ctx);

typedef void (*uvrpc_error_callback_t)(int error_code, const char* error_msg, void* ctx);

/* Configuration */
struct uvrpc_config {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_type transport;
    uvrpc_comm_type_t comm_type;
};

/* Request structure */
struct uvrpc_request {
    uvrpc_server_t* server;
    uint32_t msgid;
    char* method;
    uint8_t* params;
    size_t params_size;
    uv_stream_t* client_stream;  /* Client connection for sending response */
    void* user_data;
};

/* Response structure */
struct uvrpc_response {
    int status;
    uint32_t msgid;
    int32_t error_code;
    uint8_t* result;
    size_t result_size;
    void* user_data;
};

/* Configuration API */
uvrpc_config_t* uvrpc_config_new(void);
void uvrpc_config_free(uvrpc_config_t* config);
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_type transport);
uvrpc_config_t* uvrpc_config_set_comm_type(uvrpc_config_t* config, uvrpc_comm_type_t comm_type);

/* Server API */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
int uvrpc_server_start(uvrpc_server_t* server);
void uvrpc_server_stop(uvrpc_server_t* server);
void uvrpc_server_free(uvrpc_server_t* server);
int uvrpc_server_register(uvrpc_server_t* server, const char* method, uvrpc_handler_t handler, void* ctx);

/* Client API */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);
int uvrpc_client_connect(uvrpc_client_t* client);
int uvrpc_client_connect_with_callback(uvrpc_client_t* client, 
                                         uvrpc_connect_callback_t callback, void* ctx);
void uvrpc_client_disconnect(uvrpc_client_t* client);
void uvrpc_client_free(uvrpc_client_t* client);
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx);

/* Request/Response API */
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size);
void uvrpc_request_free(uvrpc_request_t* req);
void uvrpc_response_free(uvrpc_response_t* resp);

/* Publisher API (Broadcast) */
uvrpc_publisher_t* uvrpc_publisher_create(uvrpc_config_t* config);
int uvrpc_publisher_start(uvrpc_publisher_t* publisher);
void uvrpc_publisher_stop(uvrpc_publisher_t* publisher);
void uvrpc_publisher_free(uvrpc_publisher_t* publisher);
int uvrpc_publisher_publish(uvrpc_publisher_t* publisher, const char* topic,
                             const uint8_t* data, size_t size,
                             uvrpc_publish_callback_t callback, void* ctx);

/* Subscriber API (Broadcast) */
uvrpc_subscriber_t* uvrpc_subscriber_create(uvrpc_config_t* config);
int uvrpc_subscriber_connect(uvrpc_subscriber_t* subscriber);
void uvrpc_subscriber_disconnect(uvrpc_subscriber_t* subscriber);
void uvrpc_subscriber_free(uvrpc_subscriber_t* subscriber);
int uvrpc_subscriber_subscribe(uvrpc_subscriber_t* subscriber, const char* topic,
                                 uvrpc_subscribe_callback_t callback, void* ctx);
int uvrpc_subscriber_unsubscribe(uvrpc_subscriber_t* subscriber, const char* topic);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */