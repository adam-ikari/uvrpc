/**
 * UVRPC Memory Allocator Abstraction
 * 
 * 支持三种内存分配器：
 * 1. System - 系统默认分配器 (malloc/free)
 * 2. Mimalloc - 高性能内存分配器
 * 3. Custom - 自定义分配器（用户提供的函数指针）
 */

#ifndef UVRPC_ALLOCATOR_H
#define UVRPC_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配器类型 */
typedef enum {
    UVRPC_ALLOCATOR_SYSTEM = 0,   /* 系统默认 (malloc/free) */
    UVRPC_ALLOCATOR_MIMALLOC = 1,  /* mimalloc */
    UVRPC_ALLOCATOR_CUSTOM = 2     /* 自定义分配器 */
} uvrpc_allocator_type_t;

/* 自定义分配器函数指针类型 */
typedef void* (*uvrpc_alloc_fn_t)(size_t size);
typedef void (*uvrpc_free_fn_t)(void* ptr);
typedef void* (*uvrpc_realloc_fn_t)(void* ptr, size_t size);

/* 分配器接口 */
typedef struct {
    uvrpc_allocator_type_t type;
    uvrpc_alloc_fn_t alloc;
    uvrpc_free_fn_t free;
    uvrpc_realloc_fn_t realloc;
    void* user_data;  /* 用户自定义数据 */
} uvrpc_allocator_t;

/* 默认分配器（系统） */
extern uvrpc_allocator_t* uvrpc_system_allocator(void);

/* Mimalloc 分配器 */
extern uvrpc_allocator_t* uvrpc_mimalloc_allocator(void);

/* 自定义分配器 */
uvrpc_allocator_t* uvrpc_custom_allocator_new(
    uvrpc_alloc_fn_t alloc_fn,
    uvrpc_free_fn_t free_fn,
    uvrpc_realloc_fn_t realloc_fn,
    void* user_data
);

/* 获取当前默认分配器 */
uvrpc_allocator_t* uvrpc_get_default_allocator(void);

/* 设置默认分配器 */
void uvrpc_set_default_allocator(uvrpc_allocator_t* allocator);

/* 使用分配器的包装函数 */
void* uvrpc_alloc(size_t size);
void* uvrpc_calloc(size_t count, size_t size);
void* uvrpc_realloc(void* ptr, size_t size);
void uvrpc_free(void* ptr);
char* uvrpc_strdup(const char* s);

/* 统计信息（可选，如果编译时启用） */
#ifdef UVRPC_ALLOCATOR_STATS
typedef struct {
    size_t total_allocs;    /* 总分配次数 */
    size_t total_frees;     /* 总释放次数 */
    size_t current_bytes;   /* 当前分配的字节数 */
    size_t peak_bytes;      /* 峰值字节数 */
    size_t total_bytes;     /* 累计分配字节数 */
} uvrpc_allocator_stats_t;

uvrpc_allocator_stats_t* uvrpc_allocator_get_stats(void);
void uvrpc_allocator_reset_stats(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_ALLOCATOR_H */