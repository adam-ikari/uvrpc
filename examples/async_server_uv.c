/**
 * UVRPC Async Server with libuv Event Loop
 * Uses libuv for event loop, NNG AIO for async I/O
 */

#include <nng/nng.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* AIO context */
typedef struct aio_ctx {
    nng_aio* recv_aio;
    nng_aio* send_aio;
    nng_socket sock;
    uv_async_t async_handle;
    int active;
} aio_ctx_t;

/* Send callback */
void send_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    /* Start next receive */
    nng_socket_recv(ctx->sock, ctx->recv_aio);
}

/* Receive callback */
void recv_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    int rv = nng_aio_result(ctx->recv_aio);
    if (rv == 0) {
        nng_msg* msg = nng_aio_get_msg(ctx->recv_aio);
        if (msg) {
            /* Echo back the message */
            nng_aio_set_msg(ctx->send_aio, msg);
            nng_socket_send(ctx->sock, ctx->send_aio);
        } else {
            /* Start next receive */
            nng_socket_recv(ctx->sock, ctx->recv_aio);
        }
    } else if (rv == NNG_ECLOSED) {
        ctx->active = 0;
        uv_stop(ctx->async_handle.loop);
    }
}

/* Async callback - triggers when signal received */
void async_callback(uv_async_t* handle) {
    (void)handle;
    g_running = 0;
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Async Server with libuv\n");
    printf("Address: %s\n\n", addr);
    
    /* Initialize libuv event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Initialize NNG with task threads */
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    params.num_task_threads = 2;
    params.max_task_threads = 2;
    nng_init(&params);
    
    /* Open REP socket */
    nng_socket sock;
    if (nng_rep0_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Listen */
    nng_listener listener;
    if (nng_listener_create(&listener, sock, addr) != 0) {
        fprintf(stderr, "Failed to create listener\n");
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    if (nng_listener_start(listener, 0) != 0) {
        fprintf(stderr, "Failed to start listener\n");
        nng_listener_close(listener);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Server started. Press Ctrl+C to stop\n\n");
    
    /* Create AIO contexts */
    aio_ctx_t ctx;
    ctx.sock = sock;
    ctx.active = 1;
    
    if (nng_aio_alloc(&ctx.recv_aio, recv_callback, &ctx) != 0) {
        fprintf(stderr, "Failed to allocate recv AIO\n");
        nng_listener_close(listener);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    if (nng_aio_alloc(&ctx.send_aio, send_callback, &ctx) != 0) {
        fprintf(stderr, "Failed to allocate send AIO\n");
        nng_aio_free(ctx.recv_aio);
        nng_listener_close(listener);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Create async handle for signal */
    uv_async_init(&loop, &ctx.async_handle, async_callback);
    
    /* Start receiving */
    nng_socket_recv(sock, ctx.recv_aio);
    
    /* Run libuv event loop */
    printf("Running event loop...\n");
    while (g_running && ctx.active) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    printf("\nStopping server...\n");
    
    /* Cleanup */
    uv_close((uv_handle_t*)&ctx.async_handle, NULL);
    nng_aio_stop(ctx.recv_aio);
    nng_aio_stop(ctx.send_aio);
    nng_aio_free(ctx.recv_aio);
    nng_aio_free(ctx.send_aio);
    nng_listener_close(listener);
    nng_socket_close(sock);
    uv_loop_close(&loop);
    
    printf("Server stopped\n");
    return 0;
}