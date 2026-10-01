/*
 * A peer that can open a connection must not be able to take the process down.
 *
 * The framing layer validates the 4-byte length prefix and allocates exactly
 * what the prefix claims, so the interesting surface is one level down: a frame
 * whose length is legal but whose payload is not a FlatBuffers root. The
 * generated reader trusts a buffer once it looks like a root and then follows
 * offsets the peer chose, which is a remotely reachable out-of-bounds read.
 *
 * Each case opens a fresh connection, and after it the server must still accept
 * another one. That is how a crash surfaces here: the liveness probe fails.
 * A decoder that rejects the frame outright is fine; what must not happen is
 * the process dying.
 *
 * Before frame verification was added, the first case segfaulted the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <uv.h>

#include "uvrpc.h"
#include "free_port.h"

/* Internal, on purpose: building a genuine frame to corrupt is the point. The
 * test asserts on what the server does with it, not on the encoder. */
#include "../../src/uvrpc_flatbuffers.h"

/* From the OS, not fixed: a hard-coded port fails for reasons that have nothing
 * to do with what this scenario is about. */
static int port;
static char address[64];

/* Watchdog: a scenario that must not be able to hang also must not be able to
 * wait forever to fail. */
#define WATCHDOG_MS 20000
#define PUMP_MS 120

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void watchdog_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    printf("FAIL: run exceeded %d ms\n", WATCHDOG_MS);
    _exit(1);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

static void pump(uv_loop_t* loop, uv_timer_t* timer, int ms) {
    uv_timer_start(timer, stop_cb, ms, 0);
    uv_run(loop, UV_RUN_DEFAULT);
}

/* The server has a handler registered but nothing should reach it: none of
 * these payloads is a valid frame. */
static void handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("FAIL: a malformed frame reached the handler (method=%s)\n",
           req->method ? req->method : "(null)");
    failures++;
    uvrpc_request_free(req);
}

static int dial(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Send a frame of the given payload, then close. */
static void send_payload(const uint8_t* payload, size_t len) {
    int fd = dial();
    if (fd < 0) return;

    uint8_t header[4];
    put32(header, (uint32_t)len);
    if (write(fd, header, 4) != 4) { close(fd); return; }
    if (len && write(fd, payload, len) != (ssize_t)len) { close(fd); return; }
    close(fd);
}

/* Send the buffer but declare a much larger length, then hold the connection
 * briefly and drop it. */
static void send_lying_length(const uint8_t* payload, size_t len, uint32_t claim) {
    int fd = dial();
    if (fd < 0) return;

    uint8_t header[4];
    put32(header, claim);
    if (write(fd, header, 4) < 0) { /* peer already gone */ }
    if (len && write(fd, payload, len) < 0) { /* peer already gone */ }
    close(fd);
}

/* Each case returns 0 if it could send, -1 if it could not connect. */
static int send_garbage(void) {
    uint8_t payload[256];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 7 + 3);
    }
    int fd = dial();
    if (fd < 0) return -1;
    close(fd);
    send_payload(payload, sizeof(payload));
    return 0;
}

static int send_zeros(void) {
    uint8_t payload[64];
    memset(payload, 0, sizeof(payload));
    send_payload(payload, sizeof(payload));
    return 0;
}

static int send_ones(void) {
    uint8_t payload[64];
    memset(payload, 0xff, sizeof(payload));
    send_payload(payload, sizeof(payload));
    return 0;
}

/* A genuine request frame, cut in half: the length prefix promises the whole
 * buffer and the peer stops sending. */
static int send_truncated(void) {
    uint8_t* encoded = NULL;
    size_t size = 0;
    if (uvrpc_encode_request(1, "handler", (const uint8_t*)"x", 1,
                             &encoded, &size) != UVRPC_OK) {
        return 0;
    }

    int fd = dial();
    if (fd >= 0) {
        uint8_t header[4];
        put32(header, (uint32_t)size);
        if (write(fd, header, 4) < 0) { /* peer already gone */ }
        if (write(fd, encoded, size / 2) < 0) { /* peer already gone */ }
        close(fd);
    }
    /* FlatCC owns the encode output; free() is the documented release. */
    free(encoded);
    return 0;
}

static int send_empty_method(void) {
    uint8_t* encoded = NULL;
    size_t size = 0;
    if (uvrpc_encode_request(2, "", (const uint8_t*)"x", 1,
                             &encoded, &size) != UVRPC_OK) {
        return 0;
    }
    send_payload(encoded, size);
    free(encoded);
    return 0;
}

static int send_forged_huge(void) {
    send_lying_length((const uint8_t*)"12345678", 8, 0x40000000u);
    return 0;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uv_timer_t pump_timer;
    uv_timer_init(&loop, &pump_timer);

    uv_timer_t watchdog;
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    port = acceptance_free_port();
    if (port <= 0) {
        printf("FAIL: could not obtain a port from the OS\n");
        return 1;
    }
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", port);

    uvrpc_config_t* config = uvrpc_config_set_loop(uvrpc_config_new(), &loop);
    config = uvrpc_config_set_address(config, address);
    config = uvrpc_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_config_free(config);

    check(server != NULL, "server created");
    if (!server) return 1;

    uvrpc_server_register(server, "handler", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    for (int i = 0; i < 20; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    struct {
        const char* name;
        int (*send)(void);
    } cases[] = {
        {"garbage payload, legal length",  send_garbage},
        {"all-zero payload",                send_zeros},
        {"all-0xff payload",                send_ones},
        {"genuine frame cut in half",       send_truncated},
        {"empty method name",               send_empty_method},
        {"length prefix claiming 1 GB",     send_forged_huge},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < case_count; i++) {
        printf("[%d/%d] %s\n", i + 1, case_count, cases[i].name);
        cases[i].send();
        pump(&loop, &pump_timer, PUMP_MS);

        int probe = dial();
        if (probe < 0) {
            printf("FAIL: %s: the server no longer accepts connections\n",
                   cases[i].name);
            failures++;
            break;
        }
        close(probe);
        printf("      server alive\n");
    }

    uvrpc_server_stop(server);
    uvrpc_server_free(server);

    uv_close((uv_handle_t*)&pump_timer, NULL);
    uv_close((uv_handle_t*)&watchdog, NULL);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("protocol robustness: OK\n");
        return 0;
    }
    printf("protocol robustness: %d check(s) failed\n", failures);
    return 1;
}
