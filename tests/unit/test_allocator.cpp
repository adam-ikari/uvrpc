/**
 * UVRPC Allocator Unit Tests
 */

#include <gtest/gtest.h>
#include "../include/uvrpc_allocator.h"
#include <string.h>

class AllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize allocator with mimalloc (type = 1)
        uvrpc_allocator_init((uvrpc_allocator_type_t)1, NULL);
    }
    
    void TearDown() override {
        uvrpc_allocator_cleanup();
    }
};

TEST_F(AllocatorTest, AllocAndFree) {
    const size_t size = 1024;
    void* ptr = uvrpc_alloc(size);
    
    ASSERT_NE(ptr, nullptr);
    
    // Write to memory to ensure it's accessible
    memset(ptr, 0xAB, size);
    
    uvrpc_free(ptr);
}

TEST_F(AllocatorTest, Calloc) {
    const size_t count = 100;
    const size_t size = 16;
    
    void* ptr = uvrpc_calloc(count, size);
    ASSERT_NE(ptr, nullptr);
    
    // Verify memory is zeroed
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < count * size; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
    
    uvrpc_free(ptr);
}

TEST_F(AllocatorTest, Realloc) {
    const size_t initial_size = 100;
    const size_t new_size = 200;
    
    void* ptr = uvrpc_alloc(initial_size);
    ASSERT_NE(ptr, nullptr);
    
    // Write pattern
    memset(ptr, 0xCD, initial_size);
    
    // Reallocate
    void* new_ptr = uvrpc_realloc(ptr, new_size);
    ASSERT_NE(new_ptr, nullptr);
    
    // Verify old data is preserved
    uint8_t* bytes = (uint8_t*)new_ptr;
    for (size_t i = 0; i < initial_size; i++) {
        EXPECT_EQ(bytes[i], 0xCD);
    }
    
    uvrpc_free(new_ptr);
}

TEST_F(AllocatorTest, Strdup) {
    const char* original = "UVRPC Test String";
    char* copy = uvrpc_strdup(original);
    
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, original);
    EXPECT_NE(copy, original); // Different pointers
    
    uvrpc_free(copy);
}

TEST_F(AllocatorTest, AllocZero) {
    void* ptr = uvrpc_alloc(0);
    // mimalloc returns non-null pointer for size 0
    EXPECT_NE(ptr, nullptr);
    uvrpc_free(ptr);
}

TEST_F(AllocatorTest, LargeAllocation) {
    const size_t large_size = 10 * 1024 * 1024; // 10MB
    
    void* ptr = uvrpc_alloc(large_size);
    ASSERT_NE(ptr, nullptr);
    
    // Write to verify allocation works
    memset(ptr, 0x42, large_size);
    
    uvrpc_free(ptr);
}

TEST_F(AllocatorTest, MultipleAllocations) {
    const int num_allocs = 100;
    void* ptrs[num_allocs];
    
    // Allocate multiple blocks
    for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = uvrpc_alloc(1024);
        ASSERT_NE(ptrs[i], nullptr);
    }
    
    // Free all blocks
    for (int i = 0; i < num_allocs; i++) {
        uvrpc_free(ptrs[i]);
    }
}

TEST_F(AllocatorTest, GetAllocatorType) {
    uvrpc_allocator_type_t type = uvrpc_allocator_get_type();
    EXPECT_EQ(type, (uvrpc_allocator_type_t)1);
}

TEST_F(AllocatorTest, GetAllocatorName) {
    const char* name = uvrpc_allocator_get_name();
    ASSERT_NE(name, nullptr);
    EXPECT_GT(strlen(name), 0);
}