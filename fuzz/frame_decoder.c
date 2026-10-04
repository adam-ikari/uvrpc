/*
 * Fuzz target for the frame decoders.
 *
 * A client and a server read bytes off the wire and hand them straight to
 * uvrpc_decode_request / uvrpc_decode_response. That is the attack surface of
 * an RPC library -- the flatbuffers verifier is the only thing between an
 * arbitrary peer's bytes and code that trusts offsets. This drives those two
 * decoders (plus the frame-type probe) with whatever the fuzzer produces, so
 * a malformed frame that crashes or corrupts is what the run reports.
 *
 * Build with clang and the fuzzer sanitizer. Not part of the default build;
 * the ci.yml undefined-check / fuzz job invokes it explicitly.
 */

#include <stddef.h>
#include <stdint.h>

#include "../src/uvrpc_flatbuffers.h"

/* The fuzzer calls this with each candidate input. The decoders validate
 * before trusting embedded offsets, so most garbage is rejected on the first
 * parse without touching anything dangerous -- which is exactly what we want
 * to assert: no path into them may read out of bounds or misparse. */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* Server side of a frame. method is allocated by the decoder on success,
     * so free it or the leak checker drowns the run in noise. */
    uint32_t msgid = 0;
    char* method = NULL;
    const uint8_t* params = NULL;
    size_t params_size = 0;
    (void)uvrpc_decode_request(data, size, &msgid, &method, &params, &params_size);
    if (method) {
        uvrpc_free_decoded(method);
    }

    /* Client side of a frame. */
    uint32_t rmsgid = 0;
    const uint8_t* result = NULL;
    size_t result_size = 0;
    (void)uvrpc_decode_response(data, size, &rmsgid, &result, &result_size);

    /* The frame-type probe is reachable before either decoder runs. */
    (void)uvrpc_get_frame_type(data, size);

    return 0;
}