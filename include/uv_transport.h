/**
 * @file uv_transport.h
 * @brief Unified Transport Layer for UVRPC
 * 
 * Simple, zero-dependency transport layer abstraction.
 * Provides a unified interface for TCP, UDP, IPC, and INPROC transports.
 * 
 * Design Philosophy:
 * - Single responsibility: only handle byte stream send/recv
 * - Zero dependencies: no dependency on RPC layer
 * - Unified API: all transports use the same interface
 * - Easy to extend: adding new transport only requires 5 functions
 * - Event-driven: fully event-driven, never manage or block event loop
 * - Loop injection: user owns and manages the libuv loop, transport only uses it
 * - Zero loop ownership: transport never stores or manipulates loop->data
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 2.0
 */

#ifndef UV_TRANSPORT_H
#define UV_TRANSPORT_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport types
 */
typedef enum {
    UV_TRANSPORT_TCP = 0,     /**< TCP transport */
    UV_TRANSPORT_UDP = 1,     /**< UDP transport */
    UV_TRANSPORT_IPC = 2,     /**< Unix domain socket (IPC) */
    UV_TRANSPORT_INPROC = 3   /**< In-process transport */
} uv_transport_type_t;

/**
 * @brief Transport errors
 */
typedef enum {
    UV_TRANSPORT_OK = 0,             /**< Success */
    UV_TRANSPORT_ERROR = -1,         /**< General error */
    UV_TRANSPORT_ERROR_NOT_SUPPORTED = -2,  /**< Operation not supported */
    UV_TRANSPORT_ERROR_INVALID_PARAM = -3,  /**< Invalid parameter */
    UV_TRANSPORT_ERROR_NO_MEMORY = -4,      /**< Out of memory */
    UV_TRANSPORT_ERROR_IO = -5,            /**< I/O error */
    UV_TRANSPORT_ERROR_ALREADY_CONNECTED = -6,  /**< Already connected */
    UV_TRANSPORT_ERROR_NOT_CONNECTED = -7,   /**< Not connected */
    UV_TRANSPORT_ERROR_TIMEOUT = -8,        /**< Timeout */
    UV_TRANSPORT_ERROR_MAX
} uv_transport_error_t;

/**
 * @brief Forward declaration
 */
typedef struct uv_transport uv_transport_t;

/**
 * @brief Transport vtable (internal use)
 */
typedef struct {
    int (*listen)(uv_transport_t* transport);
    int (*connect)(uv_transport_t* transport);
    void (*stop)(uv_transport_t* transport);
    void (*disconnect)(uv_transport_t* transport);
    int (*send)(uv_transport_t* transport, const uint8_t* data, size_t size);
    int (*send_to)(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer);
    void (*free)(uv_transport_t* transport);
} uv_transport_vtable_t;

/**
 * @brief Transport base structure (internal use)
 */
struct uv_transport {
    uv_transport_type_t type;
    uv_loop_t* loop;
    const uv_transport_vtable_t* vtable;
    /* Transport-specific fields follow */
};

/**
 * @brief Transport configuration
 */
typedef struct {
    uv_loop_t* loop;             /**< Event loop (required) */
    const char* address;         /**< Address (e.g., "tcp://127.0.0.1:7003") */
    int is_server;               /**< Server mode: 1 = server, 0 = client */
    
    /* Callbacks */
    /**
     * @brief Receive callback - called when data is received
     * @param transport Transport handle
     * @param data Received data (will be freed after callback returns)
     * @param size Data size
     * @param peer Peer identifier (for connectionless transports)
     * @param ctx User context from config
     */
    void (*on_recv)(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer, void* ctx);
    
    /**
     * @brief Connect callback - called when connection state changes
     * @param transport Transport handle
     * @param status 0 = connected, < 0 = error
     * @param ctx User context from config
     */
    void (*on_connect)(uv_transport_t* transport, int status, void* ctx);
    
    /**
     * @brief Disconnect callback - called when connection is closed
     * @param transport Transport handle
     * @param ctx User context from config
     */
    void (*on_disconnect)(uv_transport_t* transport, void* ctx);
    
    /**
     * @brief Error callback - called when an error occurs
     * @param transport Transport handle
     * @param error Error code
     * @param msg Error message
     * @param ctx User context from config
     */
    void (*on_error)(uv_transport_t* transport, int error, const char* msg, void* ctx);
    
    void* callback_ctx;          /**< User context passed to callbacks */
} uv_transport_config_t;

/**
 * @brief Create transport
 * 
 * @param type Transport type
 * @param config Configuration (will be copied)
 * @return Transport handle, or NULL on error
 */
uv_transport_t* uv_transport_create(uv_transport_type_t type, const uv_transport_config_t* config);

/* Transport-specific create functions (optional, for convenience) */
uv_transport_t* uv_transport_tcp_create(const uv_transport_config_t* config);
uv_transport_t* uv_transport_udp_create(const uv_transport_config_t* config);
uv_transport_t* uv_transport_ipc_create(const uv_transport_config_t* config);
uv_transport_t* uv_transport_inproc_create(const uv_transport_config_t* config);

/**
 * @brief Destroy transport
 * 
 * @param transport Transport handle
 */
void uv_transport_destroy(uv_transport_t* transport);

/**
 * @brief Listen for connections (server mode)
 * 
 * @param transport Transport handle
 * @return 0 on success, error code on failure
 */
int uv_transport_listen(uv_transport_t* transport);

/**
 * @brief Stop listening (server mode)
 * 
 * @param transport Transport handle
 */
void uv_transport_stop(uv_transport_t* transport);

/**
 * @brief Connect to server (client mode)
 * 
 * @param transport Transport handle
 * @return 0 on success, error code on failure
 */
int uv_transport_connect(uv_transport_t* transport);

/**
 * @brief Disconnect from server (client mode)
 * 
 * @param transport Transport handle
 */
void uv_transport_disconnect(uv_transport_t* transport);

/**
 * @brief Send data
 * 
 * For connection-oriented transports (TCP, IPC), sends to connected peer.
 * For connectionless transports (UDP), sends to default peer.
 * 
 * @param transport Transport handle
 * @param data Data to send
 * @param size Data size
 * @return 0 on success, error code on failure
 */
int uv_transport_send(uv_transport_t* transport, const uint8_t* data, size_t size);

/**
 * @brief Send data to specific peer (connectionless transports)
 * 
 * @param transport Transport handle
 * @param data Data to send
 * @param size Data size
 * @param peer Peer identifier (from on_recv callback)
 * @return 0 on success, error code on failure
 */
int uv_transport_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer);

/**
 * @brief Check if transport is connected
 * 
 * @param transport Transport handle
 * @return 1 if connected, 0 otherwise
 */
int uv_transport_is_connected(uv_transport_t* transport);

/**
 * @brief Get transport type
 * 
 * @param transport Transport handle
 * @return Transport type
 */
uv_transport_type_t uv_transport_get_type(uv_transport_t* transport);

/**
 * @brief Get transport address
 * 
 * @param transport Transport handle
 * @return Address string (do not free)
 */
const char* uv_transport_get_address(uv_transport_t* transport);

#ifdef __cplusplus
}
#endif

#endif /* UV_TRANSPORT_H */