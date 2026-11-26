/**
 * UVBus UDP Transport Implementation
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include "uvbus_frame.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/* Forward declarations */
typedef struct uvbus_udp_client uvbus_udp_client_t;
typedef struct uvbus_udp_server uvbus_udp_server_t;

/* UDP client structure - optimized for cache locality */
struct uvbus_udp_client {
    /* Frequently accessed fields - grouped together */
    int is_connected;
    int port;
    size_t read_pos;
    
    /* Pointer fields */
    char* host;
    void* parent_transport;
    
    /* LibUV handle and address - kept together */
    uv_udp_t udp_handle;
    struct sockaddr_in server_addr;
    
    /* Large buffer - placed at end to improve cache locality for small fields */
    uint8_t read_buffer[UVBUS_MAX_BUFFER_SIZE];  /* 1MB read buffer */
};

/* Idle UDP peers are expired so a long-running server does not pin 1024 NAT
 * slots forever. The timer interval is one timeout; the env override exists
 * only to keep the expiry test deterministic. */
#define UVBUS_UDP_PEER_IDLE_MS 60000u

/* UDP server structure - optimized for cache locality */
struct uvbus_udp_server {
    /* Frequently accessed fields - grouped together */
    int is_listening;
    int port;
    int num_clients;
    int active_clients;
    
    /* Pointer fields */
    char* host;
    void* parent_transport;
    
    /* Client address list for send_to. Slot addresses are stable client_ctx
     * values handed upward; expiry marks a slot unused, never compacts it. */
    struct sockaddr_storage* client_addrs;
    uint8_t* client_active;
    uint64_t* client_last_seen;
    int max_clients;
    uint64_t idle_timeout_ms;
    uv_timer_t* idle_timer;
    
    /* LibUV handle - kept at end */
    uv_udp_t udp_handle;
};

/* Parse address */
static int parse_udp_address(const char* address, char** host, int* port) {
    if (!address || !host || !port) {
        return -1;
    }
    
    /* Skip protocol prefix */
    const char* addr_start = address;
    if (strncmp(address, "udp://", 6) == 0) {
        addr_start += 6;
    }
    
    /* Find port separator */
    const char* colon = strrchr(addr_start, ':');
    if (!colon) {
        return -1;
    }
    
    /* Extract host */
    size_t host_len = colon - addr_start;
    *host = (char*)uvrpc_alloc(host_len + 1);
    if (!*host) {
        return -1;
    }
    memcpy(*host, addr_start, host_len);
    (*host)[host_len] = '\0';
    
    /* Extract port */
    *port = atoi(colon + 1);
    
    return 0;
}

static uint64_t udp_peer_idle_ms(void) {
    const char* value = getenv("UVRPC_UDP_PEER_IDLE_MS");
    if (!value || !*value) {
        return UVBUS_UDP_PEER_IDLE_MS;
    }
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (*end != '\0' || parsed == 0) {
        return UVBUS_UDP_PEER_IDLE_MS;
    }
    return (uint64_t)parsed;
}

/* Expire idle peers without moving slots: client_addrs[i] is the client_ctx
 * handed upward, so compacting would silently retarget a live context. The
 * close channel tells the RPC layer to drop that identity before the slot can
 * be reused by a different peer. */
static void expire_idle_peers(uvbus_udp_server_t* server, uint64_t now) {
    if (!server) {
        return;
    }
    uvbus_transport_t* transport = (uvbus_transport_t*)server->parent_transport;
    for (int i = 0; i < server->num_clients; i++) {
        if (!server->client_active[i]) {
            continue;
        }
        if (now < server->client_last_seen[i] ||
            now - server->client_last_seen[i] < server->idle_timeout_ms) {
            continue;
        }
        if (transport && transport->conn_close_cb) {
            transport->conn_close_cb(&server->client_addrs[i],
                                     transport->conn_close_ctx);
        }
        server->client_active[i] = 0;
        server->active_clients--;
    }
}

