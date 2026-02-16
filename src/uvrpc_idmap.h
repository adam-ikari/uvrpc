/**
 * UVRPC ID Mapping for Gateway
 *
 * ID transformation mechanism for gateway scenarios:
 * 1. Client msgid_raw -> Gateway msgid_gateway -> Backend
 * 2. Backend response -> Gateway lookup -> Client msgid_raw
 */

#ifndef UVRPC_IDMAP_H
#define UVRPC_IDMAP_H

#include <stdint.h>
#include <stddef.h>
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ID mapping entry */
typedef struct {
    uint32_t msgid_raw;        /* Client original msgid */
    uint32_t msgid_gateway;    /* Gateway msgid */
    void* client_handle;       /* Client connection handle */
    UT_hash_handle hh;         /* uthash */
} uvrpc_idmap_entry_t;

/* ID mapping context */
typedef struct {
    uvrpc_idmap_entry_t* map;  /* Hash table: msgid_gateway -> entry */
    uint32_t next_gateway_id;  /* Next gateway msgid */
} uvrpc_idmap_ctx_t;

/* Create ID mapping context */
uvrpc_idmap_ctx_t* uvrpc_idmap_ctx_new(void);
void uvrpc_idmap_ctx_free(uvrpc_idmap_ctx_t* ctx);

/* Transform: client msgid -> gateway msgid */
uint32_t uvrpc_idmap_to_gateway(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_raw, void* client_handle);

/* Reverse transform: gateway msgid -> client msgid */
int uvrpc_idmap_to_raw(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway,
                       uint32_t* out_msgid_raw, void** out_client_handle);

/* Remove mapping */
void uvrpc_idmap_remove(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_IDMAP_H */
