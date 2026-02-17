/**
 * UVRPC Message ID Encoding Implementation
 */

#include "uvrpc_msgid.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>

/* Create message ID context */
uvrpc_msgid_ctx_t* uvrpc_msgid_ctx_new(void) {
    uvrpc_msgid_ctx_t* ctx = (uvrpc_msgid_ctx_t*)uvrpc_calloc(1, sizeof(uvrpc_msgid_ctx_t));
    if (!ctx) return NULL;

    ctx->next_seq = 1;

    return ctx;
}

/* Free message ID context */
void uvrpc_msgid_ctx_free(uvrpc_msgid_ctx_t* ctx) {
    if (ctx) {
        uvrpc_free(ctx);
    }
}

/* Set initial sequence number (for multi-client scenarios) */
void uvrpc_msgid_ctx_set_start(uvrpc_msgid_ctx_t* ctx, uint32_t start_seq) {
    if (ctx) {
        ctx->next_seq = start_seq;
    }
}

/* Generate next message ID (32-bit) */
uint32_t uvrpc_msgid_next(uvrpc_msgid_ctx_t* ctx) {
    if (!ctx) return 0;

    uint32_t msgid = ctx->next_seq++;
    
    /* Wrap around on overflow (32-bit) */
    if (ctx->next_seq == 0) {
        ctx->next_seq = 1;  /* Skip 0, start from 1 */
    }
    
    return msgid;
}