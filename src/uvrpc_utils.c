#include "uvrpc_internal.h"
#include <unistd.h>
#include <time.h>

/**
 * 获取错误描述
 * @param error_code 错误码
 * @return 错误描述字符串
 */
const char* uvrpc_strerror(int error_code) {
    switch (error_code) {
        case UVRPC_OK:
            return "Success";
        case UVRPC_ERROR:
            return "General error";
        case UVRPC_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case UVRPC_ERROR_NO_MEMORY:
            return "Out of memory";
        case UVRPC_ERROR_SERVICE_NOT_FOUND:
            return "Service not found";
        case UVRPC_ERROR_TIMEOUT:
            return "Operation timeout";
        case UVRPC_ERROR_NOT_FOUND:
            return "Not found";
        default:
            return "Unknown error";
    }
}

/**
 * 获取模式名称
 * @param mode RPC 模式
 * @return 模式名称字符串
 */
const char* uvrpc_mode_name(uvrpc_mode_t mode) {
    switch (mode) {
        case UVRPC_MODE_REQ_REP:
            return "REQ_REP";
        case UVRPC_MODE_ROUTER_DEALER:
            return "ROUTER_DEALER";
        case UVRPC_MODE_PUB_SUB:
            return "PUB_SUB";
        case UVRPC_MODE_PUSH_PULL:
            return "PUSH_PULL";
        default:
            return "UNKNOWN";
    }
}

/**
 * 自适应事件循环运行 - 平衡性能和功耗
 * 
 * 单线程自适应调度策略：
 * - 持续监测事件循环活跃度（连续空闲迭代次数）
 * - 高负载（连续空闲 < 阈值）：使用 UV_RUN_ONCE 快速处理
 * - 低负载（连续空闲 >= 阈值）：使用 UV_RUN_NOWAIT + 休眠降低功耗
 * - 动态调整，无需用户干预
 * 
 * @param loop libuv 事件循环
 * @param timeout_ms 超时时间（毫秒），0 表示无限期运行
 * @param check_fn 可选的检查函数，返回非 0 时退出循环
 * @param user_ctx 传递给检查函数的用户上下文
 * @return 0 表示正常退出，-1 表示超时
 */
int uvrpc_loop_run_adaptive(uv_loop_t* loop, int timeout_ms, 
                             int (*check_fn)(void*), void* user_ctx) {
    if (!loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    struct timespec start_time;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    int consecutive_idle = 0;  /* 连续空闲迭代计数 */
    const int idle_threshold = UVRPC_ADAPTIVE_IDLE_THRESHOLD;  /* 空闲阈值 */
    const int busy_threshold = UVRPC_ADAPTIVE_BUSY_THRESHOLD;   /* 忙碌阈值 */
    
    while (1) {
        /* 检查用户定义的退出条件 */
        if (check_fn && check_fn(user_ctx)) {
            return 0;
        }

        /* 检查超时 */
        if (timeout_ms > 0) {
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - start_time.tv_nsec) / 1000000;
            if (elapsed_ms >= timeout_ms) {
                return -1;  /* 超时 */
            }
        }

        /* 根据活跃度选择运行模式 */
        if (consecutive_idle < idle_threshold) {
            /* 高负载模式：使用 UV_RUN_ONCE 快速处理事件 */
            /* UV_RUN_ONCE 会阻塞直到至少有一个事件被处理 */
            int result = uv_run(loop, UV_RUN_ONCE);
            if (result == 0) {
                /* 没有事件被处理，增加空闲计数 */
                consecutive_idle++;
            } else {
                /* 有事件被处理，重置空闲计数 */
                consecutive_idle = 0;
            }
        } else {
            /* 低负载模式：使用 UV_RUN_NOWAIT + 休眠降低功耗 */
            /* UV_RUN_NOWAIT 非阻塞，立即返回 */
            int result = uv_run(loop, UV_RUN_NOWAIT);
            
            if (result == 0) {
                /* 持续空闲，增加休眠时间 */
                consecutive_idle++;
                if (consecutive_idle > idle_threshold * 2) {
                    /* 持续空闲，增加休眠时间 */
                    usleep(UVRPC_ADAPTIVE_SLEEP_US * 2);
                } else {
                    usleep(UVRPC_ADAPTIVE_SLEEP_US);
                }
            } else {
                /* 有事件被处理，立即切换回高负载模式 */
                consecutive_idle = 0;
            }
        }

        /* 检查是否有活动的句柄（避免无限空转） */
        if (!uv_loop_alive(loop) && consecutive_idle > busy_threshold) {
            /* 没有活动句柄且连续空闲，退出循环 */
            return 0;
        }
    }

    return 0;
}