static void on_peer_idle_timer(uv_timer_t* timer) {
    uvbus_udp_server_t* server = (uvbus_udp_server_t*)timer->data;
    uv_update_time(timer->loop);
    expire_idle_peers(server, uv_now(timer->loop));
}

/* Server receive callback */
static void on_server_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                           const struct sockaddr* addr, unsigned flags) {
    (void)flags;
    uvbus_transport_t* transport = (uvbus_transport_t*)handle->data;

    if (nread == 0) {
        uvrpc_free(buf->base);
        return;
    }

    if (nread < 0) {
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }

    if (nread > 0) {
        /* Record client address for broadcast (if not already recorded), and
         * keep the slot index. The layer above is handed a pointer that it
         * stores in its client table, compares on every later datagram, and
         * sends replies to -- so what it is handed has to be something that
         * outlives this callback and is unique per peer.
         *
         * It cannot be addr. libuv builds the peer address in a struct
         * sockaddr_storage local to uv__udp_recvmsg() (deps/libuv/src/unix/
         * udp.c) and passes its address down, so that storage dies with the
         * stack frame. A handler that replies synchronously still finds it
         * intact, which is why a UDP server appears to work; but every peer
         * from every datagram then resolves to the same slot, compares equal
         * to the first one recorded, and the server counts one client no
         * matter how many are talking. The table copy already made for
         * broadcast is stable and per-peer, so use that. */
        uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
        uv_update_time(handle->loop);
        uint64_t now = uv_now(handle->loop);

        int peer_idx = -1;
        for (int i = 0; i < server->num_clients; i++) {
            if (server->client_active[i] &&
                memcmp(&server->client_addrs[i], addr, sizeof(struct sockaddr_storage)) == 0) {
                peer_idx = i;
                break;
            }
        }

        if (peer_idx < 0) {
            /* A full table is not necessarily a busy table: NAT ports go stale
             * long before they go away. Expire first, then reuse a free slot;
             * never move the addresses, because their addresses are client_ctx
             * identities already stored above. */
            if (server->active_clients >= server->max_clients) {
                expire_idle_peers(server, now);
            }
            for (int i = 0; i < server->num_clients; i++) {
                if (!server->client_active[i]) {
                    peer_idx = i;
                    break;
                }
            }
            if (peer_idx < 0 && server->num_clients < server->max_clients) {
                peer_idx = server->num_clients++;
            }
            if (peer_idx >= 0) {
                memcpy(&server->client_addrs[peer_idx], addr, sizeof(struct sockaddr_storage));
                server->client_active[peer_idx] = 1;
                server->active_clients++;
            }
        }

        if (peer_idx >= 0) {
            server->client_last_seen[peer_idx] = now;
        }

        /* Table full: this peer has no slot, so there is nothing stable to
         * hand out. Passing addr instead would put a pointer to a dead frame
         * into the layer above's table, so pass NULL and let the reply fail
         * cleanly. */
        void* client_ctx = (peer_idx >= 0) ? (void*)&server->client_addrs[peer_idx] : NULL;
        
        /* Skip registration messages (UVRPC_REG magic) */
        if (nread >= 9 && memcmp(buf->base, "UVRPC_REG", 9) == 0) {
            /* Registration message received - client is now registered */
            uvrpc_free(buf->base);
            return;
        }

        /* Process frame length prefix */
        if (nread < 4) {
            uvrpc_free(buf->base);
            return;
        }

        /* Parse frame length (big-endian) */
        uint32_t frame_size = (uint32_t)((uint8_t*)buf->base)[0] << 24 |
                              (uint32_t)((uint8_t*)buf->base)[1] << 16 |
                              (uint32_t)((uint8_t*)buf->base)[2] << 8 |
                              (uint32_t)((uint8_t*)buf->base)[3];

        /* Validate frame size */
        if (frame_size == 0 || frame_size > 64*1024) {  /* 64KB max */
            uvrpc_free(buf->base);
            return;
        }

        /* Check if we have the complete frame */
        if ((size_t)nread < 4 + frame_size) {
            uvrpc_free(buf->base);
            return;
        }
        
        if (transport->recv_cb) {
            /* Pass the peer's slot as client_ctx (3rd arg) -- stable, per-peer,
             * and owned by the server, unlike addr. The server_ctx (4th arg)
             * must be recv_ctx — which holds the uvrpc_server_t* — so the
             * callback can dispatch to the registered handler.
             *
             * buf->base + 4 is only 4-byte aligned. Nested FlatBuffers
             * payloads are 8-byte aligned relative to the frame start, so a
             * misaligned frame start puts their int64/double fields back into
             * undefined territory. Hand the callback an aligned copy, as the
             * client receive path does. */
            uint8_t* frame_copy = (uint8_t*)uvrpc_alloc(frame_size);
            if (!frame_copy) {
                UVBUS_LOG_ERROR("Failed to allocate %u bytes for frame", frame_size);
                if (transport->error_cb) {
                    transport->error_cb(UVBUS_ERROR_NO_MEMORY, "Frame allocation failed", transport->callback_ctx);
                }
                uvrpc_free(buf->base);
                return;
            }
            memcpy(frame_copy, (const uint8_t*)buf->base + 4, frame_size);
            transport->recv_cb(frame_copy, frame_size, client_ctx, transport->recv_ctx);
            uvrpc_free(frame_copy);
        }
    }

    uvrpc_free(buf->base);
}

