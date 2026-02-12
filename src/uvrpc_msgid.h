/**
 * UVRPC Message ID Encoding (32-bit)
 * 
 * 设计原则：
 * 1. 32位整数，紧凑高效
 * 2. 简单递增，无需复杂编码
 * 3. 单客户端环境保证唯一性
 * 4. 无锁设计，零全局变量
 */

#ifndef UVRPC_MSGID_H
#define UVRPC_MSGID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 消息ID上下文 */
typedef struct {
    uint32_t next_seq;     /* 下一个序列号 */
} uvrpc_msgid_ctx_t;

/* 创建消息ID上下文 */
uvrpc_msgid_ctx_t* uvrpc_msgid_ctx_new(void);
void uvrpc_msgid_ctx_free(uvrpc_msgid_ctx_t* ctx);

/* 生成下一个消息ID (32位) */
uint32_t uvrpc_msgid_next(uvrpc_msgid_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_MSGID_H */