/**
 * @file uvrpc_allocator.h
 * @brief UVRPC Memory Allocator
 * 
 * Provides a memory allocation interface supporting two modes, both chosen at
 * compile time:
 * - system: Uses standard malloc/free (best compatibility)
 * - mimalloc: Uses mimalloc high-performance allocator (default)
 *
 * There is no runtime-pluggable allocator. One would need a file-scope
 * function table, and "zero mutable globals" is a rule here rather than a
 * preference, so the option is deliberately absent.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 */

#ifndef UVRPC_ALLOCATOR_H
#define UVRPC_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocator type enumeration
 * 
 * Defines the available memory allocator types.
 */
typedef enum {
    UVRPC_ALLOCATOR_SYSTEM = 0,   /**< @brief System malloc/free */
    UVRPC_ALLOCATOR_MIMALLOC = 1  /**< @brief Mimalloc (default) */
} uvrpc_allocator_type_t;


/**
 * @defgroup AllocatorAPI Allocator API
 * @brief Functions for memory allocation
 * @{
 */

/**
 * @brief Report a runtime allocator request that cannot be honoured
 *
 * The allocator is fixed at compile time, so this cannot change it. Passing
 * a type other than the compiled-in one is logged and otherwise ignored.
 *
 * @param type Allocator type requested
 */
void uvrpc_allocator_init(uvrpc_allocator_type_t type);

/**
 * @brief Cleanup allocator resources
 */
void uvrpc_allocator_cleanup(void);

/**
 * @brief Get current allocator type
 * 
 * @return Current allocator type
 */
uvrpc_allocator_type_t uvrpc_allocator_get_type(void);

/**
 * @brief Get current allocator name
 * 
 * @return Allocator name string
 */
const char* uvrpc_allocator_get_name(void);

/**
 * @brief Allocate memory
 * 
 * @param size Size to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* uvrpc_alloc(size_t size);

/**
 * @brief Allocate zero-initialized memory
 * 
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
void* uvrpc_calloc(size_t count, size_t size);

/**
 * @brief Reallocate memory
 * 
 * @param ptr Pointer to reallocate
 * @param size New size
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* uvrpc_realloc(void* ptr, size_t size);

/**
 * @brief Free memory
 * 
 * @param ptr Pointer to free
 */
void uvrpc_free(void* ptr);

/**
 * @brief Duplicate a string
 * 
 * @param s String to duplicate
 * @return Newly allocated string, or NULL on failure
 */
char* uvrpc_strdup(const char* s);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_ALLOCATOR_H */