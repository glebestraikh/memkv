#include "kv_entry.h"
#include <stdlib.h>
#include <string.h>

kv_entry_t *kv_entry_create(const char *key, const char *value, size_t value_len, const time_t ttl) {
    kv_entry_t *entry = calloc(1, sizeof(kv_entry_t));
    if (!entry) {
        return NULL;
    }

    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        return NULL;
    }

    entry->value = malloc(value_len);
    if (!entry->value) {
        free(entry->key);
        free(entry);
        return NULL;
    }
    memcpy(entry->value, value, value_len);
    entry->value_len = value_len;

    entry->created_at = time(NULL);
    entry->last_accessed = entry->created_at;
    entry->access_count = 0;

    if (ttl > 0) {
        entry->expires_at = entry->created_at + ttl;
    } else {
        entry->expires_at = 0;
    }

    entry->next = NULL;
    entry->prev = NULL;
    entry->lru_next = NULL;
    entry->lru_prev = NULL;

    return entry;
}

void kv_entry_free(kv_entry_t *entry) {
    if (!entry) return;

    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;

    free(entry->key);
    free(entry->value);

    free(entry);
}

int kv_entry_is_expired(const kv_entry_t *entry) {
    if (!entry || entry->expires_at == 0) {
        return 0;
    }

    return time(NULL) >= entry->expires_at;
}

void kv_entry_touch(kv_entry_t *entry) {
    if (!entry) {
        return;
    }

    entry->last_accessed = time(NULL);
    entry->access_count++;
}
