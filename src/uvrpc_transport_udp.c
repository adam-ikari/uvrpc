/**
 * UVRPC UDP Transport Implementation
 * Connectionless transport for broadcast scenarios
 */

#include "uvrpc_transport_internal.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* UDP transport implementation */
typedef struct uvrpc_udp_transport {
    uv_loop_t* loop;
    int is_server;

    /* Handles */
    uv_udp_t handle;

    /* Peer tracking */
    udp_peer_t* peers;

    /* Read buffer */
    uint8_t read_buffer[8192];
    size_t read_pos;

    /* Current sender address (for RPC responses) */
    struct sockaddr_storage current_sender;
    int has_current_sender;

    /* Callbacks */
    uvrpc_recv_callback_t recv_cb;
    uvrpc_connect_callback_t connect_cb;
    uvrpc_error_callback_t error_cb;
    void* ctx;

    /* Status */
    int is_connected;
} uvrpc_udp_transport_t;

/* UDP virtual stream for RPC responses */
typedef struct udp_virtual_stream {
    uv_handle_t handle;
    struct sockaddr_storage sender_addr;
    uvrpc_udp_transport_t* transport;
    int is_active;
    uv_udp_t* udp_handle;  /* UDP handle for sending responses */
} udp_virtual_stream_t;

/* Forward declarations */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void recv_callback(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                         const struct sockaddr* addr, unsigned flags);
static void send_callback(uv_udp_send_t* req, int status);
static void async_callback(uv_async_t* handle);

/* Parse address string */
static int parse_udp_address(const char* addr_str, struct sockaddr_storage* addr) {
    char host[256];
    int port = 5555;

    const char* addr_to_parse = addr_str;
    if (strncmp(addr_str, "udp://", 6) == 0) {
        addr_to_parse = addr_str + 6;
    }

    if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
        if (uv_ip4_addr(host, port, (struct sockaddr_in*)addr) == 0 ||
            uv_ip6_addr(host, port, (struct sockaddr_in6*)addr) == 0) {
            return 0;
        }
    }

    return -1;
}

/* Process complete frames */
static void process_frames(uvrpc_udp_transport_t* udp) {
    while (udp->read_pos >= 4) {
        size_t frame_size = 0;
        int rv = parse_frame_length(udp->read_buffer, udp->read_pos, &frame_size);

        if (rv <= 0) break;  /* Incomplete or invalid frame */

        size_t total_size = 4 + frame_size;
        if (udp->read_pos < total_size) break;  /* Not enough data */

        /* Extract frame data */
        uint8_t* frame_data = uvrpc_alloc(frame_size);
        if (frame_data) {
            memcpy(frame_data, udp->read_buffer + 4, frame_size);

            if (udp->recv_cb) {
                udp->recv_cb(frame_data, frame_size, udp->ctx);
            }

            uvrpc_free(frame_data);
        }

        /* Remove processed frame */
        memmove(udp->read_buffer, udp->read_buffer + total_size,
                udp->read_pos - total_size);
        udp->read_pos -= total_size;
    }
}

/* Alloc callback */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)handle->data;
    buf->base = (char*)(udp->read_buffer + udp->read_pos);
    buf->len = sizeof(udp->read_buffer) - udp->read_pos;
}

