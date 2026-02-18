/**
 * @file uvbus.h
 * @brief UVBus - Universal Bus for libuv-based Communication
 * 
 * A unified abstraction layer for various transport protocols (TCP, UDP, IPC, INPROC)
 * Built on top of libuv event loop for non-blocking I/O.
 * 
 * Design Philosophy:
 * - Zero-copy where possible
 * - Single-threaded event loop
 * - Minimal overhead
 * - Protocol-agnostic API
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 */

#ifndef UVBUS_H
#define UVBUS_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UVBus error codes
 * 
 * Error codes returned by UVBus API functions.
 */
typedef enum {
    UVBUS_OK = 0,                      /**< @brief Operation successful */
    UVBUS_ERROR = -1,                  /**< @brief General error */
    UVBUS_ERROR_INVALID_PARAM = -2,    /**< @brief Invalid parameter */
    UVBUS_ERROR_NO_MEMORY = -3,        /**< @brief Memory allocation failed */
    UVBUS_ERROR_NOT_CONNECTED = -4,    /**< @brief Not connected */
    UVBUS_ERROR_TIMEOUT = -5,          /**< @brief Operation timed out */
    UVBUS_ERROR_IO = -6,               /**< @brief I/O error */
    UVBUS_ERROR_ALREADY_EXISTS = -7,   /**< @brief Resource already exists */
    UVBUS_ERROR_NOT_FOUND = -8,        /**< @brief Resource not found */
    UVBUS_ERROR_NOT_IMPLEMENTED = -9,  /**< @brief Feature not implemented */
    UVBUS_ERROR_MAX                    /**< @brief Maximum error code */
} uvbus_error_t;

/**
 * @brief UVBus transport types
 * 
 * Defines the available transport protocols.
 */
typedef enum {
    UVBUS_TRANSPORT_TCP = 0,    /**< @brief TCP transport */
    UVBUS_TRANSPORT_UDP = 1,    /**< @brief UDP transport */
    UVBUS_TRANSPORT_IPC = 2,    /**< @brief Unix domain socket (IPC) */
    UVBUS_TRANSPORT_INPROC = 3  /**< @brief In-process transport */
} uvbus_transport_type_t;

/**
 * @brief Receive callback type
 * 
 * Called when data is received.
 * 
 * @param data Received data
 * @param size Data size
 * @param client_ctx Client context (for server)
 * @param server_ctx Server context
 */
typedef void (*uvbus_recv_callback_t)(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx);

/**
 * @brief Connect callback type
 * 
 * Called when connection state changes.
 * 
 * @param status Connection status
 * @param ctx User context
 */
typedef void (*uvbus_connect_callback_t)(uvbus_error_t status, void* ctx);

/**
 * @brief Close callback type
 * 
 * Called when connection is closed.
 * 
 * @param ctx User context
 */
typedef void (*uvbus_close_callback_t)(void* ctx);

/**
 * @brief Error callback type
 * 
 * Called when an error occurs.
 * 
 * @param error_code Error code
 * @param error_msg Error message
 * @param ctx User context
 */
typedef void (*uvbus_error_callback_t)(uvbus_error_t error_code, const char* error_msg, void* ctx);

/* Forward declarations */
typedef struct uvbus uvbus_t;
typedef struct uvbus_config uvbus_config_t;
typedef struct uvbus_transport uvbus_transport_t;

/* Transport virtual function table */
typedef struct uvbus_transport_vtable {
    int (*listen)(void* impl, const char* address);
    int (*connect)(void* impl, const char* address);
    void (*disconnect)(void* impl);
    int (*send)(void* impl, const uint8_t* data, size_t size);
    int (*send_to)(void* impl, const uint8_t* data, size_t size, void* target);
    void (*free)(void* impl);
} uvbus_transport_vtable_t;

/* Configuration */
struct uvbus_config {
    uv_loop_t* loop;
    uvbus_transport_type_t transport;
    const char* address;
    
    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_connect_callback_t connect_cb;
    uvbus_close_callback_t close_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;
    
    /* Options */
    uint64_t timeout_ms;
    int enable_timeout;
};

/* Transport implementation base structure */
struct uvbus_transport {
    uvbus_transport_type_t type;
    uv_loop_t* loop;
    char* address;

    /* Parent bus reference */
    struct uvbus* parent_bus;

    /* Callbacks */
    uvbus_recv_callback_t recv_cb;
    uvbus_connect_callback_t connect_cb;
    uvbus_close_callback_t close_cb;
    uvbus_error_callback_t error_cb;
    void* callback_ctx;

    /* Flags */
    int is_server;
    int is_connected;

