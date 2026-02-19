/**
 * @file test_promise_race_only.c
 * @brief Test only Promise.race() without other tests
 */

#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int combined_callback_count = 0;

/* Callback for combined promise */
static void on_combined_promise(uvrpc_promise_t* promise, void* user_data) {
    combined_callback_count++;
    printf("[Combined] Callback called (count=%d)\n", combined_callback_count);
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("[Combined] Promise fulfilled!\n");
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        uvrpc_promise_get_result(promise, &result, &result_size);
        
        if (result && result_size == sizeof(int)) {
            int value = *(int*)result;
            printf("[Combined] Result: %d\n", value);
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        const char* error = uvrpc_promise_get_error(promise);
        printf("[Combined] Rejected: %s\n", error);
    }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    
    printf("=== Test only Promise.race() ===\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("Test: Promise.race()\n");
    {
        combined_callback_count = 0;
        
        uvrpc_promise_t* promises[3];
        uvrpc_promise_t combined;
        
        /* Initialize promises */
        for (int i = 0; i < 3; i++) {
            promises[i] = (uvrpc_promise_t*)malloc(sizeof(uvrpc_promise_t));
            uvrpc_promise_init(promises[i], &loop);
            printf("Promise %d initialized\n", i);
        }
        uvrpc_promise_init(&combined, &loop);
        printf("Combined promise initialized\n");
        
        /* Set callback for combined promise */
        printf("Setting callback on combined promise\n");
        uvrpc_promise_set_callback(&combined, on_combined_promise, NULL);
        
        /* Set up Promise.race() */
        printf("Setting up Promise.race()\n");
        int ret = uvrpc_promise_race(promises, 3, &combined, &loop);
        printf("Promise.race() returned: %d\n", ret);
        
        /* Resolve first promise */
        printf("Resolving promise 0\n");
        int value = 999;
        uvrpc_promise_resolve(promises[0], (uint8_t*)&value, sizeof(int));
        
        /* Run event loop */
        printf("Running event loop (UV_RUN_NOWAIT mode)\n");
        for (int i = 0; i < 100; i++) {
            int ret = uv_run(&loop, UV_RUN_NOWAIT);
            printf("Iteration %d: uv_run returned %d, combined_callback_count=%d\n", 
                   i, ret, combined_callback_count);
            if (combined_callback_count > 0) {
                printf("Combined callback triggered at iteration %d\n", i);
                break;
            }
            if (ret == 0) {
                printf("Event loop has no more work\n");
                break;
            }
            usleep(1000);
        }
        
        printf("Final: combined_callback_count=%d\n", combined_callback_count);
        
        /* Cleanup */
        for (int i = 0; i < 3; i++) {
            if (uvrpc_promise_is_pending(promises[i])) {
                int dummy = 0;
                uvrpc_promise_resolve(promises[i], (uint8_t*)&dummy, sizeof(int));
            }
            uvrpc_promise_cleanup(promises[i]);
            free(promises[i]);
        }
        uvrpc_promise_cleanup(&combined);
    }
    printf("\n");
    
    printf("=== Test complete ===\n");
    
    uv_loop_close(&loop);
    return 0;
}
