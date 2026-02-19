/**
 * @file test_promise_basic.c
 * @brief Basic Promise test
 */

#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int callback_called = 0;

static void on_promise_complete(uvrpc_promise_t* promise, void* user_data) {
    printf("Callback called!\n");
    callback_called = 1;
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("Promise fulfilled!\n");
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        uvrpc_promise_get_result(promise, &result, &result_size);
        
        if (result && result_size == sizeof(int)) {
            int value = *(int*)result;
            printf("Result: %d\n", value);
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("Promise rejected: %s\n", error);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("Testing basic Promise functionality...\n");
    
    uvrpc_promise_t promise;
    uvrpc_promise_init(&promise, &loop);
    
    printf("Promise initialized\n");
    printf("Promise state: %d (pending=%d, fulfilled=%d, rejected=%d)\n", 
           promise.state, UVRPC_PROMISE_PENDING, UVRPC_PROMISE_FULFILLED, UVRPC_PROMISE_REJECTED);
    
    /* Set callback */
    uvrpc_promise_set_callback(&promise, on_promise_complete, NULL);
    printf("Callback set\n");
    
    /* Resolve promise */
    int value = 42;
    printf("Resolving promise with value %d\n", value);
    int ret = uvrpc_promise_resolve(&promise, (uint8_t*)&value, sizeof(int));
    printf("Resolve returned: %d\n", ret);
    
    printf("Promise state after resolve: %d\n", promise.state);
    printf("Is fulfilled: %d\n", uvrpc_promise_is_fulfilled(&promise));
    printf("Callback called: %d\n", callback_called);
    
    /* Run event loop */
    printf("Running event loop...\n");
    int iterations = 0;
    while (!callback_called && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(10000);
        iterations++;
        printf("Iteration %d, callback_called=%d\n", iterations, callback_called);
    }
    
    printf("Final: callback_called=%d\n", callback_called);
    
    uvrpc_promise_cleanup(&promise);
    uv_loop_close(&loop);
    
    return 0;
}
