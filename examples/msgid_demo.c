/**
 * UVRPC Message ID Demo
 * 演示简单的32位顺序消息ID生成
 */

#include "../include/uvrpc.h"
#include "../src/uvrpc_msgid.h"
#include <stdio.h>
#include <stdlib.h>

static void print_msgid_details(const char* prefix, uint32_t msgid) {
    printf("%s: 0x%08X (%u)\n", prefix, msgid, msgid);
}

int main(int argc, char** argv) {
    printf("UVRPC Message ID Demo\n");
    printf("======================\n\n");
    
    /* 测试1: 简单递增 */
    printf("1. Simple Increment (单连接)\n");
    uvrpc_msgid_ctx_t* ctx1 = uvrpc_msgid_ctx_new();
    for (int i = 0; i < 5; i++) {
        uint32_t msgid = uvrpc_msgid_next(ctx1);
        print_msgid_details("  MsgID", msgid);
    }
    uvrpc_msgid_ctx_free(ctx1);
    
    /* 测试2: 多客户端独立序列 */
    printf("\n2. Independent Sequence (多客户端)\n");
    uvrpc_msgid_ctx_t* ctx2a = uvrpc_msgid_ctx_new();
    uvrpc_msgid_ctx_t* ctx2b = uvrpc_msgid_ctx_new();    
    printf("  Client A:\n");
    for (int i = 0; i < 3; i++) {
        uint32_t msgid = uvrpc_msgid_next(ctx2a);
        print_msgid_details("    MsgID", msgid);
    }
    
    printf("  Client B:\n");
    for (int i = 0; i < 3; i++) {
        uint32_t msgid = uvrpc_msgid_next(ctx2b);
        print_msgid_details("    MsgID", msgid);
    }
    
    uvrpc_msgid_ctx_free(ctx2a);
    uvrpc_msgid_ctx_free(ctx2b);
    
    /* 性能测试 */
    printf("\n3. Performance Test (100000 IDs)\n");
    uvrpc_msgid_ctx_t* perf_ctx = uvrpc_msgid_ctx_new();
    for (int i = 0; i < 100000; i++) {
        uvrpc_msgid_next(perf_ctx);
    }
    uint32_t last_id = uvrpc_msgid_next(perf_ctx);
    print_msgid_details("  Last ID", last_id);
    uvrpc_msgid_ctx_free(perf_ctx);
    
    /* 说明 */
    printf("\n=== Notes ===\n");
    printf("UVRPC uses simple 32-bit sequential message IDs:\n");
    printf("  - Each client has independent msgid context\n");
    printf("  - Starts from 1 and increments monotonically\n");
    printf("  - Sufficient for most use cases (4.3 billion IDs)\n");
    printf("  - High performance: O(1) generation\n");
    printf("  - No conflicts between clients\n");
    
    return 0;
}