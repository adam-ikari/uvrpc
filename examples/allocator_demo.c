/**
 * UVRPC Memory Allocator Demo
 * 演示三种内存分配器的使用
 */

#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stdlib.h>
/* 自定义分配器实现 */
static size_t custom_alloc_count = 0;
static size_t custom_free_count = 0;

static void* custom_alloc(size_t size) {
    custom_alloc_count++;
    printf("[Custom] Allocating %zu bytes (total: %zu)\n", size, custom_alloc_count);
    void* ptr = malloc(size);
    return ptr;
}

static void custom_free(void* ptr) {
    if (ptr) {
        custom_free_count++;
        printf("[Custom] Freeing pointer (total: %zu)\n", custom_free_count);
        free(ptr);
    }
}

static void* custom_realloc(void* ptr, size_t size) {
    printf("[Custom] Reallocating to %zu bytes\n", size);
    return realloc(ptr, size);
}

/* 性能测试 */
static void performance_test(const char* name, int iterations) {
    printf("\n=== %s Performance Test ===\n", name);
    
    clock_t start = clock();
    
    for (int i = 0; i < iterations; i++) {
        void* ptr = uvrpc_alloc(1024);
        if (ptr) {
            memset(ptr, 0, 1024);
            uvrpc_free(ptr);
        }
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("Iterations: %d\n", iterations);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.0f ops/sec\n", iterations / elapsed);
}

int main(int argc, char** argv) {
    printf("UVRPC Memory Allocator Demo\n");
    printf("============================\n\n");

    /* 1. System Allocator */
    printf("1. System Allocator (malloc/free)\n");
    uvrpc_set_default_allocator(uvrpc_system_allocator());
    
    char* str1 = (char*)uvrpc_alloc(128);
    strcpy(str1, "Hello from system allocator!");
    printf("   Allocated: %s\n", str1);
    uvrpc_free(str1);
    
    char* str2 = uvrpc_strdup("Duplication test");
    printf("   Strdup: %s\n", str2);
    uvrpc_free(str2);
    
    performance_test("System", 100000);

#ifdef UVRPC_ALLOCATOR_STATS
    uvrpc_allocator_stats_t* stats = uvrpc_allocator_get_stats();
    printf("   Stats: allocs=%zu, frees=%zu, bytes=%zu\n",
           stats->total_allocs, stats->total_frees, stats->total_bytes);
    uvrpc_allocator_reset_stats();
#endif

    /* 2. Mimalloc Allocator (if available) */
#ifdef UVRPC_USE_MIMALLOC
    printf("\n2. Mimalloc Allocator\n");
    uvrpc_set_default_allocator(uvrpc_mimalloc_allocator());
    
    char* str3 = (char*)uvrpc_alloc(256);
    strcpy(str3, "Hello from mimalloc allocator!");
    printf("   Allocated: %s\n", str3);
    uvrpc_free(str3);
    
    performance_test("Mimalloc", 100000);

#ifdef UVRPC_ALLOCATOR_STATS
    stats = uvrpc_allocator_get_stats();
    printf("   Stats: allocs=%zu, frees=%zu, bytes=%zu\n",
           stats->total_allocs, stats->total_frees, stats->total_bytes);
    uvrpc_allocator_reset_stats();
#endif
#else
    printf("\n2. Mimalloc Allocator (not compiled - use -DUVRPC_ALLOCATOR=mimalloc)\n");
#endif

    /* 3. Custom Allocator */
    printf("\n3. Custom Allocator\n");
    uvrpc_allocator_t* custom = uvrpc_custom_allocator_new(
        custom_alloc, custom_free, custom_realloc, NULL);
    uvrpc_set_default_allocator(custom);
    
    char* str4 = (char*)uvrpc_alloc(128);
    strcpy(str4, "Hello from custom allocator!");
    printf("   Allocated: %s\n", str4);
    uvrpc_free(str4);
    
    performance_test("Custom", 10000);  /* Less iterations due to printf overhead */
    
    printf("\n   Custom stats: allocs=%zu, frees=%zu\n",
           custom_alloc_count, custom_free_count);

    printf("\n============================\n");
    printf("Demo completed successfully!\n");

    return 0;
}
