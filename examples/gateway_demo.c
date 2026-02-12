/**
 * UVRPC Gateway Demo
 * 
 * 网关 ID 转换示例：
 * 1. 客户端 -> 网关：msgid_raw
 * 2. 网关 -> 后端：msgid_gateway
 * 3. 后端 -> 网关：msgid_gateway
 * 4. 网关 -> 客户端：msgid_raw
 */

#include "../src/uvrpc_idmap.h"
#include <stdio.h>
#include <stdlib.h>

static uvrpc_idmap_ctx_t* g_idmap = NULL;

int main() {
    printf("=== UVRPC Gateway ID Mapping Demo ===\n\n");
    
    /* 创建 ID 映射上下文 */
    g_idmap = uvrpc_idmap_ctx_new();
    
    /* 模拟 5 个请求 */
    printf("Simulating 5 requests:\n");
    for (int i = 1; i <= 5; i++) {
        uint32_t msgid_raw = i * 100;
        void* client_handle = (void*)(uintptr_t)i;
        
        /* ID 转换 */
        uint32_t msgid_gateway = uvrpc_idmap_to_gateway(g_idmap, msgid_raw, client_handle);
        
        printf("  Request #%d: msgid_raw=%u -> msgid_gateway=%u\n", i, msgid_raw, msgid_gateway);
    }
    
    printf("\nSimulating 5 responses:\n");
    for (int i = 1; i <= 5; i++) {
        uint32_t msgid_gateway = i;  /* 假设后端返回 msgid = 1,2,3,4,5 */
        
        uint32_t msgid_raw;
        void* client_handle;
        
        if (uvrpc_idmap_to_raw(g_idmap, msgid_gateway, &msgid_raw, &client_handle) == 0) {
            printf("  Response #%d: msgid_gateway=%u -> msgid_raw=%u (client=%p)\n",
                   i, msgid_gateway, msgid_raw, client_handle);
        } else {
            printf("  Response #%d: msgid_gateway=%u NOT FOUND!\n", i, msgid_gateway);
        }
    }
    
    printf("\n=== Demo Complete ===\n");
    
    uvrpc_idmap_ctx_free(g_idmap);
    return 0;
}
