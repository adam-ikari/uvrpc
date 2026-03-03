/**
 * Simplified Boundary Value Tests for UVRPC
 * Focus on critical edge cases
 */

#include <uvrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("[FAIL] %s\n", message); \
            tests_failed++; \
            return; \
        } \
        tests_passed++; \
        printf("[PASS] %s\n", message); \
    } while(0)

/* Test 1: Stream end detection with type=1 */
static void test_stream_end_type_1(void) {
    printf("\n=== Test 1: Stream End Detection (type=1) ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 1;  /* Response (last) */
    resp.result = (uint8_t*)"test data";
    resp.result_size = 9;
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "type=1 should be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "type=1 should not be stream more");
}

/* Test 2: Stream more detection with type=2 */
static void test_stream_more_type_2(void) {
    printf("\n=== Test 2: Stream More Detection (type=2) ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 2;  /* ResponseMore */
    resp.result = (uint8_t*)"test data";
    resp.result_size = 9;
    
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp), "type=2 should be stream more");
    TEST_ASSERT(!uvrpc_response_is_stream_end(&resp), "type=2 should not be stream end");
}

/* Test 3: Invalid frame type */
static void test_invalid_frame_type(void) {
    printf("\n=== Test 3: Invalid Frame Type ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 99;  /* Invalid type */
    resp.result = (uint8_t*)"test data";
    resp.result_size = 9;
    
    TEST_ASSERT(!uvrpc_response_is_stream_end(&resp), "Invalid type should not be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "Invalid type should not be stream more");
}

/* Test 4: NULL response */
static void test_null_response(void) {
    printf("\n=== Test 4: NULL Response ===\n");
    
    TEST_ASSERT(!uvrpc_response_is_stream_end(NULL), "NULL response should not be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(NULL), "NULL response should not be stream more");
}

/* Test 5: Empty response (zero size) */
static void test_empty_response(void) {
    printf("\n=== Test 5: Empty Response ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 1;
    resp.result = NULL;
    resp.result_size = 0;
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Empty response with type=1 should be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "Empty response should not be stream more");
}

/* Test 6: Single byte response */
static void test_single_byte_response(void) {
    printf("\n=== Test 6: Single Byte Response ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 1;
    resp.result = (uint8_t*)"X";
    resp.result_size = 1;
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Single byte response should be stream end");
}

/* Test 7: Large response (10KB) */
static void test_large_response(void) {
    printf("\n=== Test 7: Large Response (10KB) ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 1;
    
    /* Allocate 10KB */
    size_t large_size = 10 * 1024;
    uint8_t* large_data = (uint8_t*)malloc(large_size);
    TEST_ASSERT(large_data != NULL, "Should allocate 10KB buffer");
    
    resp.result = large_data;
    resp.result_size = large_size;
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Large response should be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "Large response should not be stream more");
    
    free(large_data);
}

/* Test 8: Error response */
static void test_error_response(void) {
    printf("\n=== Test 8: Error Response ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_ERROR_INVALID_PARAM;
    resp.msgid = 1;
    resp.frame_type = 1;
    resp.result = NULL;
    resp.result_size = 0;
    resp.error_code = -2;
    resp.error_message = "Invalid parameter";
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Error response with type=1 should be stream end");
}

/* Test 9: Boundary values for frame_type */
static void test_frame_type_boundaries(void) {
    printf("\n=== Test 9: Frame Type Boundaries ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.result = NULL;
    resp.result_size = 0;
    
    /* Test type=0 (Request) */
    resp.frame_type = 0;
    TEST_ASSERT(!uvrpc_response_is_stream_end(&resp), "type=0 should not be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "type=0 should not be stream more");
    
    /* Test type=1 (Response) */
    resp.frame_type = 1;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "type=1 should be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "type=1 should not be stream more");
    
    /* Test type=2 (ResponseMore) */
    resp.frame_type = 2;
    TEST_ASSERT(!uvrpc_response_is_stream_end(&resp), "type=2 should not be stream end");
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp), "type=2 should be stream more");
    
    /* Test type=255 (max byte) */
    resp.frame_type = 255;
    TEST_ASSERT(!uvrpc_response_is_stream_end(&resp), "type=255 should not be stream end");
    TEST_ASSERT(!uvrpc_response_is_stream_more(&resp), "type=255 should not be stream more");
}

/* Test 10: Boundary values for result_size */
static void test_result_size_boundaries(void) {
    printf("\n=== Test 10: Result Size Boundaries ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.frame_type = 1;
    resp.result = (uint8_t*)"test";
    resp.result_size = 4;
    
    /* Test size=0 */
    resp.result_size = 0;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Size 0 should be stream end");
    
    /* Test size=1 */
    resp.result_size = 1;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Size 1 should be stream end");
    
    /* Test size=255 */
    resp.result_size = 255;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Size 255 should be stream end");
    
    /* Test size=65535 (max uint16) */
    resp.result_size = 65535;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Size 65535 should be stream end");
}

/* Test 11: Boundary values for msgid */
static void test_msgid_boundaries(void) {
    printf("\n=== Test 11: Message ID Boundaries ===\n");
    
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.frame_type = 1;
    resp.result = NULL;
    resp.result_size = 0;
    
    /* Test msgid=0 */
    resp.msgid = 0;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "msgid=0 should be stream end");
    
    /* Test msgid=1 */
    resp.msgid = 1;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "msgid=1 should be stream end");
    
    /* Test msgid=UINT32_MAX */
    resp.msgid = UINT32_MAX;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "msgid=UINT32_MAX should be stream end");
}

/* Test 12: Status code boundaries */
static void test_status_boundaries(void) {
    printf("\n=== Test 12: Status Code Boundaries ===\n");
    
    uvrpc_response_t resp;
    resp.msgid = 1;
    resp.frame_type = 1;
    resp.result = NULL;
    resp.result_size = 0;
    
    /* Test UVRPC_OK (0) */
    resp.status = UVRPC_OK;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "UVRPC_OK should be stream end");
    
    /* Test UVRPC_ERROR (-1) */
    resp.status = UVRPC_ERROR;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "UVRPC_ERROR should be stream end");
    
    /* Test negative status */
    resp.status = -100;
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp), "Negative status should be stream end");
}

/* Main test runner */
int main(int argc, char** argv) {
    printf("=========================================\n");
    printf("UVRPC Simplified Boundary Value Tests\n");
    printf("=========================================\n");
    
    (void)argc;
    (void)argv;
    
    /* Run all tests */
    test_stream_end_type_1();
    test_stream_more_type_2();
    test_invalid_frame_type();
    test_null_response();
    test_empty_response();
    test_single_byte_response();
    test_large_response();
    test_error_response();
    test_frame_type_boundaries();
    test_result_size_boundaries();
    test_msgid_boundaries();
    test_status_boundaries();
    
    /* Print summary */
    printf("\n=========================================\n");
    printf("Test Summary\n");
    printf("=========================================\n");
    printf("Total tests: %d\n", tests_passed + tests_failed);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("=========================================\n");
    
    return (tests_failed == 0) ? 0 : 1;
}
