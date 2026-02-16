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

/* Generate next message ID */
uint32_t uvrpc_msgid_next(uvrpc_msgid_ctx_t* ctx) {
    if (!ctx) return 0;

    return ctx->next_seq++;
}