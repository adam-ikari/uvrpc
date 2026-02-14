/**
 * UVRPC Memory Allocator
 * 
 * 支持三种分配器模式：
 * - system: 使用标准 malloc/free（兼容性最好）
 * - mimalloc: 使用 mimalloc 高性能分配器（默认）
 * - custom: 使用用户提供的自定义分配器
 */

#ifndef UVRPC_ALLOCATOR_H
#define UVRPC_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配器类型 */
typedef enum {
    UVRPC_ALLOCATOR_SYSTEM = 0,   /* System malloc/free */
    UVRPC_ALLOCATOR_MIMALLOC = 1, /* Mimalloc (default) */
    UVRPC_ALLOCATOR_CUSTOM = 2    /* User-defined allocator */
} uvrpc_allocator_type_t;

/* 自定义分配器函数类型 */
typedef void* (*uvrpc_alloc_fn)(size_t size);
typedef void* (*uvrpc_calloc_fn)(size_t count, size_t size);
typedef void* (*uvrpc_realloc_fn)(void* ptr, size_t size);
typedef void (*uvrpc_free_fn)(void* ptr);

/* 自定义分配器接口 */
typedef struct {
    uvrpc_alloc_fn alloc;
    uvrpc_calloc_fn calloc;
    uvrpc_realloc_fn realloc;
    uvrpc_free_fn free;
    const char* name;
    void* user_data;
} uvrpc_custom_allocator_t;

/* 初始化分配器（运行时选择） */
void uvrpc_allocator_init(uvrpc_allocator_type_t type, const uvrpc_custom_allocator_t* custom);
void uvrpc_allocator_cleanup(void);

/* 获取当前分配器类型 */
uvrpc_allocator_type_t uvrpc_allocator_get_type(void);

/* 获取当前分配器名称 */
const char* uvrpc_allocator_get_name(void);

/* 内存分配函数（根据当前分配器类型分发） */
void* uvrpc_alloc(size_t size);
void* uvrpc_calloc(size_t count, size_t size);
void* uvrpc_realloc(void* ptr, size_t size);
void uvrpc_free(void* ptr);

/* 字符串复制辅助函数 */
char* uvrpc_strdup(const char* s);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_ALLOCATOR_H */