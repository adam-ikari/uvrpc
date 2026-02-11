#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int echo_handler(void* ctx,
                         const uint8_t* request_data,
                         size_t request_size,
                         uint8_t** response_data,
                         size_t* response_size) {
    (void)ctx;
    *response_data = (uint8_t*)malloc(request_size);
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    return 0;
}

void example_low_latency() {
    printf("=== 示例1: 低延迟模式 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:7001", UVRPC_MODE_ROUTER_DEALER);
    
    /* 应用低延迟预设配置 */
    uvrpc_server_apply_performance_mode(server, UVRPC_PERF_LOW_LATENCY);
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    printf("服务器已启动 (低延迟模式)\n");
    printf("  HWM: 100\n");
    printf("  TCP Buffer: 64KB\n");
    printf("  IO Threads: 1\n\n");
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
}

void example_balanced() {
    printf("=== 示例2: 平衡模式 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:7002", UVRPC_MODE_ROUTER_DEALER);
    
    /* 应用平衡预设配置 */
    uvrpc_server_apply_performance_mode(server, UVRPC_PERF_BALANCED);
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    printf("服务器已启动 (平衡模式)\n");
    printf("  HWM: 1000\n");
    printf("  TCP Buffer: 256KB\n");
    printf("  IO Threads: 2\n\n");
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
}

void example_high_throughput() {
    printf("=== 示例3: 高吞吐模式 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:7003", UVRPC_MODE_ROUTER_DEALER);
    
    /* 应用高吞吐预设配置 */
    uvrpc_server_apply_performance_mode(server, UVRPC_PERF_HIGH_THROUGHPUT);
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    printf("服务器已启动 (高吞吐模式)\n");
    printf("  HWM: 10000\n");
    printf("  TCP Buffer: 1MB\n");
    printf("  IO Threads: 4\n\n");
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
}

void example_custom_config() {
    printf("=== 示例4: 自定义配置 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:7004", UVRPC_MODE_ROUTER_DEALER);
    
    /* 自定义配置 */
    uvrpc_server_set_hwm(server, 5000, 5000);
    uvrpc_server_set_tcp_buffer_size(server, 512 * 1024, 512 * 1024);
    uvrpc_server_set_io_threads(server, 3);
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    /* 获取统计信息 */
    int services_count;
    uvrpc_server_get_stats(server, &services_count);
    printf("服务器已启动 (自定义配置)\n");
    printf("  HWM: 5000\n");
    printf("  TCP Buffer: 512KB\n");
    printf("  IO Threads: 3\n");
    printf("  Services: %d\n\n", services_count);
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
}

void example_client_config() {
    printf("=== 示例5: 客户端配置 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://127.0.0.1:7005", UVRPC_MODE_ROUTER_DEALER);
    
    /* 应用高吞吐模式 */
    uvrpc_client_apply_performance_mode(client, UVRPC_PERF_HIGH_THROUGHPUT);
    
    /* 设置TCP keepalive */
    uvrpc_client_set_tcp_keepalive(client, 1, 60, 5, 10);
    
    /* 设置重连间隔 */
    uvrpc_client_set_reconnect_interval(client, 100, 5000);
    
    /* 设置Linger */
    uvrpc_client_set_linger(client, 0);
    
    printf("客户端已配置\n");
    printf("  性能模式: 高吞吐\n");
    printf("  TCP Keepalive: 启用 (60s空闲, 5次探测, 10s间隔)\n");
    printf("  重连间隔: 100ms-5000ms\n");
    printf("  Linger: 0ms (立即关闭)\n\n");
    
    uvrpc_client_free(client);
    uv_loop_close(&loop);
}

void example_get_stats() {
    printf("=== 示例6: 获取统计信息 ===\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:7006", UVRPC_MODE_ROUTER_DEALER);
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_register_service(server, "ping", echo_handler, NULL);
    uvrpc_server_start(server);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://127.0.0.1:7006", UVRPC_MODE_ROUTER_DEALER);
    uvrpc_client_connect(client);
    
    /* 获取服务器统计 */
    int server_services;
    uvrpc_server_get_stats(server, &server_services);
    printf("服务器统计:\n");
    printf("  Services: %d\n", server_services);
    
    /* 获取客户端统计 */
    int client_pending;
    uvrpc_client_get_stats(client, &client_pending);
    printf("\n客户端统计:\n");
    printf("  Pending Requests: %d\n", client_pending);
    
    /* 获取HWM配置 */
    int sndhwm, rcvhwm;
    uvrpc_server_get_hwm(server, &sndhwm, &rcvhwm);
    printf("\nHWM配置:\n");
    printf("  SNDHWM: %d\n", sndhwm);
    printf("  RCVHWM: %d\n", rcvhwm);
    
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uv_loop_close(&loop);
}

int main() {
    printf("========================================\n");
    printf("  UVRPC Layer 2 API 示例\n");
    printf("========================================\n\n");
    
    example_low_latency();
    example_balanced();
    example_high_throughput();
    example_custom_config();
    example_client_config();
    example_get_stats();
    
    printf("========================================\n");
    printf("  所有示例完成\n");
    printf("========================================\n");
    
    return 0;
}