/* Server alloc callback */
static void on_server_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    (void)suggested_size;
    (void)handle;
    /* Use 64KB buffer for receiving UDP packets - smaller than max to reduce memory pressure */
    buf->base = (char*)uvrpc_alloc(65536);
    if (buf->base) {
        buf->len = 65536;
    } else {
        buf->len = 0;
    }
}

/* Client receive callback */
static void on_client_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                           const struct sockaddr* addr, unsigned flags) {
    (void)flags;  /* Not used: all received datagrams are processed uniformly */
    /* Get client from handle data */
    uvbus_udp_client_t* client = (uvbus_udp_client_t*)handle->data;
    if (!client) {
        uvrpc_free(buf->base);
        return;
    }

    uvbus_transport_t* transport = (uvbus_transport_t*)client->parent_transport;
    if (!transport) {
        uvrpc_free(buf->base);
        return;
    }

    if (nread < 0) {
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }

    if (nread > 0) {
        /* Filter: Only accept packets from the server we connected to
         * This prevents receiving responses intended for other clients */
        if (addr && addr->sa_family == AF_INET) {
            const struct sockaddr_in* from_addr = (const struct sockaddr_in*)addr;
            const struct sockaddr_in* server_addr = &client->server_addr;

            /* Compare IP address and port */
            if (from_addr->sin_addr.s_addr != server_addr->sin_addr.s_addr ||
                from_addr->sin_port != server_addr->sin_port) {
                /* Packet not from our server, ignore it */
                uvrpc_free(buf->base);
                return;
            }
        } else if (addr && addr->sa_family == AF_INET6) {
            /* IPv6 support - compare addresses */
            const struct sockaddr_in6* from_addr = (const struct sockaddr_in6*)addr;
            const struct sockaddr_in6* server_addr = (const struct sockaddr_in6*)&client->server_addr;

            if (memcmp(&from_addr->sin6_addr, &server_addr->sin6_addr, sizeof(struct in6_addr)) != 0 ||
                from_addr->sin6_port != server_addr->sin6_port) {
                /* Packet not from our server, ignore it */
                uvrpc_free(buf->base);
                return;
            }
        }


        /* Add data to client's read buffer */
        if (client->read_pos + nread <= sizeof(client->read_buffer)) {
            memcpy(client->read_buffer + client->read_pos, buf->base, nread);
            client->read_pos += nread;
        } else {
            /* Buffer overflow - reset and log error */
            UVBUS_LOG_ERROR("Buffer overflow: read_pos=%zu, nread=%zd, buffer_size=%zu",  /* cppcheck-suppress invalidPrintfArgType_sint */
                    client->read_pos, nread, sizeof(client->read_buffer));
            client->read_pos = 0;
            uvrpc_free(buf->base);
            return;
        }


        /* Process complete frames */
        while (client->read_pos >= 4) {
            /* Parse frame length (big-endian) */
            uint32_t frame_size = (uint32_t)client->read_buffer[0] << 24 |
                                  (uint32_t)client->read_buffer[1] << 16 |
                                  (uint32_t)client->read_buffer[2] << 8 |
                                  (uint32_t)client->read_buffer[3];

            /* Validate frame size - stricter limit for stability */
            if (frame_size == 0 || frame_size > 64*1024) {  /* 64KB max */
                /* Invalid frame size, reset buffer */
                UVBUS_LOG_ERROR("Invalid frame size (%u), resetting buffer", frame_size);
                client->read_pos = 0;
                break;
            }

            size_t total_size = 4 + frame_size;
            if (client->read_pos < total_size) {
                /* Not enough data yet */
                break;
            }

            /* Extract frame data (skip 4-byte length prefix) */
            if (transport->recv_cb) {
                /* Copy frame data to heap so callback can safely access it */
                uint8_t* frame_copy = (uint8_t*)uvrpc_alloc(frame_size);
                if (!frame_copy) {
                    UVBUS_LOG_ERROR("Failed to allocate %u bytes for frame", frame_size);
                    if (transport->error_cb) {
                        transport->error_cb(UVBUS_ERROR_NO_MEMORY, "Frame allocation failed", transport->callback_ctx);
                    }
                    client->read_pos = 0;
                    break;
                }
                memcpy(frame_copy, client->read_buffer + 4, frame_size);

                /* Client mode: pass recv context */
                transport->recv_cb(frame_copy, frame_size, transport->recv_ctx, transport->recv_ctx);
                
                /* Always free the frame copy after callback returns
                 * This ensures cleanup even if callback forgets to free it */
                uvrpc_free(frame_copy);
            }

            /* Remove processed frame from buffer */
            size_t remaining = client->read_pos - total_size;
            if (remaining > 0 && remaining < sizeof(client->read_buffer)) {
                memmove(client->read_buffer, client->read_buffer + total_size, remaining);
            }
            client->read_pos = remaining;
        }
    }
    
    uvrpc_free(buf->base);
}

