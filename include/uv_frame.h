/**
 * @file uv_frame.h
 * @brief Frame Layer for UVRPC
 * 
 * Simple frame format: 4-byte big-endian length prefix + payload
 * 
 * Design Philosophy:
 * - Single responsibility: only handle frame encoding/decoding
 * - Zero dependencies: pure data transformation
 * - Efficient: pointer arithmetic, no extra copy
 * 
 * Frame Format:
 * ┌─────────────────────────────────────────┐
 * │ Length (4 bytes, big-endian) │ Payload  │
 * └─────────────────────────────────────────┘
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#ifndef UV_FRAME_H
#define UV_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frame handle
 * 
 * Contains pointer to frame data and size.
 * The data includes the 4-byte length prefix.
 */
typedef struct {
    uint8_t* data;     /**< Frame data (including 4-byte length prefix) */
    size_t size;       /**< Total frame size (4 + payload size) */
} uv_frame_t;

/**
 * @brief Encode payload as frame
 * 
 * Adds 4-byte big-endian length prefix to payload.
 * 
 * @param payload Payload data
 * @param payload_size Payload size
 * @return Frame handle, or NULL on error
 * 
 * @note Caller must free the frame using uv_frame_free()
 */
uv_frame_t* uv_frame_encode(const uint8_t* payload, size_t payload_size);

/**
 * @brief Free frame
 * 
 * @param frame Frame handle
 */
void uv_frame_free(uv_frame_t* frame);

/**
 * @brief Decode frame to extract payload
 * 
 * Parses the 4-byte length prefix and returns pointer to payload.
 * No copy is performed; payload pointer points into frame data.
 * 
 * @param frame_data Frame data (including 4-byte length prefix)
 * @param frame_size Frame size
 * @param payload Output: pointer to payload (points into frame_data)
 * @param payload_size Output: payload size
 * @return 0 on success, error code on failure
 * 
 * @note payload pointer is only valid as long as frame_data is valid
 * @note This function does not allocate memory
 */
int uv_frame_decode(const uint8_t* frame_data, size_t frame_size,
                   const uint8_t** payload, size_t* payload_size);

/**
 * @brief Get frame size from payload size
 * 
 * @param payload_size Payload size
 * @return Frame size (4 + payload_size)
 */
static inline size_t uv_frame_get_size(size_t payload_size) {
    return 4 + payload_size;
}

/**
 * @brief Validate frame
 * 
 * Checks if frame data is valid (correct length prefix).
 * 
 * @param frame_data Frame data
 * @param frame_size Frame size
 * @return 1 if valid, 0 otherwise
 */
int uv_frame_validate(const uint8_t* frame_data, size_t frame_size);

#ifdef __cplusplus
}
#endif

#endif /* UV_FRAME_H */