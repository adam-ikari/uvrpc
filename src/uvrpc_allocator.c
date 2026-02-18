/**
 * @file uvrpc_allocator.c
 * @brief UVRPC Memory Allocator Implementation
 * 
 * Supports runtime allocator type selection:
 * - system: Standard malloc/free
 * - mimalloc: High-performance mimalloc
 * - custom: User-defined allocator
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Allocator type constants (must match header enum) */
#define UVRPC_ALLOCATOR_SYSTEM 0
#define UVRPC_ALLOCATOR_MIMALLOC 1
#define UVRPC_ALLOCATOR_CUSTOM 2

/* Compile-time default allocator (can be modified via CMake) */
#ifndef UVRPC_DEFAULT_ALLOCATOR
#define UVRPC_DEFAULT_ALLOCATOR UVRPC_ALLOCATOR_MIMALLOC
#endif

/* Mimalloc support (enabled at compile time only) */
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
#include <mimalloc.h>
#endif

/* Global allocator state */
static uvrpc_allocator_type_t g_allocator_type = UVRPC_DEFAULT_ALLOCATOR;
static uvrpc_custom_allocator_t g_custom_allocator = {0};

/* System allocator functions */
static void* system_alloc(size_t size) {
    return malloc(size);
}

static void* system_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

static void* system_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

static void system_free(void* ptr) {
    free(ptr);
}

/* Mimalloc allocator functions (enabled at compile time only) */
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
static void* mimalloc_alloc(size_t size) {
    return mi_malloc(size);
}

static void* mimalloc_calloc(size_t count, size_t size) {
    return mi_calloc(count, size);
}

static void* mimalloc_realloc(void* ptr, size_t size) {
    return mi_realloc(ptr, size);
}

static void mimalloc_free(void* ptr) {
    mi_free(ptr);
}
#endif

/* Initialize allocator */
void uvrpc_allocator_init(uvrpc_allocator_type_t type, const uvrpc_custom_allocator_t* custom) {
    g_allocator_type = type;

    if (type == UVRPC_ALLOCATOR_CUSTOM && custom) {
        if (custom->alloc && custom->calloc && custom->realloc && custom->free) {
            g_custom_allocator = *custom;
        } else {
            fprintf(stderr, "Warning: Invalid custom allocator, falling back to system\n");
            g_allocator_type = UVRPC_ALLOCATOR_SYSTEM;
        }
    }
}

/* Cleanup allocator */
void uvrpc_allocator_cleanup(void) {
    /* Reset to default allocator */
    g_allocator_type = UVRPC_DEFAULT_ALLOCATOR;
    memset(&g_custom_allocator, 0, sizeof(g_custom_allocator));
}

/* Get current allocator type */
uvrpc_allocator_type_t uvrpc_allocator_get_type(void) {
    return g_allocator_type;
}

/* Get current allocator name */
const char* uvrpc_allocator_get_name(void) {
    switch (g_allocator_type) {
        case UVRPC_ALLOCATOR_SYSTEM:
            return "system";
        case UVRPC_ALLOCATOR_MIMALLOC:
            return "mimalloc";
        case UVRPC_ALLOCATOR_CUSTOM:
            return g_custom_allocator.name ? g_custom_allocator.name : "custom";
        default:
            return "unknown";
    }
}

/* Memory allocation functions */
void* uvrpc_alloc(size_t size) {
    switch (g_allocator_type) {
        case UVRPC_ALLOCATOR_SYSTEM:
            return system_alloc(size);
        case UVRPC_ALLOCATOR_MIMALLOC:
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
            return mimalloc_alloc(size);
#else
            fprintf(stderr, "Error: Mimalloc not compiled in\n");
            return system_alloc(size);
#endif
        case UVRPC_ALLOCATOR_CUSTOM:
            return g_custom_allocator.alloc ? g_custom_allocator.alloc(size) : system_alloc(size);
        default:
            return system_alloc(size);
    }
}

void* uvrpc_calloc(size_t count, size_t size) {
    switch (g_allocator_type) {
        case UVRPC_ALLOCATOR_SYSTEM:
            return system_calloc(count, size);
        case UVRPC_ALLOCATOR_MIMALLOC:
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
            return mimalloc_calloc(count, size);
#else
            fprintf(stderr, "Error: Mimalloc not compiled in\n");
            return system_calloc(count, size);
#endif
        case UVRPC_ALLOCATOR_CUSTOM:
            return g_custom_allocator.calloc ? g_custom_allocator.calloc(count, size) : system_calloc(count, size);
        default:
            return system_calloc(count, size);
    }
}

void* uvrpc_realloc(void* ptr, size_t size) {
    switch (g_allocator_type) {
        case UVRPC_ALLOCATOR_SYSTEM:
            return system_realloc(ptr, size);
        case UVRPC_ALLOCATOR_MIMALLOC:
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
            return mimalloc_realloc(ptr, size);
#else
            fprintf(stderr, "Error: Mimalloc not compiled in\n");
            return system_realloc(ptr, size);
#endif
        case UVRPC_ALLOCATOR_CUSTOM:
            return g_custom_allocator.realloc ? g_custom_allocator.realloc(ptr, size) : system_realloc(ptr, size);
        default:
            return system_realloc(ptr, size);
    }
}

void uvrpc_free(void* ptr) {
    if (!ptr) return;

    switch (g_allocator_type) {
        case UVRPC_ALLOCATOR_SYSTEM:
            system_free(ptr);
            break;
        case UVRPC_ALLOCATOR_MIMALLOC:
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
            mimalloc_free(ptr);
#else
            fprintf(stderr, "Error: Mimalloc not compiled in\n");
            system_free(ptr);
#endif
            break;
        case UVRPC_ALLOCATOR_CUSTOM:
            if (g_custom_allocator.free) {
                g_custom_allocator.free(ptr);
            } else {
                system_free(ptr);
            }
            break;
        default:
            system_free(ptr);
            break;
    }
}

/* String duplication helper function */
char* uvrpc_strdup(const char* s) {
    if (!s) return NULL;

    size_t len = strlen(s) + 1;
    char* copy = (char*)uvrpc_alloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}