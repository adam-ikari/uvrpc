/**
 * @file uvbus_frame.h
 * @brief Wire framing shared by the socket transports.
 *
 * TCP, IPC and UDP put the same 4-byte big-endian length prefix in front of
 * every frame, followed by the payload verbatim. This used to be nine copies
 * of four shifts plus a memcpy (three send slots in each of the three socket
 * drivers). The bytes are one cross-transport invariant, so they live in one
 * place: a driver that changes the prefix here keeps it for every slot.
 *
 * Allocation stays with the caller. A failed allocation has to be reported
 * through each slot's own error code, and that is transport-specific; only
 * the bytes after a successful allocation are shared.
 */

#ifndef UVBUS_FRAME_H
#define UVBUS_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Bytes of length prefix at the front of every socket frame. */
#define UVBUS_FRAME_PREFIX_SIZE 4

/** Maximum frame size the prefix can express. */
#define UVBUS_FRAME_MAX_SIZE ((size_t)0xFFFFFFFFu)

/**
 * @brief Write one complete frame into an already allocated buffer.
 *
 * @param dst Buffer with room for UVBUS_FRAME_PREFIX_SIZE + size bytes.
 * @param data Payload bytes; may be NULL when size is 0.
 * @param size Payload length; must fit in 32 bits (the prefix is uint32).
 * @return Pointer past the payload (dst + 4 + size).
 */
static inline uint8_t* uvbus_frame_write(uint8_t* dst,
                                         const uint8_t* data, size_t size) {
    dst[0] = (uint8_t)(size >> 24);
    dst[1] = (uint8_t)(size >> 16);
    dst[2] = (uint8_t)(size >> 8);
    dst[3] = (uint8_t)size;
    if (size > 0) {
        memcpy(dst + UVBUS_FRAME_PREFIX_SIZE, data, size);
    }
    return dst + UVBUS_FRAME_PREFIX_SIZE + size;
}

#endif /* UVBUS_FRAME_H */
