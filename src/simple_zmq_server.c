#include <zmq.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    /* 创建 ZMQ context */
    void* ctx = zmq_ctx_new();
    printf("Context created: %p\n", (void*)ctx);
    fflush(stdout);
    
    /* 创建 REP socket */
    void* rep = zmq_socket(ctx, ZMQ_REP);
    printf("Socket created: %p\n", (void*)rep);
    fflush(stdout);
    
    int rc = zmq_bind(rep, "tcp://*:5555");
    printf("Bind returned: %d (%s)\n", rc, rc == 0 ? "OK" : zmq_strerror(zmq_errno()));
    fflush(stdout);
    
    if (rc != 0) {
        fprintf(stderr, "Bind failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }
    
    printf("Simple ZMQ REP server waiting...\n");
    fflush(stdout);
    
    /* 接收请求 */
    char buffer[1024];
    printf("Waiting for recv...\n");
    fflush(stdout);
    int size = zmq_recv(rep, buffer, sizeof(buffer), 0);
    printf("Received %d bytes: %.*s\n", size, size, buffer);
    fflush(stdout);
    
    /* 发送响应 */
    const char* reply = "Reply from server";
    zmq_send(rep, reply, strlen(reply), 0);
    printf("Sent reply\n");
    fflush(stdout);
    
    /* 清理 */
    zmq_close(rep);
    zmq_ctx_term(ctx);
    
    printf("Server done\n");
    return 0;
}