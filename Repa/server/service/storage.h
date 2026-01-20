#pragma once

#include "../model/kv_entry.h"
#include "../model/stats.h"
#include <pthread.h>
#include <stddef.h>

#define STORAGE_DEFAULT_SIZE 1024

typedef struct {
    kv_entry_t **buckets;
    size_t bucket_count;
    size_t entry_count;
    size_t memory_used;
    size_t max_memory;
    time_t default_ttl;

    kv_entry_t *lru_head;
    kv_entry_t *lru_tail;

    pthread_rwlock_t rwlock;
    stats_t *stats;
} storage_t;

storage_t *storage_create(size_t max_memory, time_t default_ttl, stats_t *stats);

void storage_destroy(storage_t *storage);

char *storage_get(storage_t *storage, const char *key, size_t *value_len);

int storage_set(storage_t *storage, const char *key, const char *value,
                size_t value_len, time_t ttl);

int storage_del(storage_t *storage, const char *key);

int storage_exists(storage_t *storage, const char *key);

int storage_expire(storage_t *storage, const char *key, time_t ttl);

int64_t storage_ttl(storage_t *storage, const char *key);

size_t storage_cleanup_expired(storage_t *storage);

size_t storage_get_count(storage_t *storage);

size_t storage_get_memory(storage_t *storage);

void storage_set_max_memory(storage_t *storage, size_t max_memory);

void storage_set_default_ttl(storage_t *storage, time_t default_ttl);
