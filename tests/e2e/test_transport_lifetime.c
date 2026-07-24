/**
 * Transport lifetime / teardown ordering test.
 *
 * Verifies that freeing a server transport BEFORE its connected client
 * transport does not use-after-free or double-free (a regression that
 * previously existed in INPROC and SAMELOOP). Exercises both transports on
 * both the same-loop transports. Run under ASAN for full effect.
 */

#include <stdio.h>
#include <string.h>
#include <uv.h>
#include "../../include/uvrpc.h"
#include "../../include/uvbus.h"

static int got=0;
static void s_recv(const uint8_t* d, size_t s, void* c, void* sx){(void)d;(void)s;(void)c;(void)sx;got++;}
static void c_recv(const uint8_t* d, size_t s, void* c, void* sx){(void)d;(void)s;(void)c;(void)sx;got++;}

int main(void){
    uv_loop_t loop={0};
    uv_loop_init(&loop);

    /* INPROC server + client */
    uvbus_config_t* sc=uvbus_config_new();
    uvbus_config_set_loop(sc,&loop);
    uvbus_config_set_address(sc,"inproc://uaf");
    uvbus_config_set_transport(sc,UVBUS_TRANSPORT_INPROC);
    uvbus_config_set_recv_callback(sc,s_recv,NULL);
    uvbus_t* sb=uvbus_server_new(sc);

    uvbus_config_t* cc=uvbus_config_new();
    uvbus_config_set_loop(cc,&loop);
    uvbus_config_set_address(cc,"inproc://uaf");
    uvbus_config_set_transport(cc,UVBUS_TRANSPORT_INPROC);
    uvbus_config_set_recv_callback(cc,c_recv,NULL);
    uvbus_t* cb=uvbus_client_new(cc);

    uvbus_listen(sb);
    uvbus_connect(cb);
    /* free server FIRST, then client (the UAF ordering) */
    uvbus_free(sb);
    uvbus_free(cb);
    uvbus_config_free(sc);
    uvbus_config_free(cc);
    /* run loop to process any pending close callbacks */
    for(int i=0;i<10;i++) uv_run(&loop,UV_RUN_NOWAIT);
    uv_loop_close(&loop);
    printf("INPROC server-freed-first: OK (no crash/double-free)\n");

    /* SAMELOOP same test */
    uv_loop_t loop2={0};
    uv_loop_init(&loop2);
    uvbus_config_t* s2=uvbus_config_new();
    uvbus_config_set_loop(s2,&loop2);
    uvbus_config_set_address(s2,"sameloop://uaf");
    uvbus_config_set_transport(s2,UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(s2,s_recv,NULL);
    uvbus_t* sb2=uvbus_server_new(s2);
    uvbus_config_t* c2=uvbus_config_new();
    uvbus_config_set_loop(c2,&loop2);
    uvbus_config_set_address(c2,"sameloop://uaf");
    uvbus_config_set_transport(c2,UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(c2,c_recv,NULL);
    uvbus_t* cb2=uvbus_client_new(c2);
    uvbus_listen(sb2);
    uvbus_connect(cb2);
    uvbus_free(sb2);   /* server first */
    uvbus_free(cb2);   /* then client */
    uvbus_config_free(s2);
    uvbus_config_free(c2);
    for(int i=0;i<10;i++) uv_run(&loop2,UV_RUN_NOWAIT);
    uv_loop_close(&loop2);
    printf("SAMELOOP server-freed-first: OK (no crash/double-free)\n");
    return 0;
}
