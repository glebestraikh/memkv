#pragma once

#include <stddef.h>
#include <time.h>
#include <stdint.h>

typedef struct kv_entry {
    char *key;
    char *value;
    size_t value_len;
    
    time_t created_at;
    time_t expires_at;
    
    time_t last_accessed;
    uint64_t access_count;

    struct kv_entry *next;
    struct kv_entry *prev;

    struct kv_entry *lru_next;
    struct kv_entry *lru_prev;
} kv_entry_t;

kv_entry_t* kv_entry_create(const char *key, const char *value, size_t value_len, time_t ttl);

void kv_entry_free(kv_entry_t *entry);

int kv_entry_is_expired(const kv_entry_t *entry);

void kv_entry_touch(kv_entry_t *entry);


