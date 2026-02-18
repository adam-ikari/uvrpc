# Doxygen Comment Examples for UVRPC

This file provides examples of how to add Doxygen-formatted English comments to UVRPC code.

## Example 1: File Header

```c
/**
 * @file uvrpc_server.c
 * @brief Implementation of UVRPC server functionality
 * 
 * This file implements the server-side RPC functionality, including:
 * - Request handling and routing
 * - Response sending
 * - Connection management
 * - Statistics tracking
 * 
 * Design Philosophy:
 * - Zero threads: All I/O managed by libuv event loop
 * - Zero locks: No global variables or shared state
 * - Zero global variables: Complete isolation
 * 
 * @author UVRPC Team
 * @date 2026-02-18
 * @version 0.1.0
 */
```

## Example 2: Function Documentation

```c
/**
 * @brief Creates a new UVRPC server instance
 * 
 * This function creates a new server instance with the specified configuration.
 * The server will listen for incoming connections and handle RPC requests.
 * 
 * @param config Configuration structure containing server settings.
 *               Must include loop, address, and communication type.
 * 
 * @return Pointer to the created server instance, or NULL on failure.
 * 
 * @pre config must not be NULL
 * @pre config->loop must be a valid libuv loop
 * @pre config->address must be a valid address string
 * 
 * @post The returned server pointer is ready to be started
 * 
 * @note The caller is responsible for freeing the server using uvrpc_server_free()
 * 
 * @see uvrpc_server_start()
 * @see uvrpc_server_free()
 * 
 * Example:
 * @code
 * uvrpc_config_t* config = uvrpc_config_new();
 * uvrpc_config_set_loop(config, &loop);
 * uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
 * uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
 * 
 * uvrpc_server_t* server = uvrpc_server_create(config);
 * if (!server) {
 *     fprintf(stderr, "Failed to create server\n");
 *     return 1;
 * }
 * @endcode
 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
```

## Example 3: Structure Documentation

```c
/**
 * @brief UVRPC server instance structure
 * 
 * This structure represents a running UVRPC server instance.
 * It manages connections, handlers, and request routing.
 * 
 * The server maintains:
 * - A hash table of registered handlers (method name -> handler function)
 * - A ring buffer for pending requests
 * - Statistics for monitoring
 * - User-defined context
 */
typedef struct uvrpc_server {
    uv_loop_t* loop;                   /**< @brief libuv event loop */
    char* address;                      /**< @brief Server address */
    uvbus_t* uvbus;                    /**< @brief UVBus transport layer */
    handler_entry_t* handlers;          /**< @brief Registered handlers hash table */
    int is_running;                     /**< @brief Server running flag */
    
    uvrpc_context_t* ctx;               /**< @brief User-defined context */
    
    pending_request_t** pending_requests;  /**< @brief Ring buffer for pending requests */
    int max_pending_requests;           /**< @brief Maximum pending requests */
    uint32_t generation;                /**< @brief Generation counter */
    
    uint64_t total_requests;            /**< @brief Total requests received */
    uint64_t total_responses;           /**< @brief Total responses sent */
} uvrpc_server_t;
```

## Example 4: Type Definition

```c
/**
 * @brief Error codes for UVRPC operations
 * 
 * These error codes are returned by UVRPC functions to indicate
 * success or failure. All functions return UVRPC_OK on success.
 */
typedef enum {
    UVRPC_OK = 0,                        /**< @brief Operation successful */
    UVRPC_ERROR = -1,                     /**< @brief General error */
    UVRPC_ERROR_INVALID_PARAM = -2,      /**< @brief Invalid parameter provided */
    UVRPC_ERROR_NO_MEMORY = -3,          /**< @brief Memory allocation failed */
    UVRPC_ERROR_NOT_CONNECTED = -4,      /**< @brief Not connected to server */
    UVRPC_ERROR_TIMEOUT = -5,            /**< @brief Operation timed out */
    UVRPC_ERROR_TRANSPORT = -6,          /**< @brief Transport layer error */
    UVRPC_ERROR_CALLBACK_LIMIT = -7,     /**< @brief Callback limit exceeded */
    UVRPC_ERROR_CANCELLED = -8,          /**< @brief Operation was cancelled */
    UVRPC_ERROR_POOL_EXHAUSTED = -9,     /**< @brief Connection pool exhausted */
    UVRPC_ERROR_RATE_LIMITED = -10,      /**< @brief Rate limit exceeded */
    UVRPC_ERROR_NOT_FOUND = -11,         /**< @brief Resource not found */
    UVRPC_ERROR_ALREADY_EXISTS = -12,     /**< @brief Resource already exists */
    UVRPC_ERROR_INVALID_STATE = -13,     /**< @brief Invalid state for operation */
    UVRPC_ERROR_IO = -14,                 /**< @brief I/O error occurred */
    UVRPC_ERROR_MAX                       /**< @brief Maximum error code value */
} uvrpc_error_t;
```

