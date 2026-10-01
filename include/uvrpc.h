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

/* Debug logging macros - debug trace compiles out in release builds.
 *
 * NOTE: the macro name UVRPC_ERROR is intentionally NOT used here because it
 * collides with the UVRPC_ERROR enum value below. Use UVRPC_LOG_ERROR for
 * always-on error/warning logging, UVRPC_LOG_DEBUG (or the legacy UVRPC_LOG
 * alias) for trace output that compiles out unless -DUVRPC_DEBUG is set. */
#ifdef UVRPC_DEBUG
#define UVRPC_LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define UVRPC_LOG_DEBUG(fmt, ...) ((void)0)
#endif
#define UVRPC_LOG(fmt, ...) UVRPC_LOG_DEBUG(fmt, ##__VA_ARGS__)
#define UVRPC_LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

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
    UVRPC_ERROR_TRANSPORT_BUSY = -7,   /**< @brief Transport layer busy, retry later */
    UVRPC_ERROR_CALLBACK_LIMIT = -8,   /**< @brief Callback limit exceeded */
    UVRPC_ERROR_CANCELLED = -9,        /**< @brief Operation was cancelled */
    UVRPC_ERROR_POOL_EXHAUSTED = -10,   /**< @brief Connection pool exhausted */
    UVRPC_ERROR_RATE_LIMITED = -11,    /**< @brief Rate limit exceeded */
    UVRPC_ERROR_NOT_FOUND = -12,       /**< @brief Resource not found */
    UVRPC_ERROR_ALREADY_EXISTS = -13,  /**< @brief Resource already exists */
    UVRPC_ERROR_INVALID_STATE = -14,   /**< @brief Invalid state for operation */
    UVRPC_ERROR_IO = -15,              /**< @brief I/O error occurred */
    UVRPC_ERROR_MAX_CLIENTS = -16,     /**< @brief Maximum clients reached (server connection limit) */
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
#ifndef UVRPC_DEFAULT_PENDING_CALLBACKS
/* Default ring buffer size - must be a power of 2 for efficient modulo operation
 * Recommended values: 65536 (2^16), 262144 (2^18), 1048576 (2^20), 4194304 (2^22)
 * Default: 2^16 = 65,536 (balanced memory usage for typical workloads) */
#define UVRPC_DEFAULT_PENDING_CALLBACKS (1 << 16)  /* 65,536 - must be power of 2 */
#endif

#ifndef UVRPC_MAX_PENDING_CALLBACKS
/* Maximum ring buffer size - safety limit */
#define UVRPC_MAX_PENDING_CALLBACKS (1 << 22)  /* 4,194,304 - maximum allowed */
#endif

/* RpcFrame.type. Named because the value decides who owns a frame: a client
 * must not treat an error frame as a result, and it cannot tell by looking at
 * the payload -- an error carries a code and a message in exactly the bytes a
 * successful result would use. */
#define UVRPC_FRAME_TYPE_REQUEST       0  /**< @brief Client to server */
#define UVRPC_FRAME_TYPE_RESPONSE      1  /**< @brief Server to client, final */
#define UVRPC_FRAME_TYPE_RESPONSE_MORE 2  /**< @brief Server to client, more to come */
#define UVRPC_FRAME_TYPE_RESPONSE_ERROR 3 /**< @brief Server to client, the request failed; payload is int32 code then message bytes */

#ifndef UVRPC_MAX_CONCURRENT_REQUESTS
#define UVRPC_MAX_CONCURRENT_REQUESTS 100  /* Max concurrent requests per client */
#endif

#ifndef UVRPC_DEFAULT_TIMEOUT_MS
/* A request that has not been answered after this long is reported to the
 * application as UVRPC_ERROR_TIMEOUT. 30s is far past any healthy RPC round
 * trip, and it only matters for transports that can lose a datagram: on UDP a
 * lost response would otherwise hold its callback slot forever, because
 * nothing else ever releases it. Set 0 to restore the previous behaviour. */
#define UVRPC_DEFAULT_TIMEOUT_MS 30000
#endif

