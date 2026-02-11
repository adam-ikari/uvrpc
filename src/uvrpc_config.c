#include "uvrpc.h"
#include <stdlib.h>
#include <string.h>
#include <zmq.h>

/* ==================== 配置构建器实现 ==================== */

uvrpc_config_t* uvrpc_config_new(void) {
    uvrpc_config_t* config = (uvrpc_config_t*)calloc(1, sizeof(uvrpc_config_t));
    if (!config) {
        return NULL;
    }
    
    /* 默认值 */
    config->loop = NULL;
    config->address = NULL;
    config->transport = UVRPC_TRANSPORT_INPROC;
    config->mode = UVRPC_SERVER_CLIENT;
    config->zmq_ctx = NULL;
    config->owns_zmq_ctx = 1;
    config->perf_mode = UVRPC_PERF_BALANCED;
    
    /* 性能模式默认值 (UVRPC_PERF_BALANCED) */
    /* batch_size=10: 测试在1K并发下平衡吞吐量和延迟 */
    config->batch_size = 10;
    
    /* io_threads=2: 适合双核及以上CPU，单核应设为1 */
    config->io_threads = 2;
    
    /* HWM=1000: 测试支持最多1000个pending消息，超过则丢弃 */
    config->sndhwm = 1000;
    config->rcvhwm = 1000;
    
    /* TCP buffer=256KB: 测试在1Gbps网络下足够，高速网络可增加到1MB */
    config->tcp_sndbuf = 256 * 1024;
    config->tcp_rcvbuf = 256 * 1024;
    
    /* Keepalive: 默认关闭，长时间连接建议开启 */
    config->tcp_keepalive = 0;
    config->tcp_keepalive_idle = 60;
    config->tcp_keepalive_cnt = 5;
    config->tcp_keepalive_intvl = 10;
    
    /* Reconnect: 初始100ms，最大10s，适合大多数网络环境 */
    config->reconnect_ivl = 100;
    config->reconnect_ivl_max = 10000;
    
    /* Linger: 1000ms，确保优雅关闭时消息不丢失 */
    /* 原值0可能导致生产环境消息丢失，改为1000ms更安全 */
    config->linger = 1000;
    
    config->udp_multicast = 0;
    config->udp_multicast_group = NULL;
    
    return config;
}

void uvrpc_config_free(uvrpc_config_t* config) {
    if (!config) {
        return;
    }
    
    if (config->address) {
        free(config->address);
    }
    
    if (config->udp_multicast_group) {
        free(config->udp_multicast_group);
    }
    
    free(config);
}

uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop) {
    if (!config) {
        return NULL;
    }
    config->loop = loop;
    return config;
}

uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address) {
    if (!config || !address) {
        return NULL;
    }
    
    if (config->address) {
        free(config->address);
    }
    
    config->address = strdup(address);
    return config;
}

uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_t transport) {
    if (!config) {
        return NULL;
    }
    config->transport = transport;
    return config;
}

uvrpc_config_t* uvrpc_config_set_mode(uvrpc_config_t* config, uvrpc_mode_t mode) {
    if (!config) {
        return NULL;
    }
    config->mode = mode;
    return config;
}

uvrpc_config_t* uvrpc_config_set_zmq_ctx(uvrpc_config_t* config, void* zmq_ctx) {
    if (!config) {
        return NULL;
    }
    config->zmq_ctx = zmq_ctx;
    config->owns_zmq_ctx = (zmq_ctx == NULL) ? 1 : 0;
    return config;
}

uvrpc_config_t* uvrpc_config_set_perf_mode(uvrpc_config_t* config, uvrpc_performance_mode_t mode) {
    if (!config) {
        return NULL;
    }
    
    config->perf_mode = mode;
    
    /* 根据性能模式设置默认值 */
    switch (mode) {
        case UVRPC_PERF_LOW_LATENCY:
            config->batch_size = 1;
            config->sndhwm = 100;
            config->rcvhwm = 100;
            config->tcp_sndbuf = 64 * 1024;
            config->tcp_rcvbuf = 64 * 1024;
            config->io_threads = 1;
            break;
        case UVRPC_PERF_BALANCED:
            config->batch_size = 10;
            config->sndhwm = 1000;
            config->rcvhwm = 1000;
            config->tcp_sndbuf = 256 * 1024;
            config->tcp_rcvbuf = 256 * 1024;
            config->io_threads = 2;
            break;
        case UVRPC_PERF_HIGH_THROUGHPUT:
            config->batch_size = 100;
            config->sndhwm = 10000;
            config->rcvhwm = 10000;
            config->tcp_sndbuf = 1024 * 1024;
            config->tcp_rcvbuf = 1024 * 1024;
            config->io_threads = 4;
            break;
    }
    
    return config;
}

uvrpc_config_t* uvrpc_config_set_batch_size(uvrpc_config_t* config, int batch_size) {
    if (!config) {
        return NULL;
    }
    config->batch_size = batch_size;
    return config;
}

uvrpc_config_t* uvrpc_config_set_hwm(uvrpc_config_t* config, int sndhwm, int rcvhwm) {
    if (!config) {
        return NULL;
    }
    config->sndhwm = sndhwm;
    config->rcvhwm = rcvhwm;
    return config;
}

uvrpc_config_t* uvrpc_config_set_io_threads(uvrpc_config_t* config, int io_threads) {
    if (!config) {
        return NULL;
    }
    config->io_threads = io_threads;
    return config;
}

uvrpc_config_t* uvrpc_config_set_tcp_buffer(uvrpc_config_t* config, int sndbuf, int rcvbuf) {
    if (!config) {
        return NULL;
    }
    config->tcp_sndbuf = sndbuf;
    config->tcp_rcvbuf = rcvbuf;
    return config;
}

uvrpc_config_t* uvrpc_config_set_tcp_keepalive(uvrpc_config_t* config, int enable, int idle, int cnt, int intvl) {
    if (!config) {
        return NULL;
    }
    config->tcp_keepalive = enable;
    config->tcp_keepalive_idle = idle;
    config->tcp_keepalive_cnt = cnt;
    config->tcp_keepalive_intvl = intvl;
    return config;
}

uvrpc_config_t* uvrpc_config_set_reconnect(uvrpc_config_t* config, int ivl, int ivl_max) {
    if (!config) {
        return NULL;
    }
    config->reconnect_ivl = ivl;
    config->reconnect_ivl_max = ivl_max;
    return config;
}

uvrpc_config_t* uvrpc_config_set_linger(uvrpc_config_t* config, int linger_ms) {
    if (!config) {
        return NULL;
    }
    config->linger = linger_ms;
    return config;
}

uvrpc_config_t* uvrpc_config_set_udp_multicast(uvrpc_config_t* config, const char* group) {
    if (!config) {
        return NULL;
    }
    
    if (config->udp_multicast_group) {
        free(config->udp_multicast_group);
    }
    
    config->udp_multicast = 1;
    if (group) {
        config->udp_multicast_group = strdup(group);
    }
    
    return config;
}