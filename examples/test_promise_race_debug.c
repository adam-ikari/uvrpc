/**
 * @file test_promise_race_debug.c
 * @brief Debug Promise.race() issue
 */

#include "../include/uvrpc_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Disable output buffering */
static void init_no_buffer() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

static int race_callback_count = 0;
static int combined_callback_count = 0;

/* Callback for individual promises in race */
static void on_individual_promise(uvrpc_promise_t* promise, void* user_data) {
    race_callback_count++;
    printf("[Individual] Callback called (count=%d)\n", race_callback_count);
    
    if (uvrpc_promise_is_fulfilled(promise)) {
        printf("[Individual] Promise fulfilled\n");
    } else if (uvrpc_promise_is_rejected(promise)) {
        printf("[Individual] Promise rejected\n");
    }
}

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
    init_no_buffer();
    
    printf("=== Debugging Promise.race() ===\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Test 1: Manual test - set callback on individual promise, then resolve */
    printf("Test 1: Manual callback test\n");
    {
        uvrpc_promise_t promise1;
        uvrpc_promise_init(&promise1, &loop);
        
        printf("Setting callback on promise1\n");
        uvrpc_promise_set_callback(&promise1, on_individual_promise, NULL);
        
        printf("Resolving promise1\n");
        int value1 = 111;
        uvrpc_promise_resolve(&promise1, (uint8_t*)&value1, sizeof(int));
        
        printf("Running event loop (UV_RUN_NOWAIT mode)\n");
        for (int i = 0; i < 10; i++) {
            int ret = uv_run(&loop, UV_RUN_NOWAIT);
            printf("Iteration %d: uv_run returned %d, race_callback_count=%d\n", i, ret, race_callback_count);
            if (race_callback_count > 0) {
                break;
            }
            usleep(1000);
        }
        
        printf("Final: race_callback_count=%d\n", race_callback_count);
        
        uvrpc_promise_cleanup(&promise1);
    }
    printf("\n");
    
    /* Test 2: Promise.race() test */
    printf("Test 2: Promise.race() test\n");
    {
        race_callback_count = 0;
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
            printf("Iteration %d: uv_run returned %d, combined_callback_count=%d, race_callback_count=%d\n", 
                   i, ret, combined_callback_count, race_callback_count);
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
        
        printf("Final: race_callback_count=%d, combined_callback_count=%d\n", 
               race_callback_count, combined_callback_count);
        
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
    
    printf("=== Debug complete ===\n");
    
    uv_loop_close(&loop);
    return 0;
}
