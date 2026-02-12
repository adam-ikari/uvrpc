/**
 * UVRPC ID Mapping for Gateway
 * 
 * 网关场景下的 ID 转换机制：
 * 1. 客户端 msgid_raw -> 网关 msgid_gateway -> 后端
 * 2. 后端响应 -> 网关查表 -> 客户端 msgid_raw
 */

#ifndef UVRPC_IDMAP_H
#define UVRPC_IDMAP_H

#include <stdint.h>
#include <stddef.h>
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ID 映射条目 */
typedef struct {
    uint32_t msgid_raw;        /* 客户端原始 msgid */
    uint32_t msgid_gateway;    /* 网关 msgid */
    void* client_handle;       /* 客户端连接句柄 */
    UT_hash_handle hh;         /* uthash */
} uvrpc_idmap_entry_t;

/* ID 映射上下文 */
typedef struct {
    uvrpc_idmap_entry_t* map;  /* msgid_gateway -> entry 的哈希表 */
    uint32_t next_gateway_id;  /* 下一个网关 msgid */
} uvrpc_idmap_ctx_t;

/* 创建 ID 映射上下文 */
uvrpc_idmap_ctx_t* uvrpc_idmap_ctx_new(void);
void uvrpc_idmap_ctx_free(uvrpc_idmap_ctx_t* ctx);

/* 转换：客户端 msgid -> 网关 msgid */
uint32_t uvrpc_idmap_to_gateway(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_raw, void* client_handle);

/* 反向转换：网关 msgid -> 客户端 msgid */
int uvrpc_idmap_to_raw(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway, 
                       uint32_t* out_msgid_raw, void** out_client_handle);

/* 删除映射 */
void uvrpc_idmap_remove(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_IDMAP_H */