/* Receive callback */
static void recv_callback(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                         const struct sockaddr* addr, unsigned flags) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)handle->data;

    if (nread < 0) {
        if (udp->error_cb) {
            udp->error_cb(nread, uv_strerror(nread), udp->ctx);
        }
        return;
    }

    if (nread == 0) return;

    /* Save sender address for RPC responses */
    if (addr) {
        memcpy(&udp->current_sender, addr, sizeof(struct sockaddr_storage));
        udp->has_current_sender = 1;
    }

    /* For client, save peer address for sending responses */
    if (!udp->is_server && addr) {
        /* Check if peer already exists */
        udp_peer_t* peer = udp->peers;
        int found = 0;
        while (peer) {
            if (memcmp(&peer->addr, addr, sizeof(struct sockaddr_storage)) == 0) {
                found = 1;
                break;
            }
            peer = peer->next;
        }

        /* Add new peer if not found */
        if (!found) {
            udp_peer_t* new_peer = uvrpc_calloc(1, sizeof(udp_peer_t));
            if (new_peer) {
                memcpy(&new_peer->addr, addr, sizeof(struct sockaddr_storage));
                new_peer->next = udp->peers;
                udp->peers = new_peer;
            }
        }
    }

    /* Process frames */
    udp->read_pos = nread;
    if (nread <= sizeof(udp->read_buffer)) {
        memcpy(udp->read_buffer, buf->base, nread);
    } else {
        fprintf(stderr, "UDP packet exceeds buffer size, truncating\n");
        memcpy(udp->read_buffer, buf->base, sizeof(udp->read_buffer));
        udp->read_pos = sizeof(udp->read_buffer);
    }

    /* Create virtual stream for RPC responses */
    if (udp->recv_cb && udp->has_current_sender) {
        udp_virtual_stream_t* virtual_stream = uvrpc_calloc(1, sizeof(udp_virtual_stream_t));
        if (virtual_stream) {
            memcpy(&virtual_stream->sender_addr, &udp->current_sender, sizeof(struct sockaddr_storage));
            virtual_stream->transport = udp;
            virtual_stream->is_active = 1;
            virtual_stream->udp_handle = &udp->handle;
            virtual_stream->handle.data = &udp->handle;  /* Set data pointer to UDP handle */

            /* Process frames with virtual stream context */
            while (udp->read_pos >= 4) {
                size_t frame_size = 0;
                int rv = parse_frame_length(udp->read_buffer, udp->read_pos, &frame_size);

                if (rv <= 0) break;

                size_t total_size = 4 + frame_size;
                if (udp->read_pos < total_size) break;

                uint8_t* frame_data = uvrpc_alloc(frame_size);
                if (frame_data) {
                    memcpy(frame_data, udp->read_buffer + 4, frame_size);
                    udp->recv_cb(frame_data, frame_size, (uv_stream_t*)virtual_stream);
                    uvrpc_free(frame_data);
                }

                memmove(udp->read_buffer, udp->read_buffer + total_size,
                        udp->read_pos - total_size);
                udp->read_pos -= total_size;
            }

            uvrpc_free(virtual_stream);
        } else {
            /* Fallback: process frames without virtual stream */
            process_frames(udp);
        }
    } else {
        process_frames(udp);
    }
}

/* Send callback */
static void send_callback(uv_udp_send_t* req, int status) {
    if (status < 0) {
        fprintf(stderr, "UDP send error: %s\n", uv_strerror(status));
    }
    if (req->data) {
        uvrpc_free(req->data);
    }
    uvrpc_free(req);
}

/* Async callback */
static void async_callback(uv_async_t* handle) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)handle->data;
    if (udp->connect_cb) {
        udp->connect_cb(0, udp->ctx);
        udp->connect_cb = NULL;
    }
}

