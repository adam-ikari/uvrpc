/**
 * UVRPC FlatBuffers Unit Tests
 * Tests using FlatCC generated code for RpcFrame serialization
 */

#include <gtest/gtest.h>
#include "rpc_builder.h"
#include "rpc_reader.h"
#include "../include/uvrpc.h"
#include <string.h>
#include <stdlib.h>

class FlatBuffersTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize FlatCC builder
        flatcc_builder_init(&builder);
    }
    
    void TearDown() override {
        flatcc_builder_reset(&builder);
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
    }
    
    flatcc_builder_t builder;
    void* buffer;
    size_t buffer_size;
};

TEST_F(FlatBuffersTest, EncodeRequest) {
    const char* method = "test_method";
    uint32_t msgid = 12345;
    const uint8_t params[] = {0x01, 0x02, 0x03, 0x04};
    size_t params_size = sizeof(params);
    
    // Create string and vector
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t params_ref = flatbuffers_uint8_vec_create(&builder, params, params_size);
    
    // Build request frame
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_REQUEST);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_params_add(&builder, params_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    ASSERT_GT(buffer_size, 0);
    
    // Verify frame
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    
    EXPECT_EQ(uvrpc_RpcFrame_type(frame), uvrpc_FrameType_REQUEST);
    EXPECT_EQ(uvrpc_RpcFrame_msgid(frame), msgid);
    
    flatbuffers_string_t decoded_method = uvrpc_RpcFrame_method(frame);
    ASSERT_NE(decoded_method, nullptr);
    EXPECT_STREQ(decoded_method, method);
    
    flatbuffers_uint8_vec_t decoded_params = uvrpc_RpcFrame_params(frame);
    ASSERT_NE(decoded_params, nullptr);
    EXPECT_EQ(flatbuffers_uint8_vec_len(decoded_params), params_size);
}

TEST_F(FlatBuffersTest, EncodeResponse) {
    uint32_t msgid = 54321;
    int32_t error_code = 0;
    const uint8_t result[] = {0x10, 0x20, 0x30};
    size_t result_size = sizeof(result);
    
    // Create vector
    flatbuffers_uint8_vec_ref_t result_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    
    // Build response frame
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_RESPONSE);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_error_code_add(&builder, error_code);
    uvrpc_RpcFrame_params_add(&builder, result_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    ASSERT_GT(buffer_size, 0);
    
    // Verify frame
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    
    EXPECT_EQ(uvrpc_RpcFrame_type(frame), uvrpc_FrameType_RESPONSE);
    EXPECT_EQ(uvrpc_RpcFrame_msgid(frame), msgid);
    EXPECT_EQ(uvrpc_RpcFrame_error_code(frame), error_code);
}

TEST_F(FlatBuffersTest, EncodeResponseWithError) {
    uint32_t msgid = 99999;
    int32_t error_code = UVRPC_ERROR_INVALID_PARAM;
    const uint8_t result[] = {0xFF};
    size_t result_size = sizeof(result);
    
    // Create vector
    flatbuffers_uint8_vec_ref_t result_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    
    // Build response frame with error
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_RESPONSE);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_error_code_add(&builder, error_code);
    uvrpc_RpcFrame_params_add(&builder, result_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    
    // Verify error code
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(uvrpc_RpcFrame_error_code(frame), error_code);
}

TEST_F(FlatBuffersTest, EncodeNotification) {
    const char* method = "notify_event";
    uint32_t msgid = 0; // Notifications may not need msgid
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    size_t data_size = sizeof(data);
    
    // Create string and vector
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
    
    // Build notification frame
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_NOTIFICATION);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_params_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    
    // Verify frame
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(uvrpc_RpcFrame_type(frame), uvrpc_FrameType_NOTIFICATION);
}

TEST_F(FlatBuffersTest, LargePayload) {
    const char* method = "large_payload_test";
    uint32_t msgid = 1000;
    
    // Create large payload (100KB)
    size_t large_size = 100 * 1024;
    uint8_t* large_data = (uint8_t*)malloc(large_size);
    ASSERT_NE(large_data, nullptr);
    
    // Fill with pattern
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (uint8_t)(i % 256);
    }
    
    // Create string and vector
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t data_ref = flatbuffers_uint8_vec_create(&builder, large_data, large_size);
    
    // Build frame
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_REQUEST);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_params_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    
    // Verify frame
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    
    flatbuffers_uint8_vec_t decoded_data = uvrpc_RpcFrame_params(frame);
    ASSERT_NE(decoded_data, nullptr);
    EXPECT_EQ(flatbuffers_uint8_vec_len(decoded_data), large_size);
    
    // Verify data integrity
    for (size_t i = 0; i < large_size; i++) {
        EXPECT_EQ(flatbuffers_uint8_vec_at(decoded_data, i), (uint8_t)(i % 256));
    }
    
    free(large_data);
}

TEST_F(FlatBuffersTest, EmptyParams) {
    const char* method = "empty_test";
    uint32_t msgid = 777;
    
    // Create string with no params
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    
    // Build frame
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_REQUEST);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    // Finalize
    buffer = flatcc_builder_finalize_buffer(&builder, &buffer_size);
    ASSERT_NE(buffer, nullptr);
    
    // Verify frame
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    ASSERT_NE(frame, nullptr);
    
    flatbuffers_uint8_vec_t params = uvrpc_RpcFrame_params(frame);
    EXPECT_EQ(params, nullptr); // NULL pointer for empty vector
}