/* Client alloc callback */
static void on_client_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    (void)suggested_size;
    (void)handle;
    /* Use 64KB buffer for receiving UDP packets */
    buf->base = (char*)uvrpc_alloc(65536);
    if (buf->base) {
        buf->len = 65536;
    } else {
        buf->len = 0;
    }
}

/* Send callback */
static void on_send(uv_udp_send_t* req, int status) {
    (void)status;  /* Send errors are non-fatal for UDP (loss-tolerant transport) */
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* Close callbacks: the udp_handle is embedded in the client/server struct, so
 * the struct must survive until uv_close completes. These free the struct only
 * after the handle is fully closed, avoiding a use-after-free when the loop
 * subsequently runs. */
static void on_udp_client_close(uv_handle_t* handle) {
    uvbus_udp_client_t* client = (uvbus_udp_client_t*)handle->data;
    if (!client) return;
    uvrpc_free(client->host);
    uvrpc_free(client);
}

static void on_udp_server_close(uv_handle_t* handle) {
    /* handle->data is set to the server struct in udp_disconnect (it is
     * normally the transport for recv dispatch, but recv cannot fire once
     * closing has begun). */
    uvbus_udp_server_t* server = (uvbus_udp_server_t*)handle->data;
    if (!server) return;
    uvrpc_free(server->client_addrs);
    uvrpc_free(server->client_active);
    uvrpc_free(server->client_last_seen);
    uvrpc_free(server->host);
    uvrpc_free(server);
}

/* The timer is a separate allocation, so it can be reaped without touching
 * the server struct that may already have been freed by its own close. */
static void on_idle_timer_close(uv_handle_t* handle) {
    uvrpc_free(handle);
}

/* Shared broadcast buffer with refcount (single-threaded: all send completions
 * fire on the same event-loop thread, so no atomicity is required). */
typedef struct {
    uint8_t* data;      /* Frame data (4-byte length prefix + payload) */
    size_t size;        /* Total frame size */
    int ref_count;      /* Refcount, starts at the number of target clients */
} udp_broadcast_buf_t;

/* Broadcast send callback - decrements shared refcount */
static void on_broadcast_send(uv_udp_send_t* req, int status) {
    (void)status;
    udp_broadcast_buf_t* shared = (udp_broadcast_buf_t*)req->data;
    if (shared && --shared->ref_count == 0) {
        uvrpc_free(shared->data);
        uvrpc_free(shared);
    }
    uvrpc_free(req);
}

/* UDP vtable functions */
static int udp_listen(void* impl_ptr, const char* address);
static int udp_connect(void* impl_ptr, const char* address);
static void udp_disconnect(void* impl_ptr);
static int udp_send(void* impl_ptr, const uint8_t* data, size_t size);
static int udp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static int udp_broadcast(void* impl_ptr, const uint8_t* data, size_t size);
static void udp_free(void* impl_ptr);

/* Global vtable for UDP */
static const uvbus_transport_vtable_t udp_vtable = {
    .listen = udp_listen,
    .connect = udp_connect,
    .disconnect = udp_disconnect,
    .send = udp_send,
    .send_to = udp_send_to,
    .broadcast = udp_broadcast,
    .free = udp_free
};

/* UDP listen implementation */
static int udp_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_udp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create server */
    uvbus_udp_server_t* server = (uvbus_udp_server_t*)uvrpc_alloc(sizeof(uvbus_udp_server_t));
    if (!server) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(server, 0, sizeof(uvbus_udp_server_t));
    server->host = host;
    server->port = port;
    server->parent_transport = transport;
    server->num_clients = 0;
    
    /* Initialize client address list */
    server->max_clients = 1024;  /* Increased from 256 to 1024 */
    server->client_addrs = (struct sockaddr_storage*)uvrpc_calloc(server->max_clients, sizeof(struct sockaddr_storage));
    server->client_active = (uint8_t*)uvrpc_calloc(server->max_clients, sizeof(uint8_t));
    server->client_last_seen = (uint64_t*)uvrpc_calloc(server->max_clients, sizeof(uint64_t));
    if (!server->client_addrs || !server->client_active || !server->client_last_seen) {
        uvrpc_free(server->client_addrs);
        uvrpc_free(server->client_active);
        uvrpc_free(server->client_last_seen);
        uvrpc_free(server->host);
        uvrpc_free(server);
        return UVBUS_ERROR_NO_MEMORY;
    }
    server->idle_timeout_ms = udp_peer_idle_ms();
    
    /* Initialize UDP handle */
    uv_udp_init(transport->loop, &server->udp_handle);
    server->udp_handle.data = transport;
    
    /* Increase UDP buffer size for better performance
     * Note: These functions may not be available in older libuv versions,
     * so we use setsockopt directly as a fallback */
    int bufsize = 1024 * 1024;  /* 1MB */
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t*)&server->udp_handle, &fd) == 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    }
    
    /* Bind to address */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    int bind_err = uv_udp_bind(&server->udp_handle, (const struct sockaddr*)&addr, 0);
    if (bind_err != 0) {
        UVBUS_LOG_ERROR("Failed to bind to %s:%d: %s", host, port, uv_strerror(bind_err));
        /* udp_handle is embedded in server and was uv_udp_init'd, so it must be
         * closed (async) before the struct can be reaped — on_udp_server_close
         * frees server once the close completes. The caller must pump the loop. */
        server->udp_handle.data = server;
        uv_close((uv_handle_t*)&server->udp_handle, on_udp_server_close);
        return UVBUS_ERROR_IO;
    }

    /* Start receiving */
    int recv_err = uv_udp_recv_start(&server->udp_handle, on_server_alloc, on_server_recv);
    if (recv_err != 0) {
        UVBUS_LOG_ERROR("Failed to start receiving: %s", uv_strerror(recv_err));
        server->udp_handle.data = server;
        uv_close((uv_handle_t*)&server->udp_handle, on_udp_server_close);
        return UVBUS_ERROR_IO;
    }
    
    server->idle_timer = (uv_timer_t*)uvrpc_alloc(sizeof(uv_timer_t));
    if (!server->idle_timer) {
        server->udp_handle.data = server;
        uv_close((uv_handle_t*)&server->udp_handle, on_udp_server_close);
        return UVBUS_ERROR_NO_MEMORY;
    }
    uv_timer_init(transport->loop, server->idle_timer);
    server->idle_timer->data = server;
    int timer_err = uv_timer_start(server->idle_timer, on_peer_idle_timer,
                                   server->idle_timeout_ms, server->idle_timeout_ms);
    if (timer_err != 0) {
        uv_close((uv_handle_t*)server->idle_timer, on_idle_timer_close);
        server->udp_handle.data = server;
        uv_close((uv_handle_t*)&server->udp_handle, on_udp_server_close);
        return UVBUS_ERROR_IO;
    }

    server->is_listening = 1;
    transport->is_connected = 1;
    transport->parent_bus->is_active = 1;
    transport->impl.udp_server = (void*)server;
    
    return UVBUS_OK;
}