/* Listen implementation (server) */
static int udp_listen(void* impl, const char* address,
                      uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    udp->recv_cb = recv_cb;
    udp->ctx = ctx;

    struct sockaddr_storage addr;
    if (parse_udp_address(address, &addr) != 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    printf("[UDP] Binding to %s\n", address);

    if (uv_udp_bind(&udp->handle, (const struct sockaddr*)&addr, 0) != 0) {
        return UVRPC_ERROR;
    }

    if (uv_udp_recv_start(&udp->handle, alloc_callback, recv_callback) != 0) {
        return UVRPC_ERROR;
    }

    printf("[UDP] Listening on %s\n", address);
    return UVRPC_OK;
}

/* Connect implementation (client) */
static int udp_connect(void* impl, const char* address,
                       uvrpc_connect_callback_t connect_cb,
                       uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    udp->connect_cb = connect_cb;
    udp->recv_cb = recv_cb;
    udp->ctx = ctx;

    struct sockaddr_storage addr;
    if (parse_udp_address(address, &addr) != 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Add peer address */
    udp_peer_t* new_peer = uvrpc_calloc(1, sizeof(udp_peer_t));
    if (!new_peer) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(&new_peer->addr, &addr, sizeof(struct sockaddr_storage));
    new_peer->next = udp->peers;
    udp->peers = new_peer;

    udp->is_connected = 1;

    /* Start receiving */
    if (uv_udp_recv_start(&udp->handle, alloc_callback, recv_callback) != 0) {
        return UVRPC_ERROR;
    }

    /* Trigger async callback */
    if (connect_cb) {
        udp->connect_cb = connect_cb;
        udp->ctx = ctx;
        uv_async_send((uv_async_t*)&udp->handle);
    }

    printf("[UDP] Connected to %s\n", address);
    return UVRPC_OK;
}

/* Disconnect implementation */
static void udp_disconnect(void* impl) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    if (udp->is_connected) {
        uv_udp_recv_stop(&udp->handle);
        udp->is_connected = 0;
    }
}

/* Send implementation */
static void udp_send(void* impl, const uint8_t* data, size_t size) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    if (!udp->is_connected && !udp->peers) {
        fprintf(stderr, "UDP: Cannot send, no peers\n");
        return;
    }

    size_t total_size;
    uint8_t* buffer = create_length_prefixed_buffer(data, size, &total_size);
    if (!buffer) return;

    uv_buf_t buf = uv_buf_init((char*)buffer, total_size);

    /* Send to all peer addresses */
    udp_peer_t* peer = udp->peers;
    int peer_count = 0;
    while (peer && peer_count < UVRPC_MAX_UDP_PEERS) {
        peer_count++;
        peer = peer->next;
    }

    if (peer_count > 0 && peer_count <= UVRPC_MAX_UDP_PEERS) {
        /* For single peer, send directly */
        if (peer_count == 1) {
            uv_udp_send_t* req = uvrpc_alloc(sizeof(uv_udp_send_t));
            if (req) {
                req->data = buffer;
                uv_udp_send(req, &udp->handle, &buf, 1,
                            (struct sockaddr*)&udp->peers->addr, send_callback);
            } else {
                uvrpc_free(buffer);
            }
        } else {
            /* For multiple peers, send to each (create separate buffers) */
            peer = udp->peers;
            while (peer) {
                uint8_t* peer_buffer = uvrpc_alloc(total_size);
                if (peer_buffer) {
                    memcpy(peer_buffer, buffer, total_size);
                    uv_udp_send_t* req = uvrpc_alloc(sizeof(uv_udp_send_t));
                    if (req) {
                        req->data = peer_buffer;
                        uv_buf_t peer_buf = uv_buf_init((char*)peer_buffer, total_size);
                        uv_udp_send(req, &udp->handle, &peer_buf, 1,
                                    (struct sockaddr*)&peer->addr, send_callback);
                    } else {
                        uvrpc_free(peer_buffer);
                    }
                }
                peer = peer->next;
            }
            uvrpc_free(buffer);
        }
    } else {
        uvrpc_free(buffer);
    }
}

/* Send to specific target (same as send for UDP) */
static void udp_send_to(void* impl, const uint8_t* data, size_t size, void* target) {
    /* UDP is broadcast-only, send_to behaves like send */
    udp_send(impl, data, size);
}

