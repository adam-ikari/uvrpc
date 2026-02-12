/**
 * UVRPC ID Mapping Implementation
 */

#include "uvrpc_idmap.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>

/* 创建 ID 映射上下文 */
uvrpc_idmap_ctx_t* uvrpc_idmap_ctx_new(void) {
    uvrpc_idmap_ctx_t* ctx = calloc(1, sizeof(uvrpc_idmap_ctx_t));
    if (!ctx) return NULL;
    
    ctx->map = NULL;
    ctx->next_gateway_id = 1;
    
    return ctx;
}

/* 释放 ID 映射上下文 */
void uvrpc_idmap_ctx_free(uvrpc_idmap_ctx_t* ctx) {
    if (!ctx) return;
    
    /* 释放哈希表中的所有条目 */
    uvrpc_idmap_entry_t* entry, *tmp;
    HASH_ITER(hh, ctx->map, entry, tmp) {
        HASH_DEL(ctx->map, entry);
        free(entry);
    }
    
    free(ctx);
}

/* 转换：客户端 msgid -> 网关 msgid */
uint32_t uvrpc_idmap_to_gateway(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_raw, void* client_handle) {
    if (!ctx) return 0;
    
    /* 生成新的网关 msgid */
    uint32_t msgid_gateway = ctx->next_gateway_id++;
    
    /* 创建映射条目 */
    uvrpc_idmap_entry_t* entry = calloc(1, sizeof(uvrpc_idmap_entry_t));
    if (!entry) return 0;
    
    entry->msgid_raw = msgid_raw;
    entry->msgid_gateway = msgid_gateway;
    entry->client_handle = client_handle;
    
    /* 存入哈希表 */
    HASH_ADD_INT(ctx->map, msgid_gateway, entry);
    
    return msgid_gateway;
}

/* 反向转换：网关 msgid -> 客户端 msgid */
int uvrpc_idmap_to_raw(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway,
                       uint32_t* out_msgid_raw, void** out_client_handle) {
    if (!ctx || !out_msgid_raw || !out_client_handle) return -1;
    
    /* 查找映射条目 */
    uvrpc_idmap_entry_t* entry = NULL;
    HASH_FIND_INT(ctx->map, &msgid_gateway, entry);
    
    if (!entry) return -1;
    
    /* 返回原始信息 */
    *out_msgid_raw = entry->msgid_raw;
    *out_client_handle = entry->client_handle;
    
    return 0;
}

/* 删除映射 */
void uvrpc_idmap_remove(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway) {
    if (!ctx) return;
    
    /* 查找并删除条目 */
    uvrpc_idmap_entry_t* entry = NULL;
    HASH_FIND_INT(ctx->map, &msgid_gateway, entry);
    
    if (entry) {
        HASH_DEL(ctx->map, entry);
        free(entry);
    }
}
