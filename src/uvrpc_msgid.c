/**
 * UVRPC Message ID Encoding Implementation
 */

#include "uvrpc_msgid.h"
#include <stdlib.h>

/* 创建消息ID上下文 */
uvrpc_msgid_ctx_t* uvrpc_msgid_ctx_new(void) {
    uvrpc_msgid_ctx_t* ctx = (uvrpc_msgid_ctx_t*)calloc(1, sizeof(uvrpc_msgid_ctx_t));
    if (!ctx) return NULL;
    
    ctx->next_seq = 1;
    
    return ctx;
}

/* 释放消息ID上下文 */
void uvrpc_msgid_ctx_free(uvrpc_msgid_ctx_t* ctx) {
    if (ctx) {
        free(ctx);
    }
}

/* 生成下一个消息ID */
uint32_t uvrpc_msgid_next(uvrpc_msgid_ctx_t* ctx) {
    if (!ctx) return 0;
    
    return ctx->next_seq++;
}