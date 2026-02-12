/**
 * UVRPC Memory Allocator
 * 
 * 编译期配置，通过 CMake 定义 UVRPC_ALLOCATOR
 * 默认使用 Mimalloc (高性能)
 */

#ifndef UVRPC_ALLOCATOR_H
#define UVRPC_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配器类型（仅用于编译时检查） */
typedef enum {
    UVRPC_ALLOCATOR_SYSTEM = 0,
    UVRPC_ALLOCATOR_MIMALLOC = 1
} uvrpc_allocator_type_t;

/* 编译时选择分配器（默认 Mimalloc） */
#ifndef UVRPC_ALLOCATOR
#define UVRPC_ALLOCATOR 1  /* 0: system, 1: mimalloc */
#endif

/* Mimalloc 支持 */
#if UVRPC_ALLOCATOR == 1
#include <mimalloc.h>
#endif

/* 内联内存分配函数 */
static inline void* uvrpc_alloc(size_t size) {
#if UVRPC_ALLOCATOR == 1
    return mi_malloc(size);
#else
    return malloc(size);
#endif
}

static inline void* uvrpc_calloc(size_t count, size_t size) {
#if UVRPC_ALLOCATOR == 1
    return mi_calloc(count, size);
#else
    void* ptr = malloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
#endif
}

static inline void* uvrpc_realloc(void* ptr, size_t size) {
#if UVRPC_ALLOCATOR == 1
    return mi_realloc(ptr, size);
#else
    return realloc(ptr, size);
#endif
}

static inline void uvrpc_free(void* ptr) {
#if UVRPC_ALLOCATOR == 1
    mi_free(ptr);
#else
    free(ptr);
#endif
}

static inline char* uvrpc_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)uvrpc_alloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* 输出当前分配器（用于调试） */
const char* uvrpc_allocator_get_name(void);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_ALLOCATOR_H */