    /* Virtual function table */
    const uvbus_transport_vtable_t* vtable;

    /* Transport-specific implementation */
    union {
        void* tcp_server;
        void* tcp_client;
        void* udp_server;
        void* udp_client;
        void* ipc_server;
        void* ipc_client;
        void* inproc_server;
        void* inproc_client;
    } impl;
};

/* UVBus main structure */
struct uvbus {
    uvbus_config_t config;
    uvbus_transport_t* transport;
    int is_active;
};

/* Core API */

/**
 * Create UVBus configuration
 */
uvbus_config_t* uvbus_config_new(void);

/**
 * Free UVBus configuration
 */
void uvbus_config_free(uvbus_config_t* config);

/**
 * Set event loop
 */
void uvbus_config_set_loop(uvbus_config_t* config, uv_loop_t* loop);

/**
 * Set transport type
 */
void uvbus_config_set_transport(uvbus_config_t* config, uvbus_transport_type_t transport);

/**
 * Set address
 */
void uvbus_config_set_address(uvbus_config_t* config, const char* address);

/**
 * Set receive callback
 */
void uvbus_config_set_recv_callback(uvbus_config_t* config, uvbus_recv_callback_t recv_cb, void* ctx);

/**
 * Set connect callback
 */
void uvbus_config_set_connect_callback(uvbus_config_t* config, uvbus_connect_callback_t connect_cb, void* ctx);

/**
 * Set close callback
 */
void uvbus_config_set_close_callback(uvbus_config_t* config, uvbus_close_callback_t close_cb, void* ctx);

/**
 * Set error callback
 */
void uvbus_config_set_error_callback(uvbus_config_t* config, uvbus_error_callback_t error_cb, void* ctx);

/**
 * Set timeout
 */
void uvbus_config_set_timeout(uvbus_config_t* config, uint64_t timeout_ms);

/**
 * Enable/disable timeout
 */
void uvbus_config_set_timeout_enabled(uvbus_config_t* config, int enabled);

/* Server API */

/**
 * Create UVBus server
 */
uvbus_t* uvbus_server_new(uvbus_config_t* config);

/**
 * Start listening
 */
uvbus_error_t uvbus_listen(uvbus_t* bus);

/**
 * Stop listening
 */
void uvbus_stop(uvbus_t* bus);

/**
 * Send data to all connected clients (server only)
 */
uvbus_error_t uvbus_send(uvbus_t* bus, const uint8_t* data, size_t size);

/**
 * Send data to specific client (server only)
 */
uvbus_error_t uvbus_send_to(uvbus_t* bus, const uint8_t* data, size_t size, void* client);

/* Client API */

/**
 * Create UVBus client
 */
uvbus_t* uvbus_client_new(uvbus_config_t* config);

/**
 * Connect to server
 */
uvbus_error_t uvbus_connect(uvbus_t* bus);

/**
 * Connect to server with callback
 */
uvbus_error_t uvbus_connect_with_callback(uvbus_t* bus, uvbus_connect_callback_t callback, void* ctx);

/**
 * Disconnect from server
 */
void uvbus_disconnect(uvbus_t* bus);

/**
 * Send data to server (client only)
 */
uvbus_error_t uvbus_client_send(uvbus_t* bus, const uint8_t* data, size_t size);

/**
 * Send data to specific client (server only)
 * For request-response pattern, send response to the client that sent the request
 */
uvbus_error_t uvbus_send_to(uvbus_t* bus, const uint8_t* data, size_t size, void* client_ctx);

/* Common API */

/**
 * Get event loop
 */
uv_loop_t* uvbus_get_loop(uvbus_t* bus);

/**
 * Get transport type
 */
uvbus_transport_type_t uvbus_get_transport_type(uvbus_t* bus);

/**
 * Get address
 */
const char* uvbus_get_address(uvbus_t* bus);

/**
 * Check if connected
 */
int uvbus_is_connected(uvbus_t* bus);

/**
 * Check if server
 */
int uvbus_is_server(uvbus_t* bus);

/* Internal transport creation functions (not part of public API) */
uvbus_transport_t* create_tcp_transport(uvbus_transport_type_t type, uv_loop_t* loop);
uvbus_transport_t* create_udp_transport(uvbus_transport_type_t type, uv_loop_t* loop);
uvbus_transport_t* create_ipc_transport(uvbus_transport_type_t type, uv_loop_t* loop);
uvbus_transport_t* create_inproc_transport(uvbus_transport_type_t type, uv_loop_t* loop);

/**
 * Free UVBus
 */
void uvbus_free(uvbus_t* bus);

#ifdef __cplusplus
}
#endif

#endif /* UVBUS_H */