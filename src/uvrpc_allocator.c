/**
 * @file uvrpc_allocator.c
 * @brief UVRPC Memory Allocator Implementation
 *
 * Compile-time allocator selection (no runtime mutable global state):
 * - system:   Standard malloc/free  (-DUVRPC_ALLOCATOR_DEFAULT=system)
 * - mimalloc: High-performance mimalloc (-DUVRPC_ALLOCATOR_DEFAULT=mimalloc)
 *
 * The allocator is chosen at compile time (UVRPC_DEFAULT_ALLOCATOR) and
 * uvrpc_alloc/free dispatch through preprocessor selection rather than a
 * runtime type variable. There is no file-scope mutable state of any kind: a
 * pluggable allocator would need one, and "zero mutable globals" is not a
 * guideline here but the rule, so the option is not offered.
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

/* Compile-time default allocator (set via CMake -DUVRPC_ALLOCATOR_DEFAULT).
 * Defaults to mimalloc when not specified. */
#ifndef UVRPC_DEFAULT_ALLOCATOR
#define UVRPC_DEFAULT_ALLOCATOR UVRPC_ALLOCATOR_MIMALLOC
#endif

/* Mimalloc support (enabled at compile time only) */
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_MIMALLOC
#include <mimalloc.h>
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
static void  allocator_free(void* p)        { mi_free(p); }

#endif

/* ---- Public API --------------------------------------------------------- */

/* The allocator is fixed at compile time, so there is nothing to initialize.
 * Kept because it names the contract in one place: asking for a different
 * allocator at runtime changes nothing, and saying so is more useful than a
 * silent no-op. */
void uvrpc_allocator_init(uvrpc_allocator_type_t type) {
    if (type != UVRPC_DEFAULT_ALLOCATOR) {
        UVRPC_LOG_ERROR("uvrpc_allocator_init: requested type %d but allocator is "
                        "compile-time fixed to %d; ignoring runtime type.",
                        (int)type, (int)UVRPC_DEFAULT_ALLOCATOR);
    }
}

/* Nothing to release: the allocator holds no state. */
void uvrpc_allocator_cleanup(void) {
}

/* The allocator type is fixed at compile time. */
uvrpc_allocator_type_t uvrpc_allocator_get_type(void) {
    return (uvrpc_allocator_type_t)UVRPC_DEFAULT_ALLOCATOR;
}

const char* uvrpc_allocator_get_name(void) {
#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_SYSTEM
    return "system";
#else
    return "mimalloc";
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