/**
 * @brief Cleanup callback for context data
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
    uvbus_transport_type_t transport;    /**< @brief Transport type (TCP/UDP/IPC/INPROC/SAMELOOP) */
    uvbus_loop_registry_t* registry;     /**< @brief Registry shared by INPROC/SAMELOOP peers (required for those two; see uvrpc_config_set_loop_registry) */
    int max_concurrent;                  /**< @brief Max concurrent requests (default: UVRPC_MAX_CONCURRENT_REQUESTS) */
    int timeout_ms;                      /**< @brief Deadline for a request whose response never arrives, in ms (default: UVRPC_DEFAULT_TIMEOUT_MS; 0 disables it) */
    int max_pending_callbacks;           /**< @brief Callback routing table slots (default: UVRPC_DEFAULT_PENDING_CALLBACKS = 1<<16; must be a power of 2 in [64, UVRPC_MAX_PENDING_CALLBACKS]) */
    uint32_t msgid_offset;               /**< @brief Message ID offset for multi-instance isolation (default: 0 = auto) */
    int max_clients;                     /**< @brief Maximum server clients (0 or less falls back to 1024; it does not mean unlimited) */
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
    int frame_type;           /**< @brief One of UVRPC_FRAME_TYPE_*, telling you whether `result` is a result */
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
 * @brief Set the registry the INPROC or SAMELOOP transport belongs to
 *
 * Those two transports have no shared state of their own: a server and a
 * client are built from independent configurations and meet through this
 * registry. Create one with uvbus_loop_registry_new(), pass the same pointer
 * to every configuration that has to see the others, and free it with
 * uvbus_loop_registry_free() once the servers and clients are gone.
 *
 * @code
 * uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
 * uvrpc_config_set_loop_registry(scfg, reg);
 * uvrpc_config_set_loop_registry(ccfg, reg);
 * @endcode
 *
 * TCP, UDP and IPC ignore this. Leaving it unset for INPROC or SAMELOOP is an
 * error, reported when the transport is created -- the framework keeps no
 * hidden per-loop or process-wide state to fall back on, so two unrelated
 * endpoints cannot collide by accident.
 *
 * @param config Configuration structure
 * @param registry Registry, or NULL to clear it
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_loop_registry(uvrpc_config_t* config, uvbus_loop_registry_t* registry);

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
 * @param transport Transport type (TCP/UDP/IPC/INPROC/SAMELOOP)
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvbus_transport_type_t transport);

/**
 * @brief Set the maximum concurrent requests
 *
 * Only the batch entry point checks this (uvrpc_client.c:714-718); single
 * uvrpc_client_call() requests are not gated.
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
 * @param max_pending Maximum pending callbacks (must be a power of 2 in
 *                    [64, UVRPC_MAX_PENDING_CALLBACKS]); anything else is
 *                    silently replaced by UVRPC_DEFAULT_PENDING_CALLBACKS
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_max_pending_callbacks(uvrpc_config_t* config, int max_pending);

/**
 * @brief Set the request deadline
 *
 * A pending callback slot is only released when its response arrives. On a
 * transport that can lose a datagram, a lost response would therefore hold
 * its slot until the client is disconnected, and the failure would surface
 * much later as UVRPC_ERROR_RATE_LIMITED with nothing pointing at the cause.
 * With a deadline set, an unanswered request is instead reported to the
 * application as UVRPC_ERROR_TIMEOUT and its slot is returned, no later than
 * the next request issued on that client (see uvrpc_client_set_timeout).
 *
 * @param config Configuration structure
 * @param timeout_ms Deadline in milliseconds; 0 disables it
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_timeout(uvrpc_config_t* config, int timeout_ms);

/**
 * @brief Set the message ID offset
 * 
 * @param config Configuration structure
 * @param msgid_offset Message ID offset for multi-instance isolation
 * @return Configuration structure for chaining
 */
uvrpc_config_t* uvrpc_config_set_msgid_offset(uvrpc_config_t* config, uint32_t msgid_offset);

/**
 * @brief Set maximum server clients
 * 
 * @param config Configuration object
 * @param max_clients Maximum clients; 0 or less selects the 1024 default and
 *                    does not mean unlimited
 * @return Configuration object for chaining
 * @note Only applicable to server configuration
 */
