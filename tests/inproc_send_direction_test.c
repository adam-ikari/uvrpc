/* The two in-process transports must agree about what uvbus_send() means.
 *
 * INPROC has always treated a server's send as "to every client". SAMELOOP
 * implemented only the client-to-server direction, so a server that replied
 * with uvbus_send() -- the obvious thing to write, and what the INPROC examples
 * do -- dereferenced a NULL sameloop_client and segfaulted. Nothing caught it
 * because the SAMELOOP examples that exercise it were not in the test suite.
 *
 * This pins both directions on both transports: a client's send reaches the
 * server, and a server's send reaches every client. It also covers the failure
 * the examples were silently hitting -- a recv callback whose context is
 * whatever the config registered, not the bus handle.
 */
#include <stdio.h>
#include <string.h>
#include <uv.h>

#include <uvbus.h>

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#define NUM_CLIENTS 3

static int server_received;
static int client_received[NUM_CLIENTS];
static int last_client_ctx_was_null;

static void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)data; (void)size; (void)server_ctx;
    if (client_ctx == NULL) {
        last_client_ctx_was_null = 1;
    }
    server_received++;
}

static void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)data; (void)size; (void)server_ctx;
    /* Each client registered its own index as the recv context. */
    int id = (int)(intptr_t)client_ctx;
    if (id >= 0 && id < NUM_CLIENTS) {
        client_received[id]++;
    }
}

static void run(const char* label, const char* address, uvbus_transport_type_t type) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
    check(reg != NULL, "registry created");
    if (!reg) return;

    server_received = 0;
    memset(client_received, 0, sizeof(client_received));
    last_client_ctx_was_null = 0;

    uvbus_config_t* sc = uvbus_config_new();
    uvbus_config_set_loop(sc, &loop);
    uvbus_config_set_loop_registry(sc, reg);
    uvbus_config_set_transport(sc, type);
    uvbus_config_set_address(sc, address);
    uvbus_config_set_recv_callback(sc, server_recv, NULL);
    uvbus_t* server = uvbus_server_new(sc);
    uvbus_config_free(sc);
    check(server != NULL, "server created");
    if (uvbus_listen(server) != UVBUS_OK) {
        printf("FAIL: %s listen\n", label);
        failures++;
    }

    uvbus_t* clients[NUM_CLIENTS];
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_config_t* cc = uvbus_config_new();
        uvbus_config_set_loop(cc, &loop);
        uvbus_config_set_loop_registry(cc, reg);
        uvbus_config_set_transport(cc, type);
        uvbus_config_set_address(cc, address);
        uvbus_config_set_recv_callback(cc, client_recv, (void*)(intptr_t)i);
        clients[i] = uvbus_client_new(cc);
        uvbus_config_free(cc);
        if (!clients[i] || uvbus_connect(clients[i]) != UVBUS_OK) {
            printf("FAIL: %s client %d connect\n", label, i);
            failures++;
        }
    }

    /* Client -> server. SAMELOOP delivers this through a direct callback, so
     * it has already happened by the time the loop runs. */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_send(clients[i], (const uint8_t*)"ping", 4);
    }
    check(server_received == NUM_CLIENTS, "every client reached the server");

    /* Server -> every client, through uvbus_send(). This is the call that used
     * to segfault on SAMELOOP. */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        check(uvbus_send(server, (const uint8_t*)"pong", 4) == UVBUS_OK,
              "a server's uvbus_send succeeds");
    }
    /* One send reaches every client, so three sends are three per client. */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        check(client_received[i] == NUM_CLIENTS,
              "every client got each of the server's sends");
    }

    /* uvbus_broadcast is the same operation and must still work. */
    memset(client_received, 0, sizeof(client_received));
    check(uvbus_broadcast(server, (const uint8_t*)"bcast", 5) == UVBUS_OK,
          "broadcast succeeds");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        check(client_received[i] == 1, "every client got the broadcast");
    }

    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_free(clients[i]);
    }
    uvbus_free(server);
    uvbus_loop_registry_free(reg);
    /* Reclaiming the transport and its per-connection structs is deferred to
     * uv_close callbacks, which only run while the loop is pumped. */
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
}

int main(void) {
    run("inproc", "inproc://send_direction", UVBUS_TRANSPORT_INPROC);
    run("sameloop", "sameloop://send_direction", UVBUS_TRANSPORT_SAMELOOP);

    if (failures == 0) {
        printf("in-process send direction: OK\n");
        return 0;
    }
    printf("in-process send direction: %d check(s) failed\n", failures);
    return 1;
}
