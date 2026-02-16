/**
 * UVRPC ID Mapping Implementation
 */

#include "uvrpc_idmap.h"
#include "../include/uvrpc_allocator.h"
#include <uthash.h>
#include <stdlib.h>
#include <string.h>

/* Create ID mapping context */
uvrpc_idmap_ctx_t* uvrpc_idmap_ctx_new(void) {
    uvrpc_idmap_ctx_t* ctx = uvrpc_calloc(1, sizeof(uvrpc_idmap_ctx_t));
    if (!ctx) return NULL;

    ctx->map = NULL;
    ctx->next_gateway_id = 1;

    return ctx;
}

/* Free ID mapping context */
void uvrpc_idmap_ctx_free(uvrpc_idmap_ctx_t* ctx) {
    if (!ctx) return;

    /* Free all entries in hash table */
    uvrpc_idmap_entry_t* entry, *tmp;
    HASH_ITER(hh, ctx->map, entry, tmp) {
        HASH_DEL(ctx->map, entry);
        uvrpc_free(entry);
    }

    uvrpc_free(ctx);
}

/* Transform: client msgid -> gateway msgid */
uint32_t uvrpc_idmap_to_gateway(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_raw, void* client_handle) {
    if (!ctx) return 0;

    /* Generate new gateway msgid */
    uint32_t msgid_gateway = ctx->next_gateway_id++;

    /* Create mapping entry */
    uvrpc_idmap_entry_t* entry = uvrpc_calloc(1, sizeof(uvrpc_idmap_entry_t));
    if (!entry) return 0;

    entry->msgid_raw = msgid_raw;
    entry->msgid_gateway = msgid_gateway;
    entry->client_handle = client_handle;

    /* Store in hash table */
    HASH_ADD_INT(ctx->map, msgid_gateway, entry);

    return msgid_gateway;
}

/* Reverse transform: gateway msgid -> client msgid */
int uvrpc_idmap_to_raw(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway,
                       uint32_t* out_msgid_raw, void** out_client_handle) {
    if (!ctx || !out_msgid_raw || !out_client_handle) return -1;

    /* Find mapping entry */
    uvrpc_idmap_entry_t* entry = NULL;
    HASH_FIND_INT(ctx->map, &msgid_gateway, entry);

    if (!entry) return -1;

    /* Return original information */
    *out_msgid_raw = entry->msgid_raw;
    *out_client_handle = entry->client_handle;

    return 0;
}

/* Remove mapping */
void uvrpc_idmap_remove(uvrpc_idmap_ctx_t* ctx, uint32_t msgid_gateway) {
    if (!ctx) return;

    /* Find and delete entry */
    uvrpc_idmap_entry_t* entry = NULL;
    HASH_FIND_INT(ctx->map, &msgid_gateway, entry);

    if (entry) {
        HASH_DEL(ctx->map, entry);
        uvrpc_free(entry);
    }
}
