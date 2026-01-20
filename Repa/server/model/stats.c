#include "stats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

int stats_init(stats_t *stats, const uint64_t max_memory) {
    if (!stats) {
        return -1;
    }

    memset(stats, 0, sizeof(stats_t));
    stats->max_memory_bytes = max_memory;
    stats->start_time = time(NULL);

    if (pthread_mutex_init(&stats->mutex, NULL) != 0) {
        return -1;
    }

    return 0;
}

void stats_destroy(stats_t *stats) {
    if (!stats) {
        return;
    }

    pthread_mutex_destroy(&stats->mutex);
}

void stats_inc_command(stats_t *stats, const char *cmd) {
    if (!stats || !cmd) {
        return;
    }

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }

    stats->total_commands++;

    if (strcasecmp(cmd, "GET") == 0) {
        stats->cmd_get++;
    } else if (strcasecmp(cmd, "SET") == 0) {
        stats->cmd_set++;
    } else if (strcasecmp(cmd, "DEL") == 0) {
        stats->cmd_del++;
    } else if (strcasecmp(cmd, "PING") == 0) {
        stats->cmd_ping++;
    } else if (strcasecmp(cmd, "AUTH") == 0) {
        stats->cmd_auth++;
    } else if (strcasecmp(cmd, "CONFIG") == 0) {
        stats->cmd_config++;
    } else if (strcasecmp(cmd, "EXPIRE") == 0) {
        stats->cmd_expire++;
    } else if (strcasecmp(cmd, "TTL") == 0) {
        stats->cmd_ttl++;
    } else if (strcasecmp(cmd, "STATS") == 0) {
        stats->cmd_stats++;
    } else {
        stats->cmd_other++;
    }

    pthread_mutex_unlock(&stats->mutex);
}

void stats_inc_cache_hit(stats_t *stats) {
    if (!stats) return;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }
    stats->cache_hits++;
    pthread_mutex_unlock(&stats->mutex);
}

void stats_inc_cache_miss(stats_t *stats) {
    if (!stats) return;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }
    stats->cache_misses++;
    pthread_mutex_unlock(&stats->mutex);
}

void stats_set_memory(stats_t *stats, const uint64_t bytes) {
    if (!stats) return;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }
    stats->used_memory_bytes = bytes;
    pthread_mutex_unlock(&stats->mutex);
}

void stats_inc_connections(stats_t *stats) {
    if (!stats) return;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }
    stats->current_connections++;
    stats->total_connections++;
    pthread_mutex_unlock(&stats->mutex);
}

void stats_dec_connections(stats_t *stats) {
    if (!stats) return;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return;
    }
    if (stats->current_connections > 0) {
        stats->current_connections--;
    }
    pthread_mutex_unlock(&stats->mutex);
}

uint64_t stats_get_uptime(stats_t *stats) {
    if (!stats) return 0;

    return (uint64_t) (time(NULL) - stats->start_time);
}

double stats_get_hit_ratio(stats_t *stats) {
    if (!stats) return 0.0;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return 0.0;
    }

    const uint64_t total = stats->cache_hits + stats->cache_misses;
    double ratio = 0.0;

    if (total > 0) {
        ratio = (double) stats->cache_hits / (double) total * 100.0;
    }

    pthread_mutex_unlock(&stats->mutex);
    return ratio;
}

char *stats_format(stats_t *stats) {
    if (!stats) return NULL;

    if (pthread_mutex_lock(&stats->mutex) != 0) {
        return NULL;
    }

    char *buffer = malloc(4096);
    if (!buffer) {
        pthread_mutex_unlock(&stats->mutex);
        return NULL;
    }

    const uint64_t uptime = (uint64_t) (time(NULL) - stats->start_time);
    const uint64_t hours = uptime / 3600;
    const uint64_t minutes = uptime % 3600 / 60;
    const uint64_t seconds = uptime % 60;

    const double memory_mb = (double) stats->used_memory_bytes / (1024.0 * 1024.0);
    const double max_mb = (double) stats->max_memory_bytes / (1024.0 * 1024.0);
    const double memory_percent = stats->max_memory_bytes > 0
                                      ? (double) stats->used_memory_bytes / (double) stats->max_memory_bytes * 100.0
                                      : 0.0;

    const uint64_t total = stats->cache_hits + stats->cache_misses;
    double hit_ratio = 0.0;
    if (total > 0) {
        hit_ratio = (double) stats->cache_hits / (double) total * 100.0;
    }

    snprintf(buffer, 4096,
             "STATS\r\n"
             "1. Requests\r\n"
             "  total_commands_processed   %llu\r\n"
             "  cmd_get                    %llu\r\n"
             "  cmd_set                    %llu\r\n"
             "  cmd_del                    %llu\r\n"
             "  cmd_ping                   %llu\r\n"
             "  cmd_auth                   %llu\r\n"
             "  cmd_config                 %llu\r\n"
             "  cmd_expire                 %llu\r\n"
             "  cmd_ttl                    %llu\r\n"
             "  cmd_stats                  %llu\r\n"
             "  cmd_other                  %llu\r\n"
             "\r\n"
             "2. Cache\r\n"
             "  cache_hits                 %llu\r\n"
             "  cache_misses               %llu\r\n"
             "  hit_ratio                  %.1f%%\r\n"
             "\r\n"
             "3. Memory\r\n"
             "  used_memory_bytes          %llu  (%.1f / %.1f MiB, %.1f%%)\r\n"
             "\r\n"
             "4. Connections / Uptime\r\n"
             "  current_connections        %llu\r\n"
             "  total_connections_received %llu\r\n"
             "  uptime_s                   %llu  (%llu %llu %llu)\r\n",
             (unsigned long long)stats->total_commands,
             (unsigned long long)stats->cmd_get,
             (unsigned long long)stats->cmd_set,
             (unsigned long long)stats->cmd_del,
             (unsigned long long)stats->cmd_ping,
             (unsigned long long)stats->cmd_auth,
             (unsigned long long)stats->cmd_config,
             (unsigned long long)stats->cmd_expire,
             (unsigned long long)stats->cmd_ttl,
             (unsigned long long)stats->cmd_stats,
             (unsigned long long)stats->cmd_other,
             (unsigned long long)stats->cache_hits,
             (unsigned long long)stats->cache_misses,
             hit_ratio,
             (unsigned long long)stats->used_memory_bytes,
             memory_mb, max_mb, memory_percent,
             (unsigned long long)stats->current_connections,
             (unsigned long long)stats->total_connections,
             (unsigned long long)uptime,
             (unsigned long long)hours,
             (unsigned long long)minutes,
             (unsigned long long)seconds
    );

    pthread_mutex_unlock(&stats->mutex);
    return buffer;
}
