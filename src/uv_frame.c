/**
 * @file uv_frame.c
 * @brief Frame Layer Implementation
 */

#include "../include/uv_frame.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>

/**
 * @brief Encode payload as frame
 */
uv_frame_t* uv_frame_encode(const uint8_t* payload, size_t payload_size) {
    if (!payload && payload_size > 0) {
        return NULL;
    }
    
    /* Allocate frame buffer */
    size_t frame_size = uv_frame_get_size(payload_size);
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(frame_size);
    if (!frame_data) {
        return NULL;
    }
    
    /* Write length in big-endian */
    frame_data[0] = (payload_size >> 24) & 0xFF;
    frame_data[1] = (payload_size >> 16) & 0xFF;
    frame_data[2] = (payload_size >> 8) & 0xFF;
    frame_data[3] = payload_size & 0xFF;
    
    /* Copy payload */
    if (payload_size > 0 && payload) {
        memcpy(frame_data + 4, payload, payload_size);
    }
    
    /* Create frame handle */
    uv_frame_t* frame = (uv_frame_t*)uvrpc_alloc(sizeof(uv_frame_t));
    if (!frame) {
        uvrpc_free(frame_data);
        return NULL;
    }
    
    frame->data = frame_data;
    frame->size = frame_size;
    
    return frame;
}

/**
 * @brief Free frame
 */
void uv_frame_free(uv_frame_t* frame) {
    if (!frame) {
        return;
    }
    
    if (frame->data) {
        uvrpc_free(frame->data);
    }
    
    uvrpc_free(frame);
}

/**
 * @brief Decode frame to extract payload
 */
int uv_frame_decode(const uint8_t* frame_data, size_t frame_size,
                   const uint8_t** payload, size_t* payload_size) {
    if (!frame_data || frame_size < 4 || !payload || !payload_size) {
        return -1;
    }
    
    /* Parse length */
    size_t size = ((size_t)frame_data[0] << 24) | 
                  ((size_t)frame_data[1] << 16) | 
                  ((size_t)frame_data[2] << 8) | 
                  frame_data[3];
    
    /* Validate frame size */
    if (frame_size != 4 + size) {
        return -1;
    }
    
    /* Return payload pointer (no copy) */
    *payload = frame_data + 4;
    *payload_size = size;
    
    return 0;
}

/**
 * @brief Validate frame
 */
int uv_frame_validate(const uint8_t* frame_data, size_t frame_size) {
    if (!frame_data || frame_size < 4) {
        return 0;
    }
    
    /* Parse length */
    size_t size = ((size_t)frame_data[0] << 24) | 
                  ((size_t)frame_data[1] << 16) | 
                  ((size_t)frame_data[2] << 8) | 
                  frame_data[3];
    
    /* Validate frame size */
    if (frame_size != 4 + size) {
        return 0;
    }
    
    /* Validate reasonable size (max 1MB) */
    if (size > 1024 * 1024) {
        return 0;
    }
    
    return 1;
}