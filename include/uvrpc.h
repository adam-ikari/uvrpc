/**
 * @file uvrpc.h
 * @brief UVRPC - Ultra-Fast RPC Framework
 * 
 * Design: libuv + FlatCC + UVBus
 * Philosophy: Zero threads, Zero locks, Zero global variables
 *             All I/O managed by libuv event loop
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 */

#ifndef UVRPC_H
#define UVRPC_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>
#include "uvbus.h"

/* Debug logging macro - compiles out in release builds */
#ifdef UVRPC_DEBUG
#define UVRPC_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define UVRPC_LOG(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Error codes for UVRPC operations
 * 
 * These error codes are returned by UVRPC API functions to indicate
 * success or failure conditions.
 */
typedef enum {
    UVRPC_OK = 0,                      /**< @brief Operation successful */
    UVRPC_ERROR = -1,                  /**< @brief General error */
    UVRPC_ERROR_INVALID_PARAM = -2,    /**< @brief Invalid parameter provided */
    UVRPC_ERROR_NO_MEMORY = -3,        /**< @brief Memory allocation failed */
    UVRPC_ERROR_NOT_CONNECTED = -4,    /**< @brief Not connected to server */
    UVRPC_ERROR_TIMEOUT = -5,          /**< @brief Operation timed out */
    UVRPC_ERROR_TRANSPORT = -6,        /**< @brief Transport layer error */
    UVRPC_ERROR_CALLBACK_LIMIT = -7,   /**< @brief Callback limit exceeded */
    UVRPC_ERROR_CANCELLED = -8,        /**< @brief Operation was cancelled */
    UVRPC_ERROR_POOL_EXHAUSTED = -9,   /**< @brief Connection pool exhausted */
    UVRPC_ERROR_RATE_LIMITED = -10,    /**< @brief Rate limit exceeded */
    UVRPC_ERROR_NOT_FOUND = -11,       /**< @brief Resource not found */
    UVRPC_ERROR_ALREADY_EXISTS = -12,  /**< @brief Resource already exists */
    UVRPC_ERROR_INVALID_STATE = -13,   /**< @brief Invalid state for operation */
    UVRPC_ERROR_IO = -14,              /**< @brief I/O error occurred */
    UVRPC_ERROR_MAX                    /**< @brief Maximum error code (for validation) */
} uvrpc_error_t;

/**
 * @brief RPC-specific error codes for error frames
 * 
 * These error codes are used in RPC error responses to indicate
 * the specific nature of the failure.
 */
