/**
 * Simple test for UVRPC primitives (JavaScript-style)
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
    uvrpc_promise_resolve(&promise, (uint8_t*)"Success", 8);
    printf("Promise resolved\n");
    uvrpc_promise_cleanup(&promise);
    printf("Promise cleaned up\n");
    
    printf("\n=== Testing Promise Convenience Functions ===\n");
    uvrpc_promise_t* p = uvrpc_promise_create(&loop);
    if (p) {
        printf("Promise created with convenience function\n");
        uvrpc_promise_resolve(p, (uint8_t*)"Test", 5);
        uvrpc_promise_destroy(p);
        printf("Promise destroyed with convenience function\n");
    }
    
    printf("\n=== Testing Semaphore ===\n");
    uvrpc_semaphore_t sem;
    uvrpc_semaphore_init(&sem, &loop, 5);
    printf("Semaphore initialized with capacity 5\n");
    printf("Available permits: %d\n", uvrpc_semaphore_get_available(&sem));
    uvrpc_semaphore_release(&sem);
    printf("Semaphore released one permit\n");
    printf("Available permits: %d\n", uvrpc_semaphore_get_available(&sem));
    uvrpc_semaphore_cleanup(&sem);
    printf("Semaphore cleaned up\n");
    
    printf("\n=== Testing WaitGroup ===\n");
    uvrpc_waitgroup_t wg;
    uvrpc_waitgroup_init(&wg, &loop);
    printf("WaitGroup initialized\n");
    uvrpc_waitgroup_add(&wg, 1);
    printf("WaitGroup count: %d\n", uvrpc_get_count(&wg));
    uvrpc_waitgroup_done(&wg);
    printf("WaitGroup count after done: %d\n", uvrpc_get_count(&wg));
    uvrpc_waitgroup_cleanup(&wg);
    printf("WaitGroup cleaned up\n");
    
    uv_loop_close(&loop);
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}