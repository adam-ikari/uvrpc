#include <zmq.h>
#include <stdio.h>
#include <string.h>

int main() {
    /* 创建 ZMQ context */
    void* ctx = zmq_ctx_new();
    
    /* 创建 REQ socket */
    void* req = zmq_socket(ctx, ZMQ_REQ);
    int rc = zmq_connect(req, "tcp://127.0.0.1:5555");
    if (rc != 0) {
        fprintf(stderr, "Connect failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }
    
    printf("Simple ZMQ REQ client sending...\n");
    fflush(stdout);
    
    /* 发送请求 */
    const char* msg = "Hello from client";
    zmq_send(req, msg, strlen(msg), 0);
    printf("Sent: %s\n", msg);
    fflush(stdout);
    
    /* 接收响应 */
    char buffer[1024];
    int size = zmq_recv(req, buffer, sizeof(buffer), 0);
    printf("Received %d bytes: %.*s\n", size, size, buffer);
    fflush(stdout);
    
    /* 清理 */
    zmq_close(req);
    zmq_ctx_term(ctx);
    
    printf("Client done\n");
    return 0;
}