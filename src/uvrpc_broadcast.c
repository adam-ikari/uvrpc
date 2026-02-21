/**
 * UVRPC Broadcast Message Encoding/Decoding using FlatCC
 */

#include "uvrpc_broadcast.h"
#include "broadcast_reader.h"
#include "broadcast_builder.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* Encode broadcast message */
int uvrpc_broadcast_encode(const char* topic, const uint8_t* data, size_t size,
                           uint8_t** out_msg, size_t* out_msg_size) {
    if (!topic || !out_msg || !out_msg_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Create data vector */
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (data && size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, data, size);
    }

    /* Create broadcast message */
    uvrpc_BroadcastMessage_start_as_root(&builder);
    uvrpc_BroadcastMessage_topic_add(&builder, flatbuffers_string_create_str(&builder, topic));
    uvrpc_BroadcastMessage_data_add(&builder, data_ref);
    uvrpc_BroadcastMessage_end_as_root(&builder);

    void* buf = flatcc_builder_finalize_buffer(&builder, out_msg_size);
    if (buf) {
        *out_msg = buf;
    }

    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

/* Decode broadcast message */
int uvrpc_broadcast_decode(const uint8_t* msg, size_t msg_size,
                           char** out_topic, const uint8_t** out_data, size_t* out_data_size) {
    if (!msg || !out_topic || !out_data || !out_data_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    uvrpc_BroadcastMessage_table_t broadcast = uvrpc_BroadcastMessage_as_root(msg);

    if (!broadcast) {
        return UVRPC_ERROR;
    }

    /* Extract topic */
    const char* topic = uvrpc_BroadcastMessage_topic(broadcast);
    if (topic) {
        *out_topic = uvrpc_strdup(topic);
    } else {
        *out_topic = uvrpc_strdup("");
    }

    /* Extract data */
    flatbuffers_uint8_vec_t data = uvrpc_BroadcastMessage_data(broadcast);
    if (data) {
        *out_data = data;
        *out_data_size = flatbuffers_uint8_vec_len(data);
    } else {
        *out_data = NULL;
        *out_data_size = 0;
    }

    return UVRPC_OK;
}

/* Free decoded topic */
void uvrpc_broadcast_free_decoded(char* topic) {
    if (topic) {
        uvrpc_free(topic);
    }
}