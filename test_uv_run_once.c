/**
 * Test UV_RUN_ONCE behavior with active handles
 */

#include <uv.h>
#include <stdio.h>
#include <unistd.h>

static int timer_count = 0;
static int idle_count = 0;

void timer_callback(uv_timer_t* handle) {
    timer_count++;
    printf("Timer callback fired (count=%d)\n", timer_count);
}

void idle_callback(uv_idle_t* handle) {
    idle_count++;
    printf("Idle callback fired (count=%d)\n", idle_count);
    if (idle_count >= 5) {
        uv_idle_stop(handle);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create a timer that never fires (10 seconds) */
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    uv_timer_start(&timer, timer_callback, 10000, 0);

    printf("Testing UV_RUN_ONCE with active timer handle...\n");

    /* Try UV_RUN_ONCE - this should block until timer fires or timeout */
    for (int i = 0; i < 3; i++) {
        printf("Calling UV_RUN_ONCE (iteration %d)\n", i);
        uv_run(&loop, UV_RUN_ONCE);
        printf("UV_RUN_ONCE returned\n");
    }

    printf("\nNow testing with UV_RUN_NOWAIT...\n");

    /* Try UV_RUN_NOWAIT - this should not block */
    for (int i = 0; i < 3; i++) {
        printf("Calling UV_RUN_NOWAIT (iteration %d)\n", i);
        uv_run(&loop, UV_RUN_NOWAIT);
        printf("UV_RUN_NOWAIT returned\n");
    }

    /* Stop the timer */
    uv_timer_stop(&timer);

    printf("\nTesting UV_RUN_ONCE with idle handle...\n");

    /* Create an idle handle */
    uv_idle_t idle;
    uv_idle_init(&loop, &idle);
    uv_idle_start(&idle, idle_callback);

    /* Try UV_RUN_ONCE with idle - this should process immediately */
    for (int i = 0; i < 3; i++) {
        printf("Calling UV_RUN_ONCE (iteration %d)\n", i);
        uv_run(&loop, UV_RUN_ONCE);
        printf("UV_RUN_ONCE returned\n");
    }

    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&idle, NULL);
    uv_loop_close(&loop);

    printf("\nFinal counts: timer=%d, idle=%d\n", timer_count, idle_count);

    return 0;
}