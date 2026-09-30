#include "forge_web.h"
#include "forge/string.h"
#include <stdint.h>
static int64_t active_pool;
int64_t platform_pool(void) { return active_pool; }
int64_t platform_pool_set(int64_t pool) { active_pool=pool; return 1; }
static int64_t dispatch_request(int64_t request) {
    int64_t result=frmod_routes_dispatch(request);
    fr_str_arena_reset();
    return result;
}
int64_t platform_run(const char *host,int64_t port,int64_t workers) {
    return fw_run(host,port,workers,dispatch_request);
}
