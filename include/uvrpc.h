/**
 * UVRPC - Ultra-Fast RPC Framework
 * Design: libuv + FlatCC + UVBus
 * Philosophy: Zero threads, Zero locks, Zero global variables
 *             All I/O managed by libuv event loop
 */

#ifndef UVRPC_H
#define UVRPC_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>
#include "uvbus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
    UVRPC_OK = 0,
    UVRPC_ERROR = -1,
    UVRPC_ERROR_INVALID_PARAM = -2,
    UVRPC_ERROR_NO_MEMORY = -3,
    UVRPC_ERROR_NOT_CONNECTED = -4,
    UVRPC_ERROR_TIMEOUT = -5,
    UVRPC_ERROR_TRANSPORT = -6,
    UVRPC_ERROR_CALLBACK_LIMIT = -7,
    UVRPC_ERROR_CANCELLED = -8,
    UVRPC_ERROR_POOL_EXHAUSTED = -9,
    UVRPC_ERROR_RATE_LIMITED = -10,
    UVRPC_ERROR_NOT_FOUND = -11,
    UVRPC_ERROR_ALREADY_EXISTS = -12,
    UVRPC_ERROR_INVALID_STATE = -13,
    UVRPC_ERROR_IO = -14,
    UVRPC_ERROR_MAX
} uvrpc_error_t;

/* RPC-specific error codes (for error frames) */
typedef enum {
    UVRPC_RPC_ERROR_OK = 0,
    UVRPC_RPC_ERROR_INVALID_REQUEST = 1,
    UVRPC_RPC_ERROR_METHOD_NOT_FOUND = 2,
    UVRPC_RPC_ERROR_INVALID_PARAMS = 3,
    UVRPC_RPC_ERROR_INTERNAL_ERROR = 4,
    UVRPC_RPC_ERROR_TIMEOUT = 5,
    UVRPC_RPC_ERROR_PARSE_ERROR = 6,
    UVRPC_RPC_ERROR_SERVER_ERROR = 7
} uvrpc_rpc_error_t;

/* Configuration constants */
#ifndef UVRPC_MAX_PENDING_CALLBACKS
/* Ring buffer size - must be a power of 2 for efficient modulo operation
 * Recommended values: 65536 (2^16), 262144 (2^18), 1048576 (2^20), 4194304 (2^22)
 * Default: 2^20 = 1,048,576 */
#define UVRPC_MAX_PENDING_CALLBACKS (1 << 20)  /* 1,048,576 - must be power of 2 */
#endif

#ifndef UVRPC_DEFAULT_POOL_SIZE
#define UVRPC_DEFAULT_POOL_SIZE 10  /* Default connection pool size */
#endif

#ifndef UVRPC_MAX_CONCURRENT_REQUESTS
#define UVRPC_MAX_CONCURRENT_REQUESTS 100  /* Max concurrent requests per client */
#endif

/* Transport types */
typedef enum {
    UVRPC_TRANSPORT_TCP = 0,    /* TCP network transport */
    UVRPC_TRANSPORT_UDP = 1,    /* UDP transport */
    UVRPC_TRANSPORT_IPC = 2,    /* Unix domain socket (IPC) */
    UVRPC_TRANSPORT_INPROC = 3  /* In-process transport */
} uvrpc_transport_type;

/* Performance modes */
typedef enum {
    UVRPC_PERF_LOW_LATENCY = 0,  /* Low latency: process immediately, minimal batching */
    UVRPC_PERF_HIGH_THROUGHPUT = 1  /* High throughput: batch processing, optimal for bulk operations */
} uvrpc_perf_mode_t;

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

typedef void (*uvrpc_error_callback_t)(uvrpc_error_t error_code, const char* error_msg, void* ctx);

/* Configuration */
struct uvrpc_config {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_type transport;
    uvrpc_comm_type_t comm_type;
    uvrpc_perf_mode_t performance_mode;
    int pool_size;              /* Connection pool size (default: UVRPC_DEFAULT_POOL_SIZE) */
    int max_concurrent;         /* Max concurrent requests (default: UVRPC_MAX_CONCURRENT_REQUESTS) */
    int max_pending_callbacks;  /* Max pending callbacks in ring buffer (default: UVRPC_MAX_PENDING_CALLBACKS) */
    uint64_t timeout_ms;        /* Default timeout in milliseconds (default: 0 = no timeout) */
    uint32_t msgid_offset;      /* Message ID offset for multi-instance isolation (default: 0 = auto) */
};

