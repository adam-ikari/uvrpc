#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCPublisherTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_publisher_t* publisher;
    
    void SetUp() override {
        uv_loop_init(&loop);
        config = uvrpc_config_new();
        publisher = nullptr;
    }
    
    void TearDown() override {
        if (publisher) {
            uvrpc_publisher_free(publisher);
        }
        if (config) {
            uvrpc_config_free(config);
        }
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

TEST_F(UVRPCPublisherTest, CreatePublisher) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    publisher = uvrpc_publisher_create(config);
    ASSERT_NE(publisher, nullptr);
}

TEST_F(UVRPCPublisherTest, CreatePublisherWithNullConfig) {
    publisher = uvrpc_publisher_create(nullptr);
    EXPECT_EQ(publisher, nullptr);
}

TEST_F(UVRPCPublisherTest, CreatePublisherWithNullLoop) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    publisher = uvrpc_publisher_create(config);
    // Should handle null loop gracefully
}

TEST_F(UVRPCPublisherTest, CreatePublisherWithDifferentTransports) {
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    // Test TCP transport
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    publisher = uvrpc_publisher_create(config);
    ASSERT_NE(publisher, nullptr);
    uvrpc_publisher_free(publisher);
    
    // Test IPC transport
    publisher = nullptr;
    uvrpc_config_set_address(config, "ipc:///tmp/test.ipc");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    publisher = uvrpc_publisher_create(config);
    ASSERT_NE(publisher, nullptr);
    uvrpc_publisher_free(publisher);
    
    // Test INPROC transport
    publisher = nullptr;
    uvrpc_config_set_address(config, "inproc://test");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    publisher = uvrpc_publisher_create(config);
    ASSERT_NE(publisher, nullptr);
}

TEST_F(UVRPCPublisherTest, CreatePublisherWithInvalidCommType) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);  // Wrong type for publisher
    
    publisher = uvrpc_publisher_create(config);
    // Should still create but may not work correctly
}

TEST_F(UVRPCPublisherTest, FreeNullPublisher) {
    uvrpc_publisher_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCPublisherTest, MultiplePublisherCreation) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    const int count = 5;
    uvrpc_publisher_t* publishers[count];
    
    for (int i = 0; i < count; i++) {
        publishers[i] = uvrpc_publisher_create(config);
        ASSERT_NE(publishers[i], nullptr);
    }
    
    for (int i = 0; i < count; i++) {
        uvrpc_publisher_free(publishers[i]);
    }
}