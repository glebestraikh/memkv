#pragma once

#include <stdint.h>
#include <pthread.h>
#include <time.h>

typedef struct {
    uint64_t total_commands;
    uint64_t cmd_get;
    uint64_t cmd_set;
    uint64_t cmd_del;
    uint64_t cmd_ping;
    uint64_t cmd_auth;
    uint64_t cmd_config;
    uint64_t cmd_expire;
    uint64_t cmd_ttl;
    uint64_t cmd_stats;
    uint64_t cmd_other;
    
    uint64_t cache_hits;
    uint64_t cache_misses;

    uint64_t used_memory_bytes;
    uint64_t max_memory_bytes;

    uint64_t current_connections;
    uint64_t total_connections;

    time_t start_time;

    pthread_mutex_t mutex;
} stats_t;

int stats_init(stats_t *stats, uint64_t max_memory);

void stats_destroy(stats_t *stats);

void stats_inc_command(stats_t *stats, const char *cmd);

void stats_inc_cache_hit(stats_t *stats);

void stats_inc_cache_miss(stats_t *stats);

void stats_set_memory(stats_t *stats, uint64_t bytes);

void stats_inc_connections(stats_t *stats);

void stats_dec_connections(stats_t *stats);

uint64_t stats_get_uptime(stats_t *stats);

double stats_get_hit_ratio(stats_t *stats);

char* stats_format(stats_t *stats);