typedef enum {
    UVRPC_RPC_ERROR_OK = 0,                /**< @brief No error */
    UVRPC_RPC_ERROR_INVALID_REQUEST = 1,   /**< @brief Invalid request format */
    UVRPC_RPC_ERROR_METHOD_NOT_FOUND = 2,  /**< @brief Requested method does not exist */
    UVRPC_RPC_ERROR_INVALID_PARAMS = 3,    /**< @brief Invalid parameters provided */
    UVRPC_RPC_ERROR_INTERNAL_ERROR = 4,    /**< @brief Internal server error */
    UVRPC_RPC_ERROR_TIMEOUT = 5,           /**< @brief Request timed out */
    UVRPC_RPC_ERROR_PARSE_ERROR = 6,       /**< @brief Failed to parse request */
    UVRPC_RPC_ERROR_SERVER_ERROR = 7       /**< @brief Server-side error */
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

/**
 * @brief Transport types for UVRPC communication
 * 
 * Defines the underlying transport protocol used for communication.
 */
typedef enum {
    UVRPC_TRANSPORT_TCP = 0,    /**< @brief TCP network transport (reliable, connection-oriented) */
    UVRPC_TRANSPORT_UDP = 1,    /**< @brief UDP transport (fast, connectionless) */
    UVRPC_TRANSPORT_IPC = 2,    /**< @brief Unix domain socket (IPC) for local communication */
    UVRPC_TRANSPORT_INPROC = 3  /**< @brief In-process transport for same-process communication */
} uvrpc_transport_type;

/**
 * @brief Performance modes for UVRPC operations
 * 
 * Controls how requests are processed to optimize for either
 * low latency or high throughput.
 */
typedef enum {
    UVRPC_PERF_LOW_LATENCY = 0,  /**< @brief Low latency mode: process immediately with minimal batching */
    UVRPC_PERF_HIGH_THROUGHPUT = 1 /**< @brief High throughput mode: batch processing for bulk operations */
} uvrpc_perf_mode_t;

/**
 * @brief Communication types for UVRPC
 * 
 * Defines the communication pattern: request-response or publish-subscribe.
 */
typedef enum {
    UVRPC_COMM_SERVER_CLIENT = 0,  /**< @brief Server/Client mode (request-response) */
    UVRPC_COMM_BROADCAST = 1        /**< @brief Broadcast mode (publish-subscribe) */
} uvrpc_comm_type_t;

/**
 * @brief Context cleanup callback type
 * 
 * @param data User data to be cleaned up
 * @param user_data Additional user data passed to cleanup
 */
typedef void (*uvrpc_context_cleanup_t)(void* data, void* user_data);

/**
 * @brief Universal context structure for server and client
 * 
 * Provides a way to attach user-defined data to server or client instances
 * with automatic cleanup support.
 */
typedef struct uvrpc_context {
    void* data;                           /**< @brief User-defined data */
    uvrpc_context_cleanup_t cleanup;      /**< @brief Optional cleanup callback */
    void* cleanup_data;                   /**< @brief Data passed to cleanup callback */
    uint32_t flags;                       /**< @brief Context flags (reserved for future use) */
    uint32_t reserved;                    /**< @brief Reserved for future use */
} uvrpc_context_t;

/**
 * @defgroup ContextAPI Context API
 * @brief Functions for managing user contexts
 * @{
 */

/**
 * @brief Create a new context with user data
 * 
 * @param data User data to attach
 * @return New context instance, or NULL on failure
 */
uvrpc_context_t* uvrpc_context_new(void* data);

/**
 * @brief Create a new context with cleanup callback
 * 
 * @param data User data to attach
 * @param cleanup Cleanup callback function
 * @param cleanup_data Data passed to cleanup callback
 * @return New context instance, or NULL on failure
 */
uvrpc_context_t* uvrpc_context_new_with_cleanup(void* data, uvrpc_context_cleanup_t cleanup, void* cleanup_data);

/**
 * @brief Free a context
 * 
 * If a cleanup callback was registered, it will be called.
 * 
 * @param ctx Context to free
 */
void uvrpc_context_free(uvrpc_context_t* ctx);

/**
 * @brief Get user data from context
 * 
 * @param ctx Context instance
 * @return User data pointer
 */
void* uvrpc_context_get_data(uvrpc_context_t* ctx);

/** @} */

/* Forward declarations */
typedef struct uvrpc_config uvrpc_config_t;
typedef struct uvrpc_server uvrpc_server_t;
typedef struct uvrpc_client uvrpc_client_t;
typedef struct uvrpc_publisher uvrpc_publisher_t;
typedef struct uvrpc_subscriber uvrpc_subscriber_t;
typedef struct uvrpc_request uvrpc_request_t;
typedef struct uvrpc_response uvrpc_response_t;

/**
 * @brief Request handler callback type (server-side)
 * 
 * @param req Incoming request object
 * @param ctx User context provided during registration
 */
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);

/**
 * @brief Response callback type (client-side)
 * 
 * @param resp Response object containing result or error
 * @param ctx User context provided during call
 */
typedef void (*uvrpc_callback_t)(uvrpc_response_t* resp, void* ctx);

/**
 * @brief Connection state callback type
 * 
 * @param status Connection status (0 for success, negative for error)
 * @param ctx User context provided during connection
 */
typedef void (*uvrpc_connect_callback_t)(int status, void* ctx);

/**
 * @brief Publish callback type (broadcast mode)
 * 
 * @param status Publish status (0 for success, negative for error)
 * @param ctx User context provided during publish
 */
typedef void (*uvrpc_publish_callback_t)(int status, void* ctx);

/**
 * @brief Subscribe callback type (broadcast mode)
 * 
 * @param topic Topic name
 * @param data Published data
 * @param size Data size
 * @param ctx User context provided during subscription
 */
typedef void (*uvrpc_subscribe_callback_t)(const char* topic, const uint8_t* data, size_t size, void* ctx);

/**
 * @brief Error callback type
 * 
 * @param error_code Error code from uvrpc_error_t
 * @param error_msg Error message string
 * @param ctx User context
 */
typedef void (*uvrpc_error_callback_t)(uvrpc_error_t error_code, const char* error_msg, void* ctx);

/**
 * @brief UVRPC configuration structure
 * 
 * Contains all configuration parameters for creating UVRPC servers and clients.
 * All fields have sensible defaults; use configuration functions to set values.
 */
