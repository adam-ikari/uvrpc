/**
 * UVRPC Memory Allocator Implementation
 */

#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Mimalloc support (conditionally compiled) */
#ifdef UVRPC_USE_MIMALLOC
#include <mimalloc.h>
#define mi_malloc(size) mi_malloc(size)
#define mi_free(ptr) mi_free(ptr)
#define mi_realloc(ptr, size) mi_realloc(ptr, size)
#define mi_calloc(count, size) mi_calloc(count, size)
#else
/* Fallback to system functions */
#define mi_malloc(size) malloc(size)
#define mi_free(ptr) free(ptr)
#define mi_realloc(ptr, size) realloc(ptr, size)
#define mi_calloc(count, size) calloc(count, size)
#endif

/* 静态默认分配器 */
static uvrpc_allocator_t* g_default_allocator = NULL;

/* 静态统计信息 */
#ifdef UVRPC_ALLOCATOR_STATS
static uvrpc_allocator_stats_t g_stats = {0, 0, 0, 0, 0};
#endif

/* System allocator functions */
static void* system_alloc(size_t size) {
#ifdef UVRPC_ALLOCATOR_STATS
    void* ptr = malloc(size);
    if (ptr) {
        g_stats.total_allocs++;
        g_stats.current_bytes += size;
        g_stats.total_bytes += size;
        if (g_stats.current_bytes > g_stats.peak_bytes) {
            g_stats.peak_bytes = g_stats.current_bytes;
        }
    }
    return ptr;
#else
    return malloc(size);
#endif
}

static void system_free(void* ptr) {
#ifdef UVRPC_ALLOCATOR_STATS
    if (ptr) {
        /* Note: 我们无法追踪释放的大小，所以current_bytes可能不准确 */
        /* 这是一个简化版本 */
        g_stats.total_frees++;
    }
#endif
    free(ptr);
}

static void* system_realloc(void* ptr, size_t size) {
#ifdef UVRPC_ALLOCATOR_STATS
    void* new_ptr = realloc(ptr, size);
    if (new_ptr) {
        g_stats.total_allocs++;  /* Count as a new allocation */
    }
    return new_ptr;
#else
    return realloc(ptr, size);
#endif
}

/* Mimalloc allocator functions */
static void* mimalloc_alloc(size_t size) {
#ifdef UVRPC_ALLOCATOR_STATS
    void* ptr = mi_malloc(size);
    if (ptr) {
        g_stats.total_allocs++;
        g_stats.current_bytes += size;
        g_stats.total_bytes += size;
        if (g_stats.current_bytes > g_stats.peak_bytes) {
            g_stats.peak_bytes = g_stats.current_bytes;
        }
    }
    return ptr;
#else
    return mi_malloc(size);
#endif
}

static void* mimalloc_calloc(size_t count, size_t size) {
#ifdef UVRPC_ALLOCATOR_STATS
    void* ptr = mi_calloc(count, size);
    if (ptr) {
        g_stats.total_allocs++;
        size_t total = count * size;
        g_stats.current_bytes += total;
        g_stats.total_bytes += total;
        if (g_stats.current_bytes > g_stats.peak_bytes) {
            g_stats.peak_bytes = g_stats.current_bytes;
        }
    }
    return ptr;
#else
    return mi_calloc(count, size);
#endif
}

static void mimalloc_free(void* ptr) {
#ifdef UVRPC_ALLOCATOR_STATS
    if (ptr) {
        g_stats.total_frees++;
    }
#endif
    mi_free(ptr);
}

static void* mimalloc_realloc(void* ptr, size_t size) {
#ifdef UVRPC_ALLOCATOR_STATS
    void* new_ptr = mi_realloc(ptr, size);
    if (new_ptr) {
        g_stats.total_allocs++;
    }
    return new_ptr;
#else
    return mi_realloc(ptr, size);
#endif
}

/* Static allocator instances */
static uvrpc_allocator_t g_system_allocator = {
    UVRPC_ALLOCATOR_SYSTEM,
    system_alloc,
    system_free,
    system_realloc,
    NULL
};

static uvrpc_allocator_t g_mimalloc_allocator = {
    UVRPC_ALLOCATOR_MIMALLOC,
    mimalloc_alloc,
    mimalloc_free,
    mimalloc_realloc,
    NULL
};

/* Public API implementation */

uvrpc_allocator_t* uvrpc_system_allocator(void) {
    return &g_system_allocator;
}

uvrpc_allocator_t* uvrpc_mimalloc_allocator(void) {
    return &g_mimalloc_allocator;
}

uvrpc_allocator_t* uvrpc_custom_allocator_new(
    uvrpc_alloc_fn_t alloc_fn,
    uvrpc_free_fn_t free_fn,
    uvrpc_realloc_fn_t realloc_fn,
    void* user_data
) {
    if (!alloc_fn || !free_fn) return NULL;
    
    uvrpc_allocator_t* allocator = (uvrpc_allocator_t*)malloc(sizeof(uvrpc_allocator_t));
    if (!allocator) return NULL;
    
    allocator->type = UVRPC_ALLOCATOR_CUSTOM;
    allocator->alloc = alloc_fn;
    allocator->free = free_fn;
    allocator->realloc = realloc_fn ? realloc_fn : system_realloc;
    allocator->user_data = user_data;
    
    return allocator;
}

uvrpc_allocator_t* uvrpc_get_default_allocator(void) {
    if (!g_default_allocator) {
        g_default_allocator = uvrpc_system_allocator();
    }
    return g_default_allocator;
}

void uvrpc_set_default_allocator(uvrpc_allocator_t* allocator) {
    g_default_allocator = allocator;
}

/* Wrapper functions */
void* uvrpc_alloc(size_t size) {
    uvrpc_allocator_t* allocator = uvrpc_get_default_allocator();
    return allocator->alloc(size);
}

void* uvrpc_calloc(size_t count, size_t size) {
    uvrpc_allocator_t* allocator = uvrpc_get_default_allocator();
    if (allocator->type == UVRPC_ALLOCATOR_MIMALLOC) {
        return mimalloc_calloc(count, size);
    } else {
        void* ptr = allocator->alloc(count * size);
        if (ptr) {
            memset(ptr, 0, count * size);
        }
        return ptr;
    }
}

void* uvrpc_realloc(void* ptr, size_t size) {
    uvrpc_allocator_t* allocator = uvrpc_get_default_allocator();
    return allocator->realloc(ptr, size);
}

void uvrpc_free(void* ptr) {
    uvrpc_allocator_t* allocator = uvrpc_get_default_allocator();
    allocator->free(ptr);
}

char* uvrpc_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)uvrpc_alloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

#ifdef UVRPC_ALLOCATOR_STATS
uvrpc_allocator_stats_t* uvrpc_allocator_get_stats(void) {
    return &g_stats;
}

void uvrpc_allocator_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}
#endif