/**
 * Debug version to find the hang point
 */

#include <nng/nng.h>
#include <stdio.h>
#include <pthread.h>

static volatile int g_server_running = 1;

void* server_thread(void* arg) {
    printf("Server thread started\n");
    fflush(stdout);
    
    nng_socket sock;
    if (nng_pair1_open(&sock) != 0) {
        printf("Server: Failed to open socket\n");
        return NULL;
    }
    
    nng_listener listener;
    if (nng_listener_create(&listener, sock, "inproc://test") != 0) {
        printf("Server: Failed to create listener\n");
        nng_socket_close(sock);
        return NULL;
    }
    
    if (nng_listener_start(listener, 0) != 0) {
        printf("Server: Failed to start listener\n");
        nng_listener_close(listener);
        nng_socket_close(sock);
        return NULL;
    }
    
    printf("Server: Listening...\n");
    fflush(stdout);
    
    int count = 0;
    while (g_server_running && count < 10) {
        nng_msg* msg = NULL;
        printf("Server: Waiting for message %d...\n", count);
        fflush(stdout);
        
        int rv = nng_recvmsg(sock, &msg, 0);
        printf("Server: recvmsg returned %d\n", rv);
        fflush(stdout);
        
        if (rv == 0) {
            printf("Server: Echoing message\n");
            fflush(stdout);
            nng_sendmsg(sock, msg, 0);
            count++;
        } else if (rv == NNG_ECLOSED || rv == NNG_ECONNRESET) {
            break;
        }
    }
    
    printf("Server: Exiting\n");
    fflush(stdout);
    
    nng_listener_close(listener);
    nng_socket_close(sock);
    return NULL;
}

int main() {
    printf("Main: Starting\n");
    fflush(stdout);
    
    nng_init_params params = {0};
    nng_init(&params);
    
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, NULL);
    sleep(1);
    
    printf("Main: Creating client socket\n");
    fflush(stdout);
    
    nng_socket sock;
    if (nng_pair1_open(&sock) != 0) {
        fprintf(stderr, "Failed to open client socket\n");
        return 1;
    }
    
    nng_dialer dialer;
    if (nng_dialer_create(&dialer, sock, "inproc://test") != 0) {
        fprintf(stderr, "Failed to create dialer\n");
        return 1;
    }
    
    printf("Main: Connecting...\n");
    fflush(stdout);
    
    if (nng_dialer_start(dialer, 0) != 0) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }
    
    printf("Main: Sending message\n");
    fflush(stdout);
    
    nng_msg* msg;
    nng_msg_alloc(&msg, 5);
    memcpy(nng_msg_body(msg), "hello", 5);
    nng_sendmsg(sock, msg, 0);
    
    printf("Main: Waiting for reply\n");
    fflush(stdout);
    
    nng_recvmsg(sock, &msg, 0);
    printf("Main: Received reply\n");
    fflush(stdout);
    
    nng_msg_free(msg);
    nng_dialer_close(dialer);
    nng_socket_close(sock);
    
    printf("Main: Stopping server\n");
    fflush(stdout);
    
    g_server_running = 0;
    pthread_join(tid, NULL);
    
    printf("Main: Done\n");
    return 0;
}