struct uvrpc_config {
    uv_loop_t* loop;                     /**< @brief libuv event loop (required) */
    char* address;                       /**< @brief Server or client address (required) */
    uvrpc_transport_type transport;      /**< @brief Transport type (TCP/UDP/IPC/INPROC) */
    uvrpc_comm_type_t comm_type;         /**< @brief Communication type (SERVER_CLIENT or BROADCAST) */
    uvrpc_perf_mode_t performance_mode;  /**< @brief Performance mode (LOW_LATENCY or HIGH_THROUGHPUT) */
    int pool_size;                       /**< @brief Connection pool size (default: UVRPC_DEFAULT_POOL_SIZE) */
    int max_concurrent;                  /**< @brief Max concurrent requests (default: UVRPC_MAX_CONCURRENT_REQUESTS) */
    int max_pending_callbacks;           /**< @brief Max pending callbacks in ring buffer (default: UVRPC_MAX_PENDING_CALLBACKS) */
    uint64_t timeout_ms;                 /**< @brief Default timeout in milliseconds (default: 0 = no timeout) */
    uint32_t msgid_offset;               /**< @brief Message ID offset for multi-instance isolation (default: 0 = auto) */
};

/**
 * @brief RPC request structure (server-side)
 * 
 * Represents an incoming RPC request to be handled by the server.
 * @warning The method and params pointers are only valid during the handler callback.
 */
struct uvrpc_request {
    uvrpc_server_t* server;   /**< @brief Server instance */
    uint32_t msgid;           /**< @brief Message ID for request-response matching */
    char* method;             /**< @brief Method name (valid only during handler callback) */
    uint8_t* params;          /**< @brief Request parameters (valid only during handler callback) */
    size_t params_size;       /**< @brief Size of parameters buffer */
    void* client_ctx;         /**< @brief Client context for sending response (from UVBus) */
    void* user_data;          /**< @brief User-defined data */
};

/**
 * @brief RPC response structure (client-side)
 * 
 * Represents a response received from the server.
 * @warning The result pointer is only valid during the callback.
 */
