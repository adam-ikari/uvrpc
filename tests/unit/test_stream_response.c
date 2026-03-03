/**
 * Unit Test for Stream Response API
 * Tests the new streaming response functionality with type=2 (ResponseMore) and type=1 (Response)
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

/* Test helper: create mock response */
static uvrpc_response_t create_mock_response(int frame_type, const char* data) {
    uvrpc_response_t resp;
    resp.status = UVRPC_OK;
    resp.msgid = 1;
    resp.error_code = 0;
    resp.error_message = NULL;
    resp.frame_type = frame_type;
    
    if (data) {
        resp.result = (uint8_t*)strdup(data);
        resp.result_size = strlen(data) + 1;
    } else {
        resp.result = NULL;
        resp.result_size = 0;
    }
    
    return resp;
}

/* Test 1: Check stream end detection (type=1) */
static void test_stream_end_detection(void) {
    printf("\n=== Test 1: Stream End Detection ===\n");
    
    uvrpc_response_t resp = create_mock_response(1, "final chunk");
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp) == 1, 
                "Should detect Response (type=1) as stream end");
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp) == 0,
                "Should not detect Response (type=1) as more data");
    
    if (resp.result) free(resp.result);
}

/* Test 2: Check stream more detection (type=2) */
static void test_stream_more_detection(void) {
    printf("\n=== Test 2: Stream More Detection ===\n");
    
    uvrpc_response_t resp = create_mock_response(2, "intermediate chunk");
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp) == 0,
                "Should not detect ResponseMore (type=2) as stream end");
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp) == 1,
                "Should detect ResponseMore (type=2) as more data");
    
    if (resp.result) free(resp.result);
}

/* Test 3: Check frame type field */
static void test_frame_type_field(void) {
    printf("\n=== Test 3: Frame Type Field ===\n");
    
    uvrpc_response_t resp1 = create_mock_response(0, "request");
    uvrpc_response_t resp2 = create_mock_response(1, "response");
    uvrpc_response_t resp3 = create_mock_response(2, "response_more");
    
    TEST_ASSERT(resp1.frame_type == 0, "Request frame type should be 0");
    TEST_ASSERT(resp2.frame_type == 1, "Response frame type should be 1");
    TEST_ASSERT(resp3.frame_type == 2, "ResponseMore frame type should be 2");
    
    if (resp1.result) free(resp1.result);
    if (resp2.result) free(resp2.result);
    if (resp3.result) free(resp3.result);
}

/* Test 4: Check NULL response handling */
static void test_null_response_handling(void) {
    printf("\n=== Test 4: NULL Response Handling ===\n");
    
    TEST_ASSERT(uvrpc_response_is_stream_end(NULL) == 0,
                "Should handle NULL response safely for stream end check");
    TEST_ASSERT(uvrpc_response_is_stream_more(NULL) == 0,
                "Should handle NULL response safely for stream more check");
}

/* Test 5: Check empty response handling */
static void test_empty_response_handling(void) {
    printf("\n=== Test 5: Empty Response Handling ===\n");
    
    uvrpc_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.frame_type = 1;
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp) == 1,
                "Should detect empty response with type=1 as stream end");
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp) == 0,
                "Should not detect empty response with type=1 as more data");
}

/* Test 6: Check invalid frame type */
static void test_invalid_frame_type(void) {
    printf("\n=== Test 6: Invalid Frame Type ===\n");
    
    uvrpc_response_t resp = create_mock_response(99, "unknown");
    
    TEST_ASSERT(uvrpc_response_is_stream_end(&resp) == 0,
                "Should not detect invalid frame type as stream end");
    TEST_ASSERT(uvrpc_response_is_stream_more(&resp) == 0,
                "Should not detect invalid frame type as more data");
    
    if (resp.result) free(resp.result);
}

int main(void) {
    printf("========================================\n");
    printf("UVRPC Stream Response Unit Tests\n");
    printf("========================================\n");
    
    test_stream_end_detection();
    test_stream_more_detection();
    test_frame_type_field();
    test_null_response_handling();
    test_empty_response_handling();
    test_invalid_frame_type();
    
    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}