/* Request structure */
struct uvrpc_request {
    uvrpc_server_t* server;
    uint32_t msgid;
    char* method;
    uint8_t* params;
    size_t params_size;
    void* client_ctx;  /* Client context for sending response (from UVBus) */
    void* user_data;
};

/* Response structure */
struct uvrpc_response {
    int status;
    uint32_t msgid;
    int32_t error_code;
    char* error_message;
    uint8_t* result;
    size_t result_size;
    void* user_data;
};

/**
 * IMPORTANT: Request/Response Data Lifetime
 *
 * Server Request (uvrpc_request_t):
 * - req->method and req->params point to frame data and are ONLY VALID during the handler callback
 * - Do NOT store these pointers for later use - they will be freed after the callback returns
 * - If you need to use the data later, make a copy using uvrpc_strdup() for method or uvrpc_alloc() + memcpy() for params
 * - req->client_stream can be used to send responses during the callback
 *
 * Client Response (uvrpc_response_t):
 * - resp->result points to copied data that is valid until the callback returns
 * - The framework automatically frees resp->result after the callback
 * - Do NOT store resp->result for later use - make a copy if needed
 * - Use uvrpc_response_free() only if you allocated the response structure yourself (rare)
 */

/* Configuration API */
uvrpc_config_t* uvrpc_config_new(void);
void uvrpc_config_free(uvrpc_config_t* config);
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_type transport);
uvrpc_config_t* uvrpc_config_set_comm_type(uvrpc_config_t* config, uvrpc_comm_type_t comm_type);
uvrpc_config_t* uvrpc_config_set_performance_mode(uvrpc_config_t* config, uvrpc_perf_mode_t mode);
uvrpc_config_t* uvrpc_config_set_pool_size(uvrpc_config_t* config, int pool_size);
uvrpc_config_t* uvrpc_config_set_max_concurrent(uvrpc_config_t* config, int max_concurrent);
uvrpc_config_t* uvrpc_config_set_max_pending_callbacks(uvrpc_config_t* config, int max_pending);
uvrpc_config_t* uvrpc_config_set_timeout(uvrpc_config_t* config, uint64_t timeout_ms);
uvrpc_config_t* uvrpc_config_set_msgid_offset(uvrpc_config_t* config, uint32_t msgid_offset);

/* Server API */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
int uvrpc_server_start(uvrpc_server_t* server);
void uvrpc_server_stop(uvrpc_server_t* server);
void uvrpc_server_free(uvrpc_server_t* server);
int uvrpc_server_register(uvrpc_server_t* server, const char* method, uvrpc_handler_t handler, void* ctx);

/* Server statistics */
uint64_t uvrpc_server_get_total_requests(uvrpc_server_t* server);
uint64_t uvrpc_server_get_total_responses(uvrpc_server_t* server);

/* Server response API */
int uvrpc_response_send(uvrpc_request_t* req, const uint8_t* result, size_t result_size);
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code, const char* error_message);

/* Client API */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);
int uvrpc_client_connect(uvrpc_client_t* client);
int uvrpc_client_connect_with_callback(uvrpc_client_t* client,
                                         uvrpc_connect_callback_t callback, void* ctx);
void uvrpc_client_disconnect(uvrpc_client_t* client);
void uvrpc_client_free(uvrpc_client_t* client);
uv_loop_t* uvrpc_client_get_loop(uvrpc_client_t* client);

/* Retry configuration */
int uvrpc_client_set_max_retries(uvrpc_client_t* client, int max_retries);
int uvrpc_client_get_max_retries(uvrpc_client_t* client);

/* Call API with automatic retry */
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx);

/* Call API without retry (for fine-grained control) */
int uvrpc_client_call_no_retry(uvrpc_client_t* client, const char* method,
                                const uint8_t* params, size_t params_size,
                                uvrpc_callback_t callback, void* ctx);

/* Batch API - Send multiple requests efficiently */
int uvrpc_client_call_batch(uvrpc_client_t* client,
                             const char** methods,
                             const uint8_t** params_array,
                             size_t* params_sizes,
                             uvrpc_callback_t* callbacks,
                             void** contexts,
                             int count);

/* Concurrency control */
int uvrpc_client_set_max_concurrent(uvrpc_client_t* client, int max_concurrent);
int uvrpc_client_get_pending_count(uvrpc_client_t* client);

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