## Example 5: Callback Type

```c
/**
 * @brief RPC request handler callback type
 * 
 * This callback is invoked when the server receives an RPC request
 * for a registered method. The handler should:
 * 1. Parse the request parameters
 * 2. Process the request
 * 3. Send a response using uvrpc_request_send_response()
 * 4. Free the request using uvrpc_request_free()
 * 
 * @param req Pointer to the request structure
 * @param ctx User-defined context pointer (passed during registration)
 * 
 * @pre req must not be NULL
 * @pre req->params and req->params_size must be valid
 * 
 * @note The handler MUST free the request using uvrpc_request_free()
 * @note The handler MUST call uvrpc_request_send_response() before freeing
 * 
 * @see uvrpc_server_register()
 * @see uvrpc_request_send_response()
 * @see uvrpc_request_free()
 */
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);
```

## Example 6: Grouping Functions

```c
/**
 * @defgroup server_api Server API
 * @brief Functions for creating and managing UVRPC servers
 * @{
 */

/**
 * @brief Creates a new UVRPC server instance
 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);

/**
 * @brief Starts the server and begins accepting connections
 */
int uvrpc_server_start(uvrpc_server_t* server);

/**
 * @brief Stops the server and closes all connections
 */
void uvrpc_server_stop(uvrpc_server_t* server);

/**
 * @brief Frees server resources
 */
void uvrpc_server_free(uvrpc_server_t* server);

/** @} */ // end of server_api group
```

## Example 7: Inline Comments

```c
/* Calculate ring buffer index using bitwise AND for power-of-2 sizes
 * This is faster than modulo operation and avoids division */
int idx = (req->msgid & (server->max_pending_requests - 1));

/* Check if generation matches to avoid stale requests
 * Generation counter prevents processing old requests from
 * previous server instances */
if (server->pending_requests[idx]->generation != req->generation) {
    /* Request is stale, skip it */
    continue;
}

/* Decode request using FlatBuffers deserializer
 * This validates the message format and extracts fields */
if (uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size) != UVRPC_OK) {
    UVRPC_LOG("Failed to decode request (size=%zu)", size);
    return;
}
```

## Example 8: Macro Documentation

```c
/**
 * @def UVRPC_MAX_PENDING_CALLBACKS
 * @brief Maximum number of pending callbacks in the ring buffer
 * 
 * This value must be a power of 2 for efficient modulo operation.
 * Recommended values: 65536 (2^16), 262144 (2^18), 1048576 (2^20), 4194304 (2^22)
 * Default: 2^20 = 1,048,576
 * 
 * @note Increasing this value allows more concurrent requests but uses more memory
 */
#ifndef UVRPC_MAX_PENDING_CALLBACKS
#define UVRPC_MAX_PENDING_CALLBACKS (1 << 20)  /* 1,048,576 */
#endif
```

## Example 9: Preprocessor Checks

```c
/**
 * @brief Validates server state before operation
 * 
 * @param server Pointer to server instance
 * @param operation Name of the operation being performed
 * 
 * @return UVRPC_OK if valid, error code otherwise
 */
static int validate_server_state(uvrpc_server_t* server, const char* operation) {
    if (!server) {
        UVRPC_LOG("%s: server is NULL", operation);
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (!server->is_running) {
        UVRPC_LOG("%s: server is not running", operation);
        return UVRPC_ERROR_INVALID_STATE;
    }
    
    return UVRPC_OK;
}
```