/* UDP connect implementation */
static int udp_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_udp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create client */
    uvbus_udp_client_t* client = (uvbus_udp_client_t*)uvrpc_alloc(sizeof(uvbus_udp_client_t));
    if (!client) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(client, 0, sizeof(uvbus_udp_client_t));
    client->host = host;
    client->port = port;
    client->parent_transport = transport;
    
    /* Initialize UDP handle */
    uv_udp_init(transport->loop, &client->udp_handle);
    client->udp_handle.data = client;  /* Set to client so on_client_recv can find it */
    
    /* Increase UDP buffer size for better performance
     * Note: These functions may not be available in older libuv versions,
     * so we use setsockopt directly as a fallback */
    int bufsize = 1024 * 1024;  /* 1MB */
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t*)&client->udp_handle, &fd) == 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    }
    
    /* Set up server address */
    uv_ip4_addr(host, port, &client->server_addr);
    
    /* Bind to local address (random port) */
    struct sockaddr_in local_addr;
    uv_ip4_addr("0.0.0.0", 0, &local_addr);
    if (uv_udp_bind(&client->udp_handle, (const struct sockaddr*)&local_addr, 0) != 0) {
        /* udp_handle is embedded in client and was uv_udp_init'd; close it
         * asynchronously and let on_udp_client_close reap the struct. */
        client->udp_handle.data = client;
        uv_close((uv_handle_t*)&client->udp_handle, on_udp_client_close);
        return UVBUS_ERROR_IO;
    }

    /* Start receiving */
    if (uv_udp_recv_start(&client->udp_handle, on_client_alloc, on_client_recv) != 0) {
        client->udp_handle.data = client;
        uv_close((uv_handle_t*)&client->udp_handle, on_udp_client_close);
        return UVBUS_ERROR_IO;
    }
    
    client->is_connected = 1;
    transport->parent_bus->is_active = 1;
    transport->is_connected = 1;
    transport->impl.udp_client = (void*)client;

    /* Send registration message so server knows our address for broadcast */
    {
        uv_udp_send_t* reg_req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
        if (reg_req) {
            char* reg_data = (char*)uvrpc_alloc(9);
            if (reg_data) {
                memcpy(reg_data, "UVRPC_REG", 9);
                uv_buf_t reg_buf = uv_buf_init(reg_data, 9);
                reg_req->data = reg_data;
                /* on_send frees both, but libuv only calls it for a request it
                 * queued -- so a synchronous failure here leaks them both,
                 * silently, in a path whose whole purpose is best-effort. */
                if (uv_udp_send(reg_req, &client->udp_handle, &reg_buf, 1,
                                (const struct sockaddr*)&client->server_addr,
                                on_send) != 0) {
                    uvrpc_free(reg_data);
                    uvrpc_free(reg_req);
                }
            } else {
                uvrpc_free(reg_req);
            }
        }
    }

    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* UDP disconnect implementation */
