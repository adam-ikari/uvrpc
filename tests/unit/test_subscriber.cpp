#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCSubscriberTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_subscriber_t* subscriber;
    
    void SetUp() override {
        uv_loop_init(&loop);
        config = uvrpc_config_new();
        subscriber = nullptr;
    }
    
    void TearDown() override {
        if (subscriber) {
            uvrpc_subscriber_free(subscriber);
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

TEST_F(UVRPCSubscriberTest, CreateSubscriber) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    subscriber = uvrpc_subscriber_create(config);
    ASSERT_NE(subscriber, nullptr);
}

TEST_F(UVRPCSubscriberTest, CreateSubscriberWithNullConfig) {
    subscriber = uvrpc_subscriber_create(nullptr);
    EXPECT_EQ(subscriber, nullptr);
}

TEST_F(UVRPCSubscriberTest, CreateSubscriberWithNullLoop) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    subscriber = uvrpc_subscriber_create(config);
    // Should handle null loop gracefully
}

TEST_F(UVRPCSubscriberTest, CreateSubscriberWithDifferentTransports) {
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    // Test TCP transport
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    subscriber = uvrpc_subscriber_create(config);
    ASSERT_NE(subscriber, nullptr);
    uvrpc_subscriber_free(subscriber);
    
    // Test IPC transport
    subscriber = nullptr;
    uvrpc_config_set_address(config, "ipc:///tmp/test.ipc");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    subscriber = uvrpc_subscriber_create(config);
    ASSERT_NE(subscriber, nullptr);
    uvrpc_subscriber_free(subscriber);
    
    // Test INPROC transport
    subscriber = nullptr;
    uvrpc_config_set_address(config, "inproc://test");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    subscriber = uvrpc_subscriber_create(config);
    ASSERT_NE(subscriber, nullptr);
}

TEST_F(UVRPCSubscriberTest, CreateSubscriberWithInvalidCommType) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);  // Wrong type for subscriber
    
    subscriber = uvrpc_subscriber_create(config);
    // Should still create but may not work correctly
}

TEST_F(UVRPCSubscriberTest, FreeNullSubscriber) {
    uvrpc_subscriber_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCSubscriberTest, MultipleSubscriberCreation) {
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    const int count = 5;
    uvrpc_subscriber_t* subscribers[count];
    
    for (int i = 0; i < count; i++) {
        subscribers[i] = uvrpc_subscriber_create(config);
        ASSERT_NE(subscribers[i], nullptr);
    }
    
    for (int i = 0; i < count; i++) {
        uvrpc_subscriber_free(subscribers[i]);
    }
}