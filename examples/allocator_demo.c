/**
 * UVRPC Memory Allocator Demo
 * 演示编译期配置的内存分配器
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== UVRPC Memory Allocator Demo ===\n\n");
    
    /* 获取当前分配器名称 */
    const char* allocator_name = uvrpc_allocator_get_name();
    printf("Current allocator: %s\n", allocator_name);
    
    /* 测试分配器 */
    printf("\nTesting allocator...\n");
    void* ptr1 = uvrpc_alloc(100);
    void* ptr2 = uvrpc_alloc(200);
    
    if (ptr1 && ptr2) {
        printf("Successfully allocated: 100 bytes and 200 bytes\n");
        uvrpc_free(ptr1);
        uvrpc_free(ptr2);
        printf("Successfully freed both allocations\n");
    } else {
        printf("Allocation failed!\n");
        return 1;
    }
    
    /* 说明 */
    printf("\n=== Notes ===\n");
    printf("UVRPC uses compile-time allocator configuration:\n");
    printf("  - Compile with: cmake -DUVRPC_ALLOCATOR_DEFAULT=mimalloc .\n");
    printf("  - Or compile with: cmake -DUVRPC_ALLOCATOR_DEFAULT=system .\n");
    printf("  - All allocations use inline functions for zero overhead.\n");
    printf("  - No runtime configuration needed.\n");
    
    return 0;
}