static void udp_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.udp_server) {
        uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
        if (server->idle_timer && !uv_is_closing((uv_handle_t*)server->idle_timer)) {
            uv_timer_stop(server->idle_timer);
            uv_close((uv_handle_t*)server->idle_timer, on_idle_timer_close);
        }
        /* Clear parent reference to prevent access during cleanup */
        server->parent_transport = NULL;
        /* Close UDP handle. The udp_handle is embedded in the server struct, so
         * the struct cannot be freed until the close completes — on_udp_server_close
         * (which reaps server) is set as handle->data and invoked once closed. */
        if (!uv_is_closing((uv_handle_t*)&server->udp_handle)) {
            server->udp_handle.data = server;  /* recv can't fire once closing begins */
            uv_close((uv_handle_t*)&server->udp_handle, on_udp_server_close);
        } else {
            /* Already closing: the close callback will reap it; nothing to do. */
        }
        transport->impl.udp_server = NULL;
    } else if (!transport->is_server && transport->impl.udp_client) {
        uvbus_udp_client_t* client = (uvbus_udp_client_t*)transport->impl.udp_client;
        /* Clear parent reference */
        client->parent_transport = NULL;
        /* Close UDP handle; client is reaped by on_udp_client_close (handle->data
         * already points to client) once the close completes. */
        if (!uv_is_closing((uv_handle_t*)&client->udp_handle)) {
            client->udp_handle.data = client;
            uv_close((uv_handle_t*)&client->udp_handle, on_udp_client_close);
        } else {
            /* Already closing: reaped by its close callback. */
        }
        transport->impl.udp_client = NULL;
    }

    transport->is_connected = 0;
}

