/**
 * UVRPC Message ID Encoding Demo
 * 演示不同的消息ID编码方式
 */

#include "../include/uvrpc.h"
#include "../src/uvrpc_msgid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* 打印消息ID详情 */
void print_msgid_details(const char* label, uint64_t msgid, uvrpc_msgid_type_t type) {
    printf("%s: 0x%016" PRIu64 " (%" PRIu64 ")\n", label, msgid, msgid);
    
    uint32_t client_id;
    uint64_t seq;
    uvrpc_msgid_parse(msgid, &client_id, &seq);
    
    printf("  Client ID: %u\n", client_id);
    printf("  Sequence:  %" PRIu64 "\n", seq);
    
    switch (type) {
        case UVRPC_MSGID_SIMPLE:
            printf("  Type: Simple (64-bit sequence)\n");
            break;
        case UVRPC_MSGID_CLIENT_SEQ:
            printf("  Type: Client+Seq (16+48 bits)\n");
            break;
        case UVRPC_MSGID_TIMESTAMP:
            printf("  Type: Timestamp+Client+Seq (32+16+16 bits)\n");
            printf("  Timestamp: %" PRIu32 " (Unix time)\n", (uint32_t)(msgid >> 32));
            break;
        case UVRPC_MSGID_RANDOM:
            printf("  Type: Random (pseudo-random)\n");
            break;
    }
    printf("\n");
}

int main(int argc, char** argv) {
    printf("UVRPC Message ID Encoding Demo\n");
    printf("===============================\n\n");
    
    /* 测试1: 简单递增 */
    printf("1. Simple Increment (单连接)\n");
    uvrpc_msgid_ctx_t* ctx1 = uvrpc_msgid_ctx_new(UVRPC_MSGID_SIMPLE, 0);
    for (int i = 0; i < 5; i++) {
        uint64_t msgid = uvrpc_msgid_next(ctx1);
        print_msgid_details("  MsgID", msgid, UVRPC_MSGID_SIMPLE);
    }
    uvrpc_msgid_ctx_free(ctx1);
    
    /* 测试2: 客户端ID + 序列号 */
    printf("2. Client ID + Sequence (多客户端)\n");
    uvrpc_msgid_ctx_t* ctx2a = uvrpc_msgid_ctx_new(UVRPC_MSGID_CLIENT_SEQ, 0x1234);
    uvrpc_msgid_ctx_t* ctx2b = uvrpc_msgid_ctx_new(UVRPC_MSGID_CLIENT_SEQ, 0x5678);
    
    printf("  Client A (ID: 0x1234):\n");
    for (int i = 0; i < 3; i++) {
        uint64_t msgid = uvrpc_msgid_next(ctx2a);
        print_msgid_details("    MsgID", msgid, UVRPC_MSGID_CLIENT_SEQ);
    }
    
    printf("  Client B (ID: 0x5678):\n");
    for (int i = 0; i < 3; i++) {
        uint64_t msgid = uvrpc_msgid_next(ctx2b);
        print_msgid_details("    MsgID", msgid, UVRPC_MSGID_CLIENT_SEQ);
    }
    
    uvrpc_msgid_ctx_free(ctx2a);
    uvrpc_msgid_ctx_free(ctx2b);
    
    /* 测试3: 时间戳 + 客户端ID + 序列号 */
    printf("3. Timestamp + Client + Sequence (带时间信息)\n");
    uvrpc_msgid_ctx_t* ctx3 = uvrpc_msgid_ctx_new(UVRPC_MSGID_TIMESTAMP, 0xABCD);
    for (int i = 0; i < 5; i++) {
        uint64_t msgid = uvrpc_msgid_next(ctx3);
        print_msgid_details("  MsgID", msgid, UVRPC_MSGID_TIMESTAMP);
    }
    uvrpc_msgid_ctx_free(ctx3);
    
    /* 测试4: 随机生成 */
    printf("4. Random (伪随机)\n");
    uvrpc_msgid_ctx_t* ctx4 = uvrpc_msgid_ctx_new(UVRPC_MSGID_RANDOM, 0);
    for (int i = 0; i < 5; i++) {
        uint64_t msgid = uvrpc_msgid_next(ctx4);
        print_msgid_details("  MsgID", msgid, UVRPC_MSGID_RANDOM);
    }
    uvrpc_msgid_ctx_free(ctx4);
    
    /* 性能测试 */
    printf("5. Performance Test (1,000,000 IDs per method)\n");
    uvrpc_msgid_ctx_t* perf_ctx = uvrpc_msgid_ctx_new(UVRPC_MSGID_CLIENT_SEQ, 0x1000);
    
    printf("  Generating 1,000,000 message IDs...\n");
    uint64_t start = __builtin_ia32_rdtsc();
    
    for (int i = 0; i < 1000000; i++) {
        uvrpc_msgid_next(perf_ctx);
    }
    
    uint64_t end = __builtin_ia32_rdtsc();
    double elapsed_ms = (end - start) / (2.4e6);  /* Assuming 2.4 GHz CPU */
    
    printf("  Completed in %.2f ms\n", elapsed_ms);
    printf("  Throughput: %.0f IDs/ms\n", 1000000.0 / elapsed_ms);
    
    uvrpc_msgid_ctx_free(perf_ctx);
    
    /* 使用建议 */
    printf("\n===============================\n");
    printf("Usage Recommendations:\n");
    printf("- Simple:          Single connection, simple RPC\n");
    printf("- Client+Seq:      Multi-client environments (recommended)\n");
    printf("- Timestamp:       When time-based ordering needed\n");
    printf("- Random:          Distributed systems with low collision requirements\n");
    
    return 0;
}
