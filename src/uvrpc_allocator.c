/**
 * @file uvrpc_allocator.c
 * @brief UVRPC Memory Allocator Implementation
 *
 * Compile-time allocator selection (no runtime mutable global state):
 * - system:   Standard malloc/free  (-DUVRPC_ALLOCATOR_DEFAULT=system)
 * - mimalloc: High-performance mimalloc (-DUVRPC_ALLOCATOR_DEFAULT=mimalloc)
 * - custom:   User-defined allocator (-DUVRPC_ALLOCATOR_DEFAULT=custom)
 *
 * The allocator type is fixed at compile time via UVRPC_DEFAULT_ALLOCATOR, so
 * uvrpc_alloc/free dispatch through preprocessor selection rather than a
 * runtime type variable. The only file-scope state is g_custom_allocator,
 * which exists ONLY in CUSTOM builds (where the user must register function
 * pointers once via uvrpc_allocator_init). system/mimalloc builds have ZERO
 * allocator globals, honoring the "zero global variables" design philosophy.
 *
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc_allocator.h"
#include "../include/uvrpc.h"   /* for UVRPC_LOG_ERROR logging macros */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Allocator type constants (must match header enum) */
#define UVRPC_ALLOCATOR_SYSTEM 0
#define UVRPC_ALLOCATOR_MIMALLOC 1
#define UVRPC_ALLOCATOR_CUSTOM 2

/* Compile-time default allocator (set via CMake -DUVRPC_ALLOCATOR_DEFAULT).
 * Defaults to mimalloc when not specified. */
#ifndef UVRPC_DEFAULT_ALLOCATOR
#define UVRPC_DEFAULT_ALLOCATOR UVRPC_ALLOCATOR_MIMALLOC
#endif

/* Mimalloc support (enabled at compile time only) */
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
#include <mimalloc.h>
#endif

/* Custom allocator state. ONLY present in CUSTOM builds: the user registers a
 * set of function pointers once via uvrpc_allocator_init(). In system/mimalloc
 * builds there is no allocator state at all. Written once at init; not mutated
 * per-request. */
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_CUSTOM
static uvrpc_custom_allocator_t g_custom_allocator = {0};
#endif

/* ---- Per-build allocator primitives ------------------------------------- */

#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_SYSTEM

static void* allocator_alloc(size_t size)   { return malloc(size); }
static void* allocator_calloc(size_t n, size_t s) { return calloc(n, s); }
static void* allocator_realloc(void* p, size_t s) { return realloc(p, s); }
static void  allocator_free(void* p)        { free(p); }

#elif UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC

static void* allocator_alloc(size_t size)   { return mi_malloc(size); }
static void* allocator_calloc(size_t n, size_t s) { return mi_calloc(n, s); }
static void* allocator_realloc(void* p, size_t s) { return mi_realloc(p, s); }
static void  allocator_free(void* p)        { return mi_free(p); }

#else /* UVRPC_ALLOCATOR_CUSTOM */

static void* allocator_alloc(size_t size) {
    return g_custom_allocator.alloc ? g_custom_allocator.alloc(size) : malloc(size);
}
static void* allocator_calloc(size_t count, size_t size) {
    return g_custom_allocator.calloc ? g_custom_allocator.calloc(count, size) : calloc(count, size);
}
static void* allocator_realloc(void* ptr, size_t size) {
    return g_custom_allocator.realloc ? g_custom_allocator.realloc(ptr, size) : realloc(ptr, size);
}
static void  allocator_free(void* ptr) {
    if (g_custom_allocator.free) { g_custom_allocator.free(ptr); }
    else { free(ptr); }
}

#endif

/* ---- Public API --------------------------------------------------------- */

/* Initialize allocator.
 *
 * The allocator TYPE is fixed at compile time (UVRPC_DEFAULT_ALLOCATOR); this
 * function cannot change it. It is retained for API compatibility and to
 * register the custom allocator function pointers in CUSTOM builds.
 * - In CUSTOM builds: stores *custom (validated) for use by the alloc fns.
 * - In system/mimalloc builds: logs a warning if `type` does not match the
 *   compile-time choice, then returns (behavior is unchanged — the type is
 *   fixed at compile time). */
void uvrpc_allocator_init(uvrpc_allocator_type_t type, const uvrpc_custom_allocator_t* custom) {
    if (type != UVRPC_DEFAULT_ALLOCATOR) {
        UVRPC_LOG_ERROR("uvrpc_allocator_init: requested type %d but allocator is "
                        "compile-time fixed to %d; ignoring runtime type.",
                        (int)type, (int)UVRPC_DEFAULT_ALLOCATOR);
    }

#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_CUSTOM
    if (custom) {
        if (custom->alloc && custom->calloc && custom->realloc && custom->free) {
            g_custom_allocator = *custom;
        } else {
            UVRPC_LOG_ERROR("Invalid custom allocator (missing function pointers); "
                            "falling back to system malloc");
            memset(&g_custom_allocator, 0, sizeof(g_custom_allocator));
        }
    }
#else
    (void)custom;
#endif
}

/* Cleanup allocator resources. No-op except in CUSTOM builds (clears the
 * registered function pointers so allocations fall back to system malloc). */
void uvrpc_allocator_cleanup(void) {
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_CUSTOM
    memset(&g_custom_allocator, 0, sizeof(g_custom_allocator));
#endif
}

/* The allocator type is fixed at compile time. */
uvrpc_allocator_type_t uvrpc_allocator_get_type(void) {
    return (uvrpc_allocator_type_t)UVRPC_DEFAULT_ALLOCATOR;
}

const char* uvrpc_allocator_get_name(void) {
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_SYSTEM
    return "system";
#elif UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
    return "mimalloc";
#else
    return g_custom_allocator.name ? g_custom_allocator.name : "custom";
#endif
}

/* Memory allocation functions — compile-time dispatched, no runtime branch. */
void* uvrpc_alloc(size_t size) {
    return allocator_alloc(size);
}

void* uvrpc_calloc(size_t count, size_t size) {
    return allocator_calloc(count, size);
}

void* uvrpc_realloc(void* ptr, size_t size) {
    return allocator_realloc(ptr, size);
}

void uvrpc_free(void* ptr) {
    if (!ptr) return;
    allocator_free(ptr);
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