/* Send to specific address (for RPC responses) */
static void send_to_addr(uvrpc_udp_transport_t* udp, const struct sockaddr_storage* addr,
                        const uint8_t* data, size_t size) {
    size_t total_size = 4 + size;
    uint8_t* buffer = uvrpc_alloc(total_size);
    if (!buffer) return;

    buffer[0] = (size >> 24) & 0xFF;
    buffer[1] = (size >> 16) & 0xFF;
    buffer[2] = (size >> 8) & 0xFF;
    buffer[3] = size & 0xFF;
    memcpy(buffer + 4, data, size);

    uv_buf_t buf = uv_buf_init((char*)buffer, total_size);

    uv_udp_send_t* req = uvrpc_alloc(sizeof(uv_udp_send_t));
    if (req) {
        req->data = buffer;
        uv_udp_send(req, &udp->handle, &buf, 1,
                   (struct sockaddr*)addr, send_callback);
    } else {
        uvrpc_free(buffer);
    }
}

/* Check if stream is UDP virtual stream and send response */
int uvrpc_transport_udp_send_response(void* transport, uv_stream_t* stream,
                                     const uint8_t* data, size_t size) {
    if (!transport || !stream || !data || size == 0) return -1;

    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)transport;

    /* Check if this is a UDP virtual stream */
    if (stream->data && stream->data != stream) {
        udp_virtual_stream_t* vstream = (udp_virtual_stream_t*)stream;
        if (vstream->is_active && vstream->transport == udp) {
            send_to_addr(udp, &vstream->sender_addr, data, size);
            return 0;
        }
    }

    return -1;
}

/* Free implementation */
static void udp_free(void* impl) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    if (!udp) return;

    /* Clean up peer addresses */
    udp_peer_t* peer = udp->peers;
    while (peer) {
        udp_peer_t* next = peer->next;
        uvrpc_free(peer);
        peer = next;
    }
    udp->peers = NULL;

    /* Close handle */
    if (!uv_is_closing((uv_handle_t*)&udp->handle)) {
        uv_close((uv_handle_t*)&udp->handle, NULL);
    }

    uvrpc_free(udp);
}

/* Set timeout implementation (not applicable for UDP) */
static int udp_set_timeout(void* impl, uint64_t timeout_ms) {
    /* UDP is connectionless, timeout not applicable */
    return UVRPC_OK;
}

/* UDP send to specific address */
static void udp_send_to(void* impl, const uint8_t* data, size_t size, void* stream_ctx) {
    uvrpc_udp_transport_t* udp = (uvrpc_udp_transport_t*)impl;

    if (!udp || !data || size == 0) return;

    /* Check if stream_ctx is a virtual stream (for RPC responses) */
    if (stream_ctx) {
        udp_virtual_stream_t* vstream = (udp_virtual_stream_t*)stream_ctx;
        if (vstream->is_active) {
            send_to_addr(udp, &vstream->sender_addr, data, size);
            return;
        }
    }

    /* Default: broadcast to all peers */
    send_to_all(udp, data, size);
}

/* Virtual function table */
static const uvrpc_transport_vtable_t udp_vtable = {
    .listen = udp_listen,
    .connect = udp_connect,
    .disconnect = udp_disconnect,
    .send = udp_send,
    .send_to = udp_send_to,
    .free = udp_free,
    .set_timeout = udp_set_timeout
};

/* Create UDP transport */
uvrpc_transport_t* uvrpc_transport_udp_new(uv_loop_t* loop, int is_server) {
    uvrpc_udp_transport_t* udp = uvrpc_calloc(1, sizeof(uvrpc_udp_transport_t));
    if (!udp) return NULL;

    udp->loop = loop;
    udp->is_server = is_server;
    udp->is_connected = 0;
    udp->read_pos = 0;
    udp->peers = NULL;

    /* Initialize handle */
    uv_udp_init(loop, &udp->handle);
    udp->handle.data = udp;

    /* Create transport wrapper */
    uvrpc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) {
        udp_free(udp);
        return NULL;
    }

    transport->loop = loop;
    transport->type = UVRPC_TRANSPORT_UDP;
    transport->impl = udp;
    transport->vtable = &udp_vtable;
    transport->is_server = is_server;

    return transport;
}
