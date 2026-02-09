#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "../include/uvrpc.h"
#include "../examples/echo_generated.h"
#include "../examples/rpc_generated.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 测试统计 */
typedef struct {
    int total;
    int passed;
    int failed;
} test_stats_t;

/* 测试结果 */
#define TEST_PASS(stats) ((stats)->passed++)
#define TEST_FAIL(stats, msg) do { \
    fprintf(stderr, "  FAILED: %s\n", msg); \
    (stats)->failed++; \
} while(0)

#define ASSERT_TRUE(stats, cond, msg) do { \
    if (!(cond)) { \
        TEST_FAIL(stats, msg); \
        return; \
    } \
    TEST_PASS(stats); \
} while(0)

#define ASSERT_EQ(stats, a, b, msg) do { \
    if ((a) != (b)) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "%s (expected %d, got %d)", msg, (int)(b), (int)(a)); \
        TEST_FAIL(stats, buf); \
        return; \
    } \
    TEST_PASS(stats); \
} while(0)

#define ASSERT_STR_EQ(stats, a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "%s (expected '%s', got '%s')", msg, (b), (a)); \
        TEST_FAIL(stats, buf); \
        return; \
    } \
    TEST_PASS(stats); \
} while(0)

/* 测试回调上下文 */
typedef struct {
    test_stats_t* stats;
    const char* test_name;
    int received_response;
    int expected_status;
    const char* expected_message;
} test_callback_ctx_t;

/* 打印测试结果 */
void print_test_summary(test_stats_t* stats) {
    printf("\n=== Test Summary ===\n");
    printf("Total: %d\n", stats->total);
    printf("Passed: %d\n", stats->passed);
    printf("Failed: %d\n", stats->failed);
    printf("==================\n");
    
    if (stats->failed > 0) {
        printf("\n❌ TESTS FAILED\n");
        exit(1);
    } else {
        printf("\n✅ ALL TESTS PASSED\n");
    }
}

/* 创建 Echo 请求 */
uint8_t* create_echo_request(const char* message, size_t* size) {
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        return NULL;
    }
    
    flatbuffers_string_ref_t msg_ref = flatbuffers_string_create(builder, message);
    int64_t timestamp = 0;
    
    echo_EchoRequest_start(builder);
    echo_EchoRequest_message_add(builder, msg_ref);
    echo_EchoRequest_timestamp_add(builder, timestamp);
    flatbuffers_uint8_vec_ref_t request_ref = echo_EchoRequest_end(builder);
    echo_EchoRequest_create_as_root(builder, request_ref);
    
    const uint8_t* data = flatbuffers_builder_get_data(builder, size);
    uint8_t* result = (uint8_t*)malloc(*size);
    if (result) {
        memcpy(result, data, *size);
    }
    
    flatbuffers_builder_clear(builder);
    return result;
}

/* 解析 Echo 响应 */
int parse_echo_response(const uint8_t* data, size_t size, char* reply, size_t reply_size) {
    flatbuffers_t* fb = flatbuffers_init(data, size);
    if (!fb) {
        return -1;
    }
    
    echo_EchoResponse_table_t response;
    if (!echo_EchoResponse_as_root(fb)) {
        flatbuffers_clear(fb);
        return -1;
    }
    echo_EchoResponse_init(&response, fb);
    
    const char* reply_str = echo_EchoResponse_reply(&response);
    if (reply_str && reply_size > 0) {
        strncpy(reply, reply_str, reply_size - 1);
        reply[reply_size - 1] = '\0';
    }
    
    flatbuffers_clear(fb);
    return 0;
}

#endif /* TEST_COMMON_H */
