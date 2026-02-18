# UVRPC Coding Standards and Documentation Guidelines

## Overview

This document describes the coding standards and documentation guidelines for UVRPC, including requirements for English comments and Doxygen format.

## General Principles

### 1. Language

- **All comments MUST be in English**
- **All documentation MUST be in English**
- **Git commit messages MUST be in English**

### 2. Comment Style

Use clear, concise, and descriptive comments:
- Explain WHY, not just WHAT
- Keep comments up-to-date
- Avoid obvious comments

## Doxygen Documentation Guidelines

### File Header

Every source file should start with a Doxygen file header:

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

### Function Documentation

Use Doxygen format for all public functions:

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

### Structure Documentation

Document all public structures:

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

### Type Definitions

Document all public types:

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

### Callback Types

Document callback function pointers:

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

### Member Variables

Document structure members with @brief:

```c
typedef struct uvrpc_client {
    uv_loop_t* loop;           /**< @brief libuv event loop for this client */
    char* address;             /**< @brief Server address */
    uvbus_t* uvbus;             /**< @brief UVBus transport layer */
    int is_connected;          /**< @brief Connection status flag */
    uvrpc_context_t* ctx;       /**< @brief User-defined context */
} uvrpc_client_t;
```

## Inline Comments

Use inline comments sparingly, only for complex logic:

```c
/* Calculate ring buffer index using bitwise AND for power-of-2 sizes */
int idx = (req->msgid & (server->max_pending_requests - 1));

/* Check if generation matches to avoid stale requests */
if (server->pending_requests[idx]->generation != req->generation) {
    /* Request is stale, skip it */
    continue;
}
```

## Comment Anti-Patterns

### ❌ Bad Comments

```c
// Set x to 0
int x = 0;

// Loop through array
for (int i = 0; i < size; i++) {
    // Process element
    process(arr[i]);
}

/* Function that does something */
void some_function() {
    // Do stuff
}
```

### ✅ Good Comments

```c
/* Initialize ring buffer index to ensure valid starting position */
int idx = 0;

/* Process all active request slots in the ring buffer */
for (int i = 0; i < server->max_pending_requests; i++) {
    pending_request_t* req = server->pending_requests[i];
    if (req->in_use) {
        process_request(req);
    }
}

/**
 * @brief Validates request parameters before processing
 * 
 * This function checks if the request has valid parameters
 * according to the method signature. It verifies:
 * - Parameter size matches expected size
 * - Parameter values are within valid ranges
 * - No buffer overflow would occur
 * 
 * @param req The request to validate
 * @return UVRPC_OK if valid, error code otherwise
 */
int validate_request(uvrpc_request_t* req);
```

## Code Organization

### File Structure

Each source file should follow this structure:

```c
/**
 * @file filename.c
 * @brief Brief description
 */

/* Standard library includes */
#include <stdio.h>
#include <stdlib.h>

/* Project includes */
#include "../include/uvrpc.h"
#include "internal_header.h"

/* Local definitions */
#define MAX_SIZE 1024

/* Type definitions */
typedef struct {
    int field1;
    int field2;
} local_type_t;

/* Global variables (avoid if possible) */
static int g_counter = 0;

/* Function declarations */
static void helper_function(void);

/* Function implementations */

/**
 * @brief Main function
 */
int main() {
    /* Implementation */
}
```

## Naming Conventions

### Functions
```c
// Good: Clear and descriptive
int uvrpc_server_start(uvrpc_server_t* server);
int uvrpc_client_connect(uvrpc_client_t* client);

// Bad: Vague or abbreviated
int svr_start(void* svr);
int conn_cli(void* cli);
```

### Variables
```c
// Good: Descriptive names
int max_pending_requests;
int current_connection_count;

// Bad: Abbreviations or single letters
int max_req;
int curr_cnt;
```

### Constants
```c
// Good: UPPERCASE with underscores
#define MAX_PENDING_REQUESTS 1048576
#define DEFAULT_TIMEOUT_MS 5000

// Bad: Lowercase or unclear names
#define max_pending 1048576
#define timeout 5000
```

## Best Practices

### 1. Keep Comments Concise

