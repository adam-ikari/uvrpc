#include <zmq.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* ROUTER 服务器 */
void* server_thread(void* arg) {
    (void)arg;
    void* context = zmq_ctx_new();
    void* router = zmq_socket(context, ZMQ_ROUTER);
    zmq_bind(router, "tcp://127.0.0.1:6004");
    
    printf("ROUTER server running...\n");
    
    while (1) {
        /* 接收多部分消息 */
        char routing_id[256];
        zmq_recv(router, routing_id, sizeof(routing_id), 0);
        printf("Server: Received routing_id (%d bytes)\n", (int)strlen(routing_id));
        
        char data[256];
        zmq_recv(router, data, sizeof(data), 0);
        printf("Server: Received data: %s\n", data);
        
        /* 发送响应 */
        zmq_send(router, routing_id, strlen(routing_id), ZMQ_SNDMORE);
        zmq_send(router, data, strlen(data), 0);
        printf("Server: Sent response\n");
    }
    
    zmq_close(router);
    zmq_ctx_term(context);
    return NULL;
}

/* DEALER 客户端 */
void client_test() {
    void* context = zmq_ctx_new();
    void* dealer = zmq_socket(context, ZMQ_DEALER);
    
    /* 设置身份 */
    zmq_setsockopt(dealer, ZMQ_IDENTITY, "client-1", 8);
    
    sleep(1);
    zmq_connect(dealer, "tcp://127.0.0.1:6004");
    
    printf("DEALER client connected\n");
    
    /* 发送请求 */
    const char* msg = "Hello, ROUTER/DEALER!";
    zmq_send(dealer, msg, strlen(msg), 0);
    printf("Client: Sent message\n");
    
    /* 接收响应 */
    char recv[256];
    zmq_recv(dealer, recv, sizeof(recv), 0);
    printf("Client: Received: %s\n", recv);
    
    zmq_close(dealer);
    zmq_ctx_term(context);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, server_thread, NULL);
    
    sleep(1);
    client_test();
    
    return 0;
}