/**
 * @file uvrpc_allocator.h
 * @brief UVRPC Memory Allocator
 * 
 * Provides a flexible memory allocation interface supporting three modes:
 * - system: Uses standard malloc/free (best compatibility)
 * - mimalloc: Uses mimalloc high-performance allocator (default)
 * - custom: Uses user-provided custom allocator
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
    UVRPC_ALLOCATOR_MIMALLOC = 1, /**< @brief Mimalloc (default) */
    UVRPC_ALLOCATOR_CUSTOM = 2    /**< @brief User-defined allocator */
} uvrpc_allocator_type_t;

/**
 * @brief Custom allocator function type - alloc
 * 
 * @param size Size to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
typedef void* (*uvrpc_alloc_fn)(size_t size);

/**
 * @brief Custom allocator function type - calloc
 * 
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
typedef void* (*uvrpc_calloc_fn)(size_t count, size_t size);

/**
 * @brief Custom allocator function type - realloc
 * 
 * @param ptr Pointer to reallocate
 * @param size New size
 * @return Pointer to reallocated memory, or NULL on failure
 */
typedef void* (*uvrpc_realloc_fn)(void* ptr, size_t size);

/**
 * @brief Custom allocator function type - free
 * 
 * @param ptr Pointer to free
 */
typedef void (*uvrpc_free_fn)(void* ptr);

/**
 * @brief Custom allocator interface
 * 
 * Structure containing function pointers for a custom allocator.
 */
typedef struct {
    uvrpc_alloc_fn alloc;       /**< @brief Allocation function */
    uvrpc_calloc_fn calloc;     /**< @brief Calloc function */
    uvrpc_realloc_fn realloc;   /**< @brief Realloc function */
    uvrpc_free_fn free;         /**< @brief Free function */
    const char* name;           /**< @brief Allocator name */
    void* user_data;            /**< @brief User data */
} uvrpc_custom_allocator_t;

/**
 * @defgroup AllocatorAPI Allocator API
 * @brief Functions for memory allocation
 * @{
 */

/**
 * @brief Initialize allocator (runtime selection)
 * 
 * @param type Allocator type
 * @param custom Custom allocator (required if type is CUSTOM)
 */
void uvrpc_allocator_init(uvrpc_allocator_type_t type, const uvrpc_custom_allocator_t* custom);

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