struct uvrpc_response {
    int status;               /**< @brief Response status (0 for success) */
    uint32_t msgid;           /**< @brief Message ID matching the request */
    int32_t error_code;       /**< @brief RPC error code (0 for success) */
    char* error_message;      /**< @brief Error message (if error_code != 0) */
    uint8_t* result;          /**< @brief Response result data (valid only during callback) */
    size_t result_size;       /**< @brief Size of result buffer */
    void* user_data;          /**< @brief User-defined data */
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

/**
 * @defgroup ConfigAPI Configuration API
 * @brief Functions for creating and configuring UVRPC instances
 * @{
 */

/**
 * @brief Create a new configuration structure
 * 
 * Creates a configuration structure with default values.
 * 
 * @return New configuration instance, or NULL on failure
 */
uvrpc_config_t* uvrpc_config_new(void);

/**
 * @brief Free a configuration structure
 * 
 * @param config Configuration to free
 */
void uvrpc_config_free(uvrpc_config_t* config);

/**
 * @brief Set the event loop
 * 
 * @param config Configuration structure
 * @param loop libuv event loop
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);

/**
 * @brief Set the address
 * 
 * @param config Configuration structure
 * @param address Server or client address (e.g., "tcp://127.0.0.1:5555")
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);

/**
 * @brief Set the transport type
 * 
 * @param config Configuration structure
 * @param transport Transport type (TCP/UDP/IPC/INPROC)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_type transport);

/**
 * @brief Set the communication type
 * 
 * @param config Configuration structure
 * @param comm_type Communication type (SERVER_CLIENT or BROADCAST)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_comm_type(uvrpc_config_t* config, uvrpc_comm_type_t comm_type);

/**
 * @brief Set the performance mode
 * 
 * @param config Configuration structure
 * @param mode Performance mode (LOW_LATENCY or HIGH_THROUGHPUT)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_performance_mode(uvrpc_config_t* config, uvrpc_perf_mode_t mode);

/**
 * @brief Set the connection pool size
 * 
 * @param config Configuration structure
 * @param pool_size Number of connections in pool
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_pool_size(uvrpc_config_t* config, int pool_size);

/**
 * @brief Set the maximum concurrent requests
 * 
 * @param config Configuration structure
 * @param max_concurrent Maximum concurrent requests
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_max_concurrent(uvrpc_config_t* config, int max_concurrent);

/**
 * @brief Set the maximum pending callbacks
 * 
 * @param config Configuration structure
 * @param max_pending Maximum pending callbacks (must be power of 2)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_max_pending_callbacks(uvrpc_config_t* config, int max_pending);

/**
 * @brief Set the default timeout
 * 
 * @param config Configuration structure
 * @param timeout_ms Timeout in milliseconds (0 for no timeout)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_timeout(uvrpc_config_t* config, uint64_t timeout_ms);

/**
 * @brief Set the message ID offset
 * 
 * @param config Configuration structure
 * @param msgid_offset Message ID offset for multi-instance isolation
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_msgid_offset(uvrpc_config_t* config, uint32_t msgid_offset);

/** @} */

/**
 * @defgroup ServerAPI Server API
 * @brief Functions for creating and managing UVRPC servers
 * @{
 */

/**
 * @brief Create a new RPC server
 * 
 * @param config Configuration structure
 * @return New server instance, or NULL on failure
 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);

/**
 * @brief Start the server
 * 
 * @param server Server instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_server_start(uvrpc_server_t* server);

/**
 * @brief Stop the server
 * 
 * Stops accepting new connections but allows existing connections to complete.
 * 
 * @param server Server instance
 */
void uvrpc_server_stop(uvrpc_server_t* server);

/**
 * @brief Free the server
 * 
 * @param server Server instance
 */
void uvrpc_server_free(uvrpc_server_t* server);

/**
 * @brief Register a method handler
 * 
 * @param server Server instance
 * @param method Method name
 * @param handler Handler function
 * @param ctx User context passed to handler
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_server_register(uvrpc_server_t* server, const char* method, uvrpc_handler_t handler, void* ctx);

/**
 * @brief Set server context
 * 
 * @param server Server instance
 * @param ctx Context to attach
 */
void uvrpc_server_set_context(uvrpc_server_t* server, uvrpc_context_t* ctx);

/**
 * @brief Get server context
 * 
 * @param server Server instance
 * @return Context instance
 */
uvrpc_context_t* uvrpc_server_get_context(uvrpc_server_t* server);

/**
 * @brief Get total requests received
 * 
 * @param server Server instance
 * @return Total number of requests received
 */
uint64_t uvrpc_server_get_total_requests(uvrpc_server_t* server);

/**
 * @brief Get total responses sent
 * 
 * @param server Server instance
 * @return Total number of responses sent
 */
uint64_t uvrpc_server_get_total_responses(uvrpc_server_t* server);

/**
 * @brief Send a response to a request
 * 
 * @param req Request object
 * @param result Response data
 * @param result_size Size of response data
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_response_send(uvrpc_request_t* req, const uint8_t* result, size_t result_size);

/**
 * @brief Send an error response to a request
 * 
 * @param req Request object
 * @param error_code RPC error code
 * @param error_message Error message
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code, const char* error_message);

/** @} */

/**
 * @defgroup ClientAPI Client API
 * @brief Functions for creating and managing UVRPC clients
 * @{
 */

/**
 * @brief Create a new RPC client
 * 
 * @param config Configuration structure
 * @return New client instance, or NULL on failure
 */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);

/**
 * @brief Connect to server synchronously
 * 
 * @param client Client instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_connect(uvrpc_client_t* client);

/**
 * @brief Connect to server asynchronously
 * 
 * @param client Client instance
 * @param callback Connection callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_connect_with_callback(uvrpc_client_t* client,
                                         uvrpc_connect_callback_t callback, void* ctx);

/**
 * @brief Disconnect from server
 * 
 * @param client Client instance
 */
void uvrpc_client_disconnect(uvrpc_client_t* client);

/**
 * @brief Free the client
 * 
 * @param client Client instance
 */
void uvrpc_client_free(uvrpc_client_t* client);

/**
 * @brief Get the event loop
 * 
 * @param client Client instance
 * @return libuv event loop
 */
uv_loop_t* uvrpc_client_get_loop(uvrpc_client_t* client);

/**
 * @brief Set client context
 * 
 * @param client Client instance
 * @param ctx Context to attach
 */
void uvrpc_client_set_context(uvrpc_client_t* client, uvrpc_context_t* ctx);

/**
 * @brief Get client context
 * 
 * @param client Client instance
 * @return Context instance
 */
uvrpc_context_t* uvrpc_client_get_context(uvrpc_client_t* client);

/**
 * @brief Set maximum retry count
 * 
 * @param client Client instance
 * @param max_retries Maximum number of retries
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_set_max_retries(uvrpc_client_t* client, int max_retries);

/**
 * @brief Get maximum retry count
 * 
 * @param client Client instance
 * @return Maximum number of retries
 */
int uvrpc_client_get_max_retries(uvrpc_client_t* client);

/**
 * @brief Call RPC method with automatic retry
 * 
 * @param client Client instance
 * @param method Method name
 * @param params Request parameters
 * @param params_size Size of parameters
 * @param callback Response callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx);

/**
 * @brief Call RPC method without retry
 * 
 * @param client Client instance
 * @param method Method name
 * @param params Request parameters
 * @param params_size Size of parameters
 * @param callback Response callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_call_no_retry(uvrpc_client_t* client, const char* method,
                                const uint8_t* params, size_t params_size,
                                uvrpc_callback_t callback, void* ctx);

/**
 * @brief Call multiple RPC methods in batch
 * 
 * @param client Client instance
 * @param methods Array of method names
 * @param params_array Array of parameter arrays
 * @param params_sizes Array of parameter sizes
 * @param callbacks Array of response callbacks
 * @param contexts Array of user contexts
 * @param count Number of calls
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_call_batch(uvrpc_client_t* client,
                             const char** methods,
                             const uint8_t** params_array,
                             size_t* params_sizes,
                             uvrpc_callback_t* callbacks,
                             void** contexts,
                             int count);

/**
 * @brief Set maximum concurrent requests
 * 
 * @param client Client instance
 * @param max_concurrent Maximum concurrent requests
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_set_max_concurrent(uvrpc_client_t* client, int max_concurrent);

/**
 * @brief Get pending request count
 * 
 * @param client Client instance
 * @return Number of pending requests
 */
int uvrpc_client_get_pending_count(uvrpc_client_t* client);

/** @} */

/**
 * @defgroup RequestResponseAPI Request/Response API
 * @brief Functions for handling requests and responses
 * @{
 */

/**
 * @brief Send a response to a request (deprecated, use uvrpc_response_send)
 * 
 * @param req Request object
 * @param status Response status
 * @param result Response data
 * @param result_size Size of response data
 */
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size);

/**
 * @brief Free a request object
 * 
 * @param req Request object
 */
void uvrpc_request_free(uvrpc_request_t* req);

/**
 * @brief Free a response object
 * 
 * @param resp Response object
 */
void uvrpc_response_free(uvrpc_response_t* resp);

/** @} */

/**
 * @defgroup BroadcastAPI Broadcast API
 * @brief Functions for publish-subscribe messaging
 * @{
 */

/**
 * @brief Create a new publisher
 * 
 * @param config Configuration structure
 * @return New publisher instance, or NULL on failure
 */
uvrpc_publisher_t* uvrpc_publisher_create(uvrpc_config_t* config);

/**
 * @brief Start the publisher
 * 
 * @param publisher Publisher instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_publisher_start(uvrpc_publisher_t* publisher);

/**
 * @brief Stop the publisher
 * 
 * @param publisher Publisher instance
 */
void uvrpc_publisher_stop(uvrpc_publisher_t* publisher);

/**
 * @brief Free the publisher
 * 
 * @param publisher Publisher instance
 */
void uvrpc_publisher_free(uvrpc_publisher_t* publisher);

/**
 * @brief Publish a message to a topic
 * 
 * @param publisher Publisher instance
 * @param topic Topic name
 * @param data Message data
 * @param size Data size
 * @param callback Publish callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_publisher_publish(uvrpc_publisher_t* publisher, const char* topic,
                             const uint8_t* data, size_t size,
                             uvrpc_publish_callback_t callback, void* ctx);

/**
 * @brief Create a new subscriber
 * 
 * @param config Configuration structure
 * @return New subscriber instance, or NULL on failure
 */
uvrpc_subscriber_t* uvrpc_subscriber_create(uvrpc_config_t* config);

/**
 * @brief Connect subscriber to publisher
 * 
 * @param subscriber Subscriber instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_subscriber_connect(uvrpc_subscriber_t* subscriber);

/**
 * @brief Disconnect subscriber
 * 
 * @param subscriber Subscriber instance
 */
void uvrpc_subscriber_disconnect(uvrpc_subscriber_t* subscriber);

/**
 * @brief Free the subscriber
 * 
 * @param subscriber Subscriber instance
 */
void uvrpc_subscriber_free(uvrpc_subscriber_t* subscriber);

/**
 * @brief Subscribe to a topic
 * 
 * @param subscriber Subscriber instance
 * @param topic Topic name
 * @param callback Subscription callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_subscriber_subscribe(uvrpc_subscriber_t* subscriber, const char* topic,
                                 uvrpc_subscribe_callback_t callback, void* ctx);

/**
 * @brief Unsubscribe from a topic
 * 
 * @param subscriber Subscriber instance
 * @param topic Topic name
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_subscriber_unsubscribe(uvrpc_subscriber_t* subscriber, const char* topic);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */