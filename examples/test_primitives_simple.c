/**
 * Simple test for UVRPC primitives
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("=== Testing uvrpc_strerror ===\n");
    printf("UVRPC_OK: %s\n", uvrpc_strerror(UVRPC_OK));
    printf("UVRPC_ERROR: %s\n", uvrpc_strerror(UVRPC_ERROR));
    printf("UVRPC_ERROR_CALLBACK_LIMIT: %s\n", uvrpc_strerror(UVRPC_ERROR_CALLBACK_LIMIT));
    printf("UVRPC_ERROR_TIMEOUT: %s\n", uvrpc_strerror(UVRPC_ERROR_TIMEOUT));
    
    printf("\n=== Testing Promise ===\n");
    uvrpc_promise_t promise;
    uvrpc_promise_init(&promise, &loop);
    printf("Promise initialized\n");
    uvrpc_promise_set_result(&promise, "Success", 8);
    printf("Promise result set\n");
    uvrpc_promise_free(&promise);
    printf("Promise freed\n");
    
    printf("\n=== Testing Semaphore ===\n");
    uvrpc_semaphore_t sem;
    uvrpc_semaphore_init(&sem, &loop, 5);
    printf("Semaphore initialized with capacity 5\n");
    uvrpc_semaphore_free(&sem);
    printf("Semaphore freed\n");
    
    printf("\n=== Testing Barrier ===\n");
    uvrpc_barrier_t barrier;
    uvrpc_barrier_init(&barrier, &loop, 3, NULL, NULL);
    printf("Barrier initialized with count 3\n");
    uvrpc_barrier_free(&barrier);
    printf("Barrier freed\n");
    
    printf("\n=== Testing WaitGroup ===\n");
    uvrpc_waitgroup_t wg;
    uvrpc_waitgroup_init(&wg, &loop);
    printf("WaitGroup initialized\n");
    uvrpc_waitgroup_add(&wg, 1);
    uvrpc_waitgroup_done(&wg);
    printf("WaitGroup add/done tested\n");
    uvrpc_waitgroup_free(&wg);
    printf("WaitGroup freed\n");
    
    uv_loop_close(&loop);
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}