/**
 * UVRPC Memory Allocator Implementation
 * 
 * 编译期配置，无运行时开销
 */

#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdio.h>

/* 当前选择的分配器 */
#if UVRPC_ALLOCATOR == 1
static const char* g_allocator_name = "mimalloc";
#else
static const char* g_allocator_name = "system";
#endif

/* 输出当前分配器（用于调试） */
const char* uvrpc_allocator_get_name(void) {
    return g_allocator_name;
}

/* 所有分配函数已内联在头文件中，此处无需实现 */