/* Redis protocol and connection ownership only; cache policy lives in cache.fg. */
#include <hiredis/hiredis.h>
#include "forge/string.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
int64_t platform_redis_open(const char *host, int64_t port, const char *password) {
    if (!host || !*host || port < 1 || port > 65535) return 0;
    struct timeval timeout = {0, 100000};
    redisContext *client = redisConnectWithTimeout(host, (int)port, timeout);
    if (!client || client->err || redisSetTimeout(client, timeout) != REDIS_OK) {
        if (client) redisFree(client);
        return 0;
    }
    if (password && *password) {
        redisReply *reply = redisCommand(client, "AUTH %b", password, strlen(password));
        int ok = reply && reply->type == REDIS_REPLY_STATUS && reply->str && strcmp(reply->str, "OK") == 0;
        if (reply) freeReplyObject(reply);
        if (!ok) { redisFree(client); return 0; }
    }
    return (int64_t)(intptr_t)client;
}
int64_t platform_redis_close(int64_t handle) {
    if (handle) redisFree((redisContext *)(intptr_t)handle);
    return 1;
}
const char *platform_redis_get(int64_t handle, const char *key) {
    if (!handle || !key || strlen(key) > 2048) return "";
    redisContext *client = (redisContext *)(intptr_t)handle;
    if (client->err) return "";
    redisReply *reply = redisCommand(client, "GET %b", key, strlen(key));
    const char *value = "";
    if (reply && reply->type == REDIS_REPLY_STRING && reply->len <= 262144 &&
        reply->str && !memchr(reply->str, 0, reply->len)) value = fr_str_concat("", reply->str);
    if (reply) freeReplyObject(reply);
    return value;
}
int64_t platform_redis_set(int64_t handle, const char *key, const char *body, int64_t seconds, int64_t only_missing) {
    if (!handle || !key || !body || strlen(key) > 2048 || strlen(body) > 262144 || seconds < 0) return 0;
    redisContext *client = (redisContext *)(intptr_t)handle;
    if (client->err) return 0;
    const char *argv[6] = {"SET", key, body};
    size_t lengths[6] = {3, strlen(key), strlen(body)};
    int argc = 3;
    char ttl[32];
    if (seconds) {
        snprintf(ttl, sizeof(ttl), "%lld", (long long)seconds);
        argv[argc] = "EX"; lengths[argc++] = 2;
        argv[argc] = ttl; lengths[argc++] = strlen(ttl);
    }
    if (only_missing) { argv[argc] = "NX"; lengths[argc++] = 2; }
    redisReply *reply = redisCommandArgv(client, argc, argv, lengths);
    int ok = reply && reply->type == REDIS_REPLY_STATUS && reply->str && strcmp(reply->str, "OK") == 0;
    if (reply) freeReplyObject(reply);
    return ok;
}
