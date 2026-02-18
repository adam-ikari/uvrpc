#include <gtest/gtest.h>
#include "uvbus.h"

class UVBUSTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    
    void SetUp() override {
        uv_loop_init(&loop);
    }
    
    void TearDown() override {
        // Run loop to cleanup
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

TEST_F(UVBUSTest, CreateServerConfig) {
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NE(config, nullptr);
    uvbus_config_free(config);
}

TEST_F(UVBUSTest, FreeNullServerConfig) {
    uvbus_config_free(nullptr);
    // Should not crash
}

TEST_F(UVBUSTest, CreateServerConfigAndSetOptions) {
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NE(config, nullptr);
    
    // Test setting config options
    uvbus_config_set_address(config, "tcp://127.0.0.1:5555");
    uvbus_config_set_loop(config, &loop);
    
    uvbus_config_free(config);
}

TEST_F(UVBUSTest, SetNullAddress) {
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NE(config, nullptr);
    
    uvbus_config_set_address(config, nullptr);
    
    uvbus_config_free(config);
}

TEST_F(UVBUSTest, SetEmptyAddress) {
    uvbus_config_t* config = uvbus_config_new();
    ASSERT_NE(config, nullptr);
    
    uvbus_config_set_address(config, "");
    
    uvbus_config_free(config);
}