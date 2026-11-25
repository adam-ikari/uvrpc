/*
 * Every frame encoder must hand back an 8-byte-aligned payload pointer.
 *
 * The nested payload is a complete FlatBuffers buffer carried in the frame's
 * [ubyte] vector. A handler reads int64/double fields from it through typed
 * pointers, so the vector's data must be 8-aligned in the FINALIZED buffer,
 * not merely in the builder's emission cursor. start_as_root() uses a
 * block_align of 0, which lets the 4-byte root offset shift the vector to a
 * 4-mod-8 offset; start_frame() passes block_align=8 instead.
 *
 * This pins all three encoders (request, response, response-more) across
 * payload sizes. It is a direct invariant test: x86 tolerates the misaligned
 * load, so without UBSan the observable signal is the pointer's alignment.
 *
 * Run: ctest -R frame_payload_alignment
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uvrpc.h"
#include "uvrpc_flatbuffers.h"

static int failures;

static void check(const char* what, size_t n, int ok) {
    if (!ok) {
        printf("  FAIL: %s (payload=%zu)\n", what, n);
        failures++;
    }
}

/* Encode with one of the three encoders, then verify the decoded payload
 * pointer is 8-aligned and that a double can be read through it. */
static void probe(size_t n, int kind) {
    uint8_t payload[64];
    double* d = (double*)payload;
    memset(payload, 0, sizeof(payload));
    d[0] = 1.5;
    if (n >= 16) d[1] = 2.25;

    uint8_t* frame = NULL;
    size_t size = 0;
    int rc;
    switch (kind) {
        case 0:
            rc = uvrpc_encode_request(1, "echo", payload, n, &frame, &size);
            break;
        case 1:
            rc = uvrpc_encode_response(1, payload, n, &frame, &size);
            break;
        default:
            rc = uvrpc_encode_response_more(1, payload, n, &frame, &size);
            break;
    }
    if (rc != UVRPC_OK || !frame) {
        check("encode", n, 0);
        return;
    }

    uint32_t msgid = 0;
    char* method = NULL;
    const uint8_t* result = NULL;
    size_t result_size = 0;
    rc = (kind == 0)
             ? uvrpc_decode_request(frame, size, &msgid, &method, &result, &result_size)
             : uvrpc_decode_response(frame, size, &msgid, &result, &result_size);
    if (rc != UVRPC_OK) {
        check("decode", n, 0);
        free(method);
        uvrpc_free_encoded(frame);
        return;
    }

    char label[64];
    snprintf(label, sizeof(label), "%s params 8-byte aligned",
             kind == 0 ? "request" : kind == 1 ? "response" : "response-more");
    check(label, n, (((uintptr_t)result & 7) == 0));
    check("payload size preserved", n, result_size == n);

    /* The read itself is the UB that UBSan catches when alignment is wrong. */
    if (result_size >= sizeof(double)) {
        double observed;
        memcpy(&observed, result, sizeof(observed));
        check("double value intact", n, observed == 1.5);
    }

    free(method);
    uvrpc_free_encoded(frame);
}

int main(void) {
    static const char* kinds[] = {"request", "response", "response-more"};
    for (int k = 0; k < 3; k++) {
        for (size_t n = 1; n <= 40; n++) {
            probe(n, k);
        }
        printf("  %s: checked 40 payload sizes\n", kinds[k]);
    }
    if (failures) {
        printf("frame_payload_alignment: %d failure(s)\n", failures);
        return 1;
    }
    printf("frame_payload_alignment: 0 failure(s)\n");
    return 0;
}