## Example 10: Error Handling

```c
/**
 * @brief Sends response to client
 * 
 * This function sends a response back to the client who made the request.
 * It handles encoding, transport, and error conditions.
 * 
 * @param req Pointer to the request structure
 * @param status Response status code (UVRPC_OK for success)
 * @param result Response data buffer (can be NULL)
 * @param result_size Size of result data in bytes
 * 
 * @return UVRPC_OK on success, error code on failure
 * 
 * @pre req must not be NULL
 * @pre req must be in a valid state (not yet freed)
 * 
 * @post The response is queued for sending to the client
 * @note This function does not block - sending is asynchronous
 * 
 * @see uvrpc_response_t
 * @see uvrpc_error_t
 */
int uvrpc_request_send_response(uvrpc_request_t* req, 
                                uvrpc_error_t status,
                                const uint8_t* result,
                                size_t result_size);
```

## Doxygen Tags Reference

### Basic Tags
- `@brief` - Brief description
- `@details` - Detailed description
- `@param` - Parameter description
- `@return` - Return value description
- `@note` - Additional notes
- `@warning` - Warning messages
- `@pre` - Precondition
- `@post` - Postcondition
- `@author` - Author
- `@date` - Date
- @version - Version

### Advanced Tags
- `@file` - File description
- `@struct` - Structure documentation
- `@typedef` - Type definition
- `@enum` - Enumeration documentation
- `@defgroup` - Create a group
- `@addtogroup` - Add to a group
- `@{` - Start of group
- `@}` - End of group
- `@see` - Cross-reference
- `@code` - Code block start
- `@endcode` - Code block end

### Formatting
- `@code ... @endcode` - Code example
- `@verbatim ... @endverbatim` - Verbatim text
- `@a <word>` - Emphasize word
- `@b <word>` - Bold word
- `@p <word>` - Typewriter word
- `@n` - New line
- `@li` - List item

## Anti-Patterns to Avoid

### ❌ Bad: No Documentation
```c
int uvrpc_server_create(uvrpc_config_t* config) {
    // No comments at all
}
```

### ❌ Bad: Minimal Comments
```c
// Create server
int uvrpc_server_create(uvrpc_config_t* config) {
}
```

### ❌ Bad: Obvious Comments
```c
// Return 0 on success
return 0;

// Set value to 0
value = 0;

// Loop through array
for (int i = 0; i < size; i++) {
}
```

### ❌ Bad: Chinese Comments
```c
// 创建服务器
int uvrpc_server_create(uvrpc_config_t* config) {
}

/* 这是一个处理器 */
void handler(uvrpc_request_t* req, void* ctx) {
}
```

## Best Practices Summary

1. **Always use English** for all comments and documentation
2. **Document public APIs** with full Doxygen comments
3. **Explain WHY, not just WHAT**
4. **Keep comments concise** and up-to-date
5. **Use @brief** for short descriptions
6. **Use @details** for longer explanations
7. **Document all parameters** with @param
8. **Document return values** with @return
9. **Add @note** for important information
10. **Use @pre/@post** for pre/post-conditions
11. **Use @see** for cross-references
12. **Include code examples** with @code/@endcode

## Generating Documentation

```bash
# Generate HTML documentation
doxygen Doxyfile

# View documentation
open docs/doxygen/html/index.html

# Generate PDF (requires LaTeX)
doxygen Doxyfile
make -C docs/doxygen/latex
```

## Resources

- [Doxygen Manual](https://www.doxygen.nl/manual/)
- [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html)
- [Doxygen Examples](https://www.doxygen.nl/manual/examples.html)
- [UVRPC Coding Standards](CODING_STANDARDS.md)

---

**Version**: 1.0  
**Last Updated**: 2026-02-18  
**Maintained By**: UVRPC Team