/* UDP send implementation */
static int udp_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (transport->is_server) {
        /* UDP server needs a target for send */
        return UVBUS_ERROR_INVALID_PARAM;
    } else {
        uvbus_udp_client_t* client = (uvbus_udp_client_t*)transport->impl.udp_client;
        
        /* Allocate buffer with 4-byte frame length prefix */
        size_t total_size = 4 + size;
        uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
        if (!frame_data) {
            return UVBUS_ERROR_NO_MEMORY;
        }

        /* Big-endian 4-byte prefix + payload; see uvbus_frame.h. */
        uvbus_frame_write(frame_data, data, size);
        
        /* Client send to server */
        uv_udp_send_t* req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
        if (!req) {
            uvrpc_free(frame_data);
            return UVBUS_ERROR_NO_MEMORY;
        }
        
        uv_buf_t buf = uv_buf_init((char*)frame_data, total_size);
        req->data = frame_data;
        
        if (uv_udp_send(req, &client->udp_handle, &buf, 1,
                        (const struct sockaddr*)&client->server_addr, on_send) != 0) {
            uvrpc_free(frame_data);
            uvrpc_free(req);
            return UVBUS_ERROR_IO;
        }
    }
    
    return UVBUS_OK;
}

/* UDP send to specific target implementation */
static int udp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
    if (!server || !target) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* A stale client_ctx must never turn back into a send target after its
     * slot has expired. The recv callback only ever hands out addresses from
     * this array, so reject anything outside it or in an unused slot. */
    uintptr_t offset = (uintptr_t)target - (uintptr_t)server->client_addrs;
    if ((uintptr_t)target < (uintptr_t)server->client_addrs ||
        offset % sizeof(struct sockaddr_storage) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    size_t target_idx = offset / sizeof(struct sockaddr_storage);
    if (target_idx >= (size_t)server->num_clients || !server->client_active[target_idx]) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* target is struct sockaddr_storage*, need to cast it properly */
    struct sockaddr_storage* addr_storage = (struct sockaddr_storage*)target;
    struct sockaddr* addr = (struct sockaddr*)addr_storage;

    /* Allocate buffer with 4-byte frame length prefix */
    size_t total_size = 4 + size;
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
    if (!frame_data) {
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Big-endian 4-byte prefix + payload; see uvbus_frame.h. */
    uvbus_frame_write(frame_data, data, size);

    uv_udp_send_t* req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
    if (!req) {
        uvrpc_free(frame_data);
        return UVBUS_ERROR_NO_MEMORY;
    }

    uv_buf_t buf = uv_buf_init((char*)frame_data, total_size);
    req->data = frame_data;

    int send_result = uv_udp_send(req, &server->udp_handle, &buf, 1,
                    (const struct sockaddr*)addr, on_send);

    if (send_result != 0) {
        UVBUS_LOG_ERROR("Send failed: %s", uv_strerror(send_result));
        uvrpc_free(frame_data);
        uvrpc_free(req);
        return UVBUS_ERROR_IO;
    }

    return UVBUS_OK;
}

/* UDP broadcast implementation - point-to-point send to each known client */
static int udp_broadcast(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Reject a payload that is not there before building a frame out of it
     * -- same as the TCP transport, and the check IPC, INPROC and SAMELOOP
     * already made. uvbus_broadcast() hands the payload down untouched. */
    if (!data || size == 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
    if (!server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* No clients - nothing to do */
    if (server->active_clients == 0) {
        return UVBUS_OK;
    }

    /* Allocate shared broadcast buffer */
    udp_broadcast_buf_t* shared = (udp_broadcast_buf_t*)uvrpc_alloc(sizeof(udp_broadcast_buf_t));
    if (!shared) {
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Allocate and fill frame data once (4-byte big-endian length prefix + payload) */
    size_t total_size = 4 + size;
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
    if (!frame_data) {
        uvrpc_free(shared);
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Big-endian 4-byte prefix + payload; see uvbus_frame.h. */
    uvbus_frame_write(frame_data, data, size);

    shared->data = frame_data;
    shared->size = total_size;
    shared->ref_count = server->active_clients;

    /* Send to each known client address */
    for (int i = 0; i < server->num_clients; i++) {
        if (!server->client_active[i]) {
            continue;
        }
        uv_udp_send_t* req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
        if (!req) {
            /* Alloc failed - decrement refcount for this skipped client */
            if (--shared->ref_count == 0) {
                uvrpc_free(shared->data);
                uvrpc_free(shared);
            }
            continue;
        }

        uv_buf_t buf = uv_buf_init((char*)shared->data, shared->size);
        req->data = shared;

        struct sockaddr* addr = (struct sockaddr*)&server->client_addrs[i];
        int send_result = uv_udp_send(req, &server->udp_handle, &buf, 1, addr, on_broadcast_send);
        if (send_result != 0) {
            uvrpc_free(req);
            /* Decrement refcount for this failed send so the buffer is eventually freed */
            if (--shared->ref_count == 0) {
                uvrpc_free(shared->data);
                uvrpc_free(shared);
            }
            continue;
        }
    }

    return UVBUS_OK;
}

/* UDP free implementation */
static void udp_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    udp_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}

/* Export function to create UDP transport */
uvbus_transport_t* create_udp_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &udp_vtable;
    
    return transport;
}