```c
// Good
/* Calculate ring buffer index using modulo operation */
int idx = req->msgid % server->max_pending_requests;

// Bad
/* This line calculates the index by taking the message ID
   and using the modulo operator with the maximum pending
   requests to get the correct index in the ring buffer */
int idx = req->msgid % server->max_pending_requests;
```

### 2. Document Non-Obvious Logic

```c
/* Use bitwise AND instead of modulo for power-of-2 sizes
 * because it's faster and avoids division operation */
int idx = req->msgid & (server->max_pending_requests - 1);
```

### 3. Document Invariants

```c
/* Invariant: pending_requests array must always have a power-of-2 size
 * This enables efficient bitwise modulo operation */
static_assert(IS_POWER_OF_TWO(MAX_PENDING_REQUESTS), 
               "MAX_PENDING_REQUESTS must be power of 2");
```

### 4. Document Resource Ownership

```c
/**
 * @brief Creates a new server instance
 * 
 * The caller owns the returned server pointer and must free it
 * using uvrpc_server_free() when no longer needed.
 * 
 * @return Pointer to server instance, or NULL on failure
 * 
 * @note The returned pointer must be freed by the caller
 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
```

## Doxygen Special Commands

### Grouping Related Functions

```c
/**
 * @defgroup server_api Server API
 * @brief Functions for creating and managing UVRPC servers
 * @{
 */

uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
int uvrpc_server_start(uvrpc_server_t* server);
void uvrpc_server_free(uvrpc_server_t* server);

/** @} */ // end of server_api group
```

### Documenting Modules

```c
/**
 * @mainpage UVRPC Documentation
 * 
 * UVRPC (Ultra-Fast RPC) is a high-performance RPC framework
 * designed with the following principles:
 * - Zero threads: All I/O managed by libuv event loop
 * - Zero locks: No global variables or shared state
 * - Zero global variables: Complete isolation
 * 
 * @section features Features
 * - High throughput: 100,000+ ops/s for INPROC
 * - Low latency: 0.03ms average for INPROC
 * - Multiple transports: TCP, UDP, IPC, INPROC
 * - Two modes: Client-Server and Broadcast
 * 
 * @section getting_started Getting Started
 * See @ref quick_start for a 5-minute tutorial.
 */

/**
 * @page quick_start Quick Start Guide
 * 
 * This page provides a quick start guide for UVRPC.
 * 
 * @section step1 Step 1: Create a Server
 * @code
 * uvrpc_server_t* server = uvrpc_server_create(config);
 * uvrpc_server_register(server, "method", handler, NULL);
 * uvrpc_server_start(server);
 * @endcode
 * 
 * @section step2 Step 2: Create a Client
 * @code
 * uvrpc_client_t* client = uvrpc_client_create(config);
 * uvrpc_client_connect(client);
 * uvrpc_client_call(client, "method", params, size, callback, ctx);
 * @endcode
 */
```

## Generating Documentation

### Using Doxygen

```bash
# Generate HTML documentation
doxygen Doxyfile

# View documentation
open docs/doxygen/html/index.html
```

### Doxygen Configuration

The Doxyfile is located at the root of the project and includes:
- Project information
- Input/output settings
- Formatting options
- Diagram generation settings

## Review Checklist

Before committing code, verify:

- [ ] All comments are in English
- [ ] Public functions have Doxygen comments
- [ ] Structures are documented
- [ ] Types are documented
- [ ] Comments explain WHY, not just WHAT
- [ ] No obsolete comments
- [ ] Code compiles without warnings
- [ ] Documentation generates successfully

## Resources

- [Doxygen Manual](https://www.doxygen.nl/manual/)
- [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html)
- [Writing Comments](https://stackoverflow.com/questions/291825/writing-comments-when-to-and-when-not-to)

## Examples

See the following files for good examples:
- `include/uvrpc.h` - Public API documentation
- `include/uvrpc_config.h` - Configuration structure
- `include/uvrpc_async.h` - Async operations
- `include/uvrpc_allocator.h` - Memory management
- `examples/complete_example.c` - Complete usage example

---

**Version**: 1.0  
**Last Updated**: 2026-02-18  
**Maintained By**: UVRPC Team