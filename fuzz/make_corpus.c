/*
 * Build the seed corpus for the frame fuzzer from real encoded frames.
 *
 * libFuzzer starts from random inputs, which the verifier rejects at the door;
 * without seeds it spends its budget re-discovering that a 4-byte table header
 * is rejected. Seeds that are genuine frames -- with method names of varying
 * length and payloads that exercise both empty and full responses -- let it
 * mutate something that parses, where the interesting misparse lives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/uvrpc.h"        /* error codes */
#include "../src/uvrpc_flatbuffers.h"  /* encode / free_encoded */

static int write_seed(const char* path, const uint8_t* buf, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (size > 0 && fwrite(buf, 1, size, f) != size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    const char* out_dir = (argc > 1) ? argv[1] : ".";
    char path[512];
    int n = 0;

#define WRITE(label, ptr, size) do {                                   \
        snprintf(path, sizeof(path), "%s/seed_%02d_%s.bin", out_dir, n++, label); \
        if (write_seed(path, ptr, size) != 0) {                        \
            fprintf(stderr, "failed: %s\n", path); return 1;           \
        }                                                              \
    } while (0)

    /* Requests: short method, long method, empty payload, non-empty payload. */
    uint8_t* out = NULL; size_t out_size = 0;
    if (uvrpc_encode_request(1, "echo", (const uint8_t*)"x", 1, &out, &out_size) == UVRPC_OK) {
        WRITE("req_short", out, out_size); uvrpc_free_encoded(out);
    }
    if (uvrpc_encode_request(2, "a_quite_long_method_name_to_stress_offsets",
                             NULL, 0, &out, &out_size) == UVRPC_OK) {
        WRITE("req_empty", out, out_size); uvrpc_free_encoded(out);
    }
    const uint8_t payload[200] = {0};
    if (uvrpc_encode_request(3, "add", payload, sizeof(payload), &out, &out_size) == UVRPC_OK) {
        WRITE("req_payload", out, out_size); uvrpc_free_encoded(out);
    }

    /* Responses: empty and with a result. */
    if (uvrpc_encode_response(1, NULL, 0, &out, &out_size) == UVRPC_OK) {
        WRITE("resp_empty", out, out_size); uvrpc_free_encoded(out);
    }
    if (uvrpc_encode_response(2, payload, 32, &out, &out_size) == UVRPC_OK) {
        WRITE("resp_data", out, out_size); uvrpc_free_encoded(out);
    }

    /* An error frame: a different frame type, which the decoders must not
     * accept as a request or response. */
    if (uvrpc_encode_error(4, UVRPC_ERROR_NOT_FOUND, "Method not found", &out, &out_size) == UVRPC_OK) {
        WRITE("error_frame", out, out_size); uvrpc_free_encoded(out);
    }

    printf("wrote %d seeds to %s\n", n, out_dir);
    return 0;
}