uvrpc_config_t* uvrpc_config_set_max_clients(uvrpc_config_t* config, int max_clients);

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
 * @brief Get current connected clients count
 * 
 * @param server Server instance
 * @return Current number of connected clients
 */
int uvrpc_server_get_client_count(uvrpc_server_t* server);

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
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code, const char*error_message);

/**
 * Send streaming chunk response
 * 
 * Note: Streaming is now implemented using multiple Response frames.
 * The caller should use uvrpc_response_send() for each chunk.
 * The is_last flag is handled by the application protocol in the data payload.
 * 
 * @param req Request object
 * @param chunk Chunk data to send
 * @param chunk_size Size of chunk data
 * @param is_last Set to 1 if this is the last chunk, 0 otherwise (ignored, kept for compatibility)
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_response_send_stream(uvrpc_request_t* req, const uint8_t* chunk, 
                                size_t chunk_size, int is_last);

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
 * @brief Call RPC method with oneway mode (no response expected)
 * 
 * Zero-overhead: no callback registration, no response handling.
 * Useful for fire-and-forget operations like logging, notifications, etc.
 * 
 * @param client Client instance
 * @param method Method name
 * @param params Request parameters
 * @param params_size Size of parameters
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_call_oneway(uvrpc_client_t* client, const char* method,
                              const uint8_t* params, size_t params_size);

/**
 * @brief Set maximum concurrent requests
 * 
 * @param client Client instance
 * @param max_concurrent Maximum concurrent requests
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_set_max_concurrent(uvrpc_client_t* client, int max_concurrent);

/**
 * @brief Set the request deadline for this client
 *
 * Applies to requests issued from now on; a request already in flight keeps
 * the deadline it was given.
 *
 * An expired request is reported with status UVRPC_ERROR_TIMEOUT and its slot
 * is returned -- no later than the next request issued on this client. There is
 * no per-client timer, so an idle client is not interrupted; the sweep runs
 * where a slot is about to be taken, which is also where the concurrency quota
 * is checked. That is what keeps a lost response from consuming slots until the
 * client can no longer issue a request at all.
 *
 * @param client Client instance
 * @param timeout_ms Deadline in milliseconds; 0 disables it
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_client_set_timeout(uvrpc_client_t* client, int timeout_ms);

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
 * @brief Send response (more to come)
 * 
 * Sends a response with type=2 (ResponseMore) to indicate more responses
 * will follow. The client keeps the pending callback alive.
 * 
 * @param req Request object
 * @param result Response data
 * @param result_size Response data size
 */
void uvrpc_request_send_response_more(uvrpc_request_t* req, const uint8_t* result, size_t result_size);

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
 * @brief Convert error code to human-readable error message
 * 
 * This function converts UVRPC error codes to descriptive strings.
 * It does NOT print or output the error message. The caller is responsible
 * for any output (fprintf, logging, etc.).
 * 
 * @param error_code UVRPC error code
 * @return Static string describing the error
 * 
 * Example:
 * @code
 * int ret = uvrpc_client_call(client, "add", params, sizeof(params), callback, ctx);
 * if (ret != UVRPC_OK) {
 *     fprintf(stderr, "Error: %s\n", uvrpc_strerror(ret));
 * }
 * @endcode
 */
const char* uvrpc_strerror(int error_code);

/**
 * @defgroup StreamAPI Stream Support
 * @brief Functions for stream mode (multiple responses per request)
 * @{
 */

/**
 * @brief Check if response type is Response (type=1, last)
 * 
 * Returns 1 if the response frame type is 1 (Response, last), indicating
 * the stream is complete. This is used by the client to detect when a
 * stream ends and cleanup resources.
 * 
 * @param resp Response object
 * @return 1 if Response (last) type, 0 otherwise
 */
int uvrpc_response_is_stream_end(uvrpc_response_t* resp);

/**
 * @brief Check if response type is ResponseMore (type=2)
 * 
 * Returns 1 if the response frame type is 2 (ResponseMore), indicating
 * more responses will follow. This is used by the client to detect when a
 * stream is still active.
 * 
 * @param resp Response object
 * @return 1 if ResponseMore type, 0 otherwise
 */
int uvrpc_response_is_stream_more(uvrpc_response_t* resp);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */