/**
 * UVRPC Async Client with libuv Event Loop
 * Uses libuv for event loop, NNG AIO for async I/O
 */

#include <nng/nng.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* AIO context */
typedef struct aio_ctx {
    nng_aio* recv_aio;
    nng_aio* send_aio;
    nng_socket sock;
    uv_async_t async_handle;
    uv_timer_t timeout_timer;
    int sent;
    int received;
    struct timespec start_time;
    int total_requests;
    int completed;
} aio_ctx_t;

/* Send callback */
void send_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    /* Start receive after send */
    nng_socket_recv(ctx->sock, ctx->recv_aio);
}

/* Receive callback */
void recv_callback(void* arg) {
    aio_ctx_t* ctx = (aio_ctx_t*)arg;
    
    int rv = nng_aio_result(ctx->recv_aio);
    if (rv == 0) {
        nng_msg* msg = nng_aio_get_msg(ctx->recv_aio);
        if (msg) {
            nng_msg_free(msg);
            ctx->received++;
            
            /* Send next request if we haven't sent all */
            if (ctx->sent < ctx->total_requests) {
                nng_msg* new_msg;
                uint8_t data[128];
                memset(data, 'A', 128);
                
                nng_msg_alloc(&new_msg, 128);
                memcpy(nng_msg_body(new_msg), data, 128);
                
                nng_aio_set_msg(ctx->send_aio, new_msg);
                nng_socket_send(ctx->sock, ctx->send_aio);
                ctx->sent++;
            } else if (ctx->received == ctx->total_requests) {
                /* All requests completed */
                ctx->completed = 1;
                uv_stop(ctx->async_handle.loop);
            }
        }
    }
}

/* Timeout callback - just keeps loop running */
void timeout_callback(uv_timer_t* handle) {
    (void)handle;
    /* No-op, just keeps event loop alive */
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 1000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 128;
    
    printf("UVRPC Async Client with libuv\n");
    printf("Address: %s\n", addr);
    printf("Requests: %d\n", num_requests);
    printf("Payload: %d bytes\n\n", payload_size);
    
    /* Initialize libuv event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Initialize NNG with task threads */
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    params.num_task_threads = 2;
    params.max_task_threads = 2;
    nng_init(&params);
    
    /* Open REQ socket */
    nng_socket sock;
    if (nng_req0_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Connect */
    nng_dialer dialer;
    if (nng_dialer_create(&dialer, sock, addr) != 0) {
        fprintf(stderr, "Failed to create dialer\n");
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    if (nng_dialer_start(dialer, 0) != 0) {
        fprintf(stderr, "Failed to connect\n");
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Connected to server\n");
    printf("Starting benchmark...\n\n");
    
    /* Create AIO contexts */
    aio_ctx_t ctx;
    ctx.sock = sock;
    ctx.sent = 0;
    ctx.received = 0;
    ctx.total_requests = num_requests;
    ctx.completed = 0;
    
    if (nng_aio_alloc(&ctx.recv_aio, recv_callback, &ctx) != 0) {
        fprintf(stderr, "Failed to allocate recv AIO\n");
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    if (nng_aio_alloc(&ctx.send_aio, send_callback, &ctx) != 0) {
        fprintf(stderr, "Failed to allocate send AIO\n");
        nng_aio_free(ctx.recv_aio);
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Create async handle and timer */
    uv_async_init(&loop, &ctx.async_handle, NULL);
    uv_timer_init(&loop, &ctx.timeout_timer);
    uv_timer_start(&ctx.timeout_timer, timeout_callback, 1, 1);  /* 1ms interval */
    
    /* Warmup - send first request synchronously */
    uint8_t warmup_data[128];
    memset(warmup_data, 'A', 128);
    nng_msg* warmup_msg;
    nng_msg_alloc(&warmup_msg, 128);
    memcpy(nng_msg_body(warmup_msg), warmup_data, 128);
    nng_sendmsg(sock, warmup_msg, 0);
    
    nng_msg* warmup_reply;
    nng_recvmsg(sock, &warmup_reply, 0);
    nng_msg_free(warmup_reply);
    
    /* Start benchmark */
    clock_gettime(CLOCK_MONOTONIC, &ctx.start_time);
    
    /* Send first request asynchronously */
    nng_msg* first_msg;
    nng_msg_alloc(&first_msg, payload_size);
    memset(nng_msg_body(first_msg), 'A', payload_size);
    
    nng_aio_set_msg(ctx.send_aio, first_msg);
    nng_socket_send(sock, ctx.send_aio);
    ctx.sent = 1;
    
    /* Run libuv event loop */
    while (!ctx.completed) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double elapsed_ms = (end_time.tv_sec - ctx.start_time.tv_sec) * 1000.0 + 
                       (end_time.tv_nsec - ctx.start_time.tv_nsec) / 1000000.0;
    double throughput = num_requests / (elapsed_ms / 1000.0);
    
    printf("\n========== Results ==========\n");
    printf("Total time: %.2f ms\n", elapsed_ms);
    printf("Received: %d / %d\n", ctx.received, num_requests);
    printf("Throughput: %.0f ops/s\n", throughput);
    printf("Avg latency: %.3f ms\n", elapsed_ms / num_requests);
    printf("=============================\n");
    
    /* Cleanup */
    uv_timer_stop(&ctx.timeout_timer);
    uv_close((uv_handle_t*)&ctx.timeout_timer, NULL);
    uv_close((uv_handle_t*)&ctx.async_handle, NULL);
    nng_aio_stop(ctx.recv_aio);
    nng_aio_stop(ctx.send_aio);
    nng_aio_free(ctx.recv_aio);
    nng_aio_free(ctx.send_aio);
    nng_dialer_close(dialer);
    nng_socket_close(sock);
    uv_loop_close(&loop);
    
    return 0;
}
