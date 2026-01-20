#include "storage.h"
#include <stdlib.h>
#include <string.h>

static void lru_remove(storage_t *storage, kv_entry_t *entry);

static void lru_add_to_head(storage_t *storage, kv_entry_t *entry);

static void lru_move_to_head(storage_t *storage, kv_entry_t *entry);

static void lru_evict(storage_t *storage, size_t needed_bytes);

static uint32_t hash_function(const char *key) {
    uint32_t hash = 5381;
    int c;
    while ((c = (unsigned char) *key++)) {
        hash = hash * 33 + c;
    }
    return hash;
}

storage_t *storage_create(const size_t max_memory, const time_t default_ttl, stats_t *stats) {
    storage_t *storage = calloc(1, sizeof(storage_t));
    if (!storage) {
        return NULL;
    }

    storage->bucket_count = STORAGE_DEFAULT_SIZE;
    storage->buckets = calloc(storage->bucket_count, sizeof(kv_entry_t *));
    if (!storage->buckets) {
        free(storage);
        return NULL;
    }

    storage->max_memory = max_memory;
    storage->default_ttl = default_ttl;
    storage->stats = stats;
    storage->entry_count = 0;
    storage->memory_used = 0;
    storage->lru_head = NULL;
    storage->lru_tail = NULL;

    if (pthread_rwlock_init(&storage->rwlock, NULL) != 0) {
        free(storage->buckets);
        free(storage);
        return NULL;
    }

    return storage;
}

void storage_destroy(storage_t *storage) {
    if (!storage) {
        return;
    }

    const int lock_result = pthread_rwlock_wrlock(&storage->rwlock);

    for (size_t i = 0; i < storage->bucket_count; i++) {
        kv_entry_t *entry = storage->buckets[i];
        while (entry) {
            kv_entry_t *next = entry->next;
            kv_entry_free(entry);
            entry = next;
        }
    }

    free(storage->buckets);

    if (lock_result == 0) {
        pthread_rwlock_unlock(&storage->rwlock);
    }
    pthread_rwlock_destroy(&storage->rwlock);
    free(storage);
}

static void lru_remove(storage_t *storage, kv_entry_t *entry) {
    if (!entry) return;

    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        storage->lru_head = entry->lru_next;
    }

    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        storage->lru_tail = entry->lru_prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void lru_add_to_head(storage_t *storage, kv_entry_t *entry) {
    if (!entry) return;

    entry->lru_prev = NULL;
    entry->lru_next = storage->lru_head;

    if (storage->lru_head) {
        storage->lru_head->lru_prev = entry;
    } else {
        storage->lru_tail = entry;
    }

    storage->lru_head = entry;
}

static void lru_move_to_head(storage_t *storage, kv_entry_t *entry) {
    if (!entry || entry == storage->lru_head) return;
    lru_remove(storage, entry);
    lru_add_to_head(storage, entry);
}

static void lru_evict(storage_t *storage, const size_t needed_bytes) {
    size_t freed = 0;

    while (freed < needed_bytes && storage->lru_tail) {
        kv_entry_t *victim = storage->lru_tail;

        lru_remove(storage, victim);

        const uint32_t hash = hash_function(victim->key);
        const size_t index = hash % storage->bucket_count;

        if (storage->buckets[index] == victim) {
            storage->buckets[index] = victim->next;
        }
        if (victim->prev) {
            victim->prev->next = victim->next;
        }
        if (victim->next) {
            victim->next->prev = victim->prev;
        }

        freed += strlen(victim->key) + victim->value_len;
        storage->memory_used -= strlen(victim->key) + victim->value_len;
        storage->entry_count--;

        kv_entry_free(victim);
    }

    if (storage->stats && freed > 0) {
        stats_set_memory(storage->stats, storage->memory_used);
    }
}

static kv_entry_t *find_entry(const storage_t *storage, const char *key) {
    const uint32_t hash = hash_function(key);
    const size_t index = hash % storage->bucket_count;

    kv_entry_t *entry = storage->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (kv_entry_is_expired(entry)) {
                return NULL;
            }
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

char *storage_get(storage_t *storage, const char *key, size_t *value_len) {
    if (!storage || !key) {
        return NULL;
    }

    if (pthread_rwlock_rdlock(&storage->rwlock) != 0) {
        return NULL;
    }

    kv_entry_t *entry = find_entry(storage, key);
    if (!entry) {
        if (storage->stats) {
            stats_inc_cache_miss(storage->stats);
        }
        pthread_rwlock_unlock(&storage->rwlock);
        return NULL;
    }

    if (storage->stats) {
        stats_inc_cache_hit(storage->stats);
    }

    kv_entry_touch(entry);

    char *value = malloc(entry->value_len);
    if (value) {
        memcpy(value, entry->value, entry->value_len);
        if (value_len) {
            *value_len = entry->value_len;
        }
    }

    pthread_rwlock_unlock(&storage->rwlock);
    return value;
}

static int update_existing_entry(storage_t *storage, kv_entry_t *existing, const char *value, size_t value_len,
                                 const time_t ttl) {
    char *new_value = malloc(value_len);
    if (!new_value) {
        return -1;
    }

    storage->memory_used -= existing->value_len;
    free(existing->value);

    existing->value = new_value;
    memcpy(existing->value, value, value_len);
    existing->value_len = value_len;
    storage->memory_used += value_len;

    if (ttl > 0) {
        existing->expires_at = time(NULL) + ttl;
    } else if (ttl == 0 && storage->default_ttl > 0) {
        existing->expires_at = time(NULL) + storage->default_ttl;
    } else {
        existing->expires_at = 0;
    }

    kv_entry_touch(existing);
    lru_move_to_head(storage, existing);

    if (storage->stats) {
        stats_set_memory(storage->stats, storage->memory_used);
    }

    return 0;
}

static int check_and_evict_memory(storage_t *storage, const size_t key_len, const size_t value_len) {
    const size_t new_memory = storage->memory_used + key_len + value_len;
    if (storage->max_memory > 0 && new_memory > storage->max_memory) {
        const size_t needed = new_memory - storage->max_memory;
        lru_evict(storage, needed);

        if (storage->memory_used + key_len + value_len > storage->max_memory) {
            return -1;
        }
    }
    return 0;
}

static void insert_new_entry(storage_t *storage, kv_entry_t *new_entry, const char *key) {
    const uint32_t hash = hash_function(key);
    const size_t index = hash % storage->bucket_count;

    new_entry->next = storage->buckets[index];
    if (storage->buckets[index]) {
        storage->buckets[index]->prev = new_entry;
    }
    storage->buckets[index] = new_entry;

    lru_add_to_head(storage, new_entry);

    const size_t key_len = strlen(key);
    storage->entry_count++;
    storage->memory_used += key_len + new_entry->value_len;

    if (storage->stats) {
        stats_set_memory(storage->stats, storage->memory_used);
    }
}

int storage_set(storage_t *storage, const char *key, const char *value,
                const size_t value_len, const time_t ttl) {
    if (!storage || !key || !value) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return -1;
    }

    kv_entry_t *existing = find_entry(storage, key);
    if (existing) {
        const int result = update_existing_entry(storage, existing, value, value_len, ttl);
        pthread_rwlock_unlock(&storage->rwlock);
        return result;
    }

    const time_t entry_ttl = ttl > 0 ? ttl : storage->default_ttl;
    kv_entry_t *new_entry = kv_entry_create(key, value, value_len, entry_ttl);
    if (!new_entry) {
        pthread_rwlock_unlock(&storage->rwlock);
        return -1;
    }

    const size_t key_len = strlen(key);
    if (check_and_evict_memory(storage, key_len, value_len) != 0) {
        kv_entry_free(new_entry);
        pthread_rwlock_unlock(&storage->rwlock);
        return -1;
    }

    insert_new_entry(storage, new_entry, key);

    pthread_rwlock_unlock(&storage->rwlock);
    return 0;
}

int storage_del(storage_t *storage, const char *key) {
    if (!storage || !key) {
        return 0;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return 0;
    }

    const uint32_t hash = hash_function(key);
    const size_t index = hash % storage->bucket_count;

    kv_entry_t *entry = storage->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            lru_remove(storage, entry);

            if (entry->prev) {
                entry->prev->next = entry->next;
            } else {
                storage->buckets[index] = entry->next;
            }
            if (entry->next) {
                entry->next->prev = entry->prev;
            }

            storage->memory_used -= strlen(entry->key) + entry->value_len;
            storage->entry_count--;

            if (storage->stats) {
                stats_set_memory(storage->stats, storage->memory_used);
            }

            kv_entry_free(entry);
            pthread_rwlock_unlock(&storage->rwlock);
            return 1;
        }
        entry = entry->next;
    }

    pthread_rwlock_unlock(&storage->rwlock);
    return 0;
}

int storage_exists(storage_t *storage, const char *key) {
    if (!storage || !key) {
        return 0;
    }

    if (pthread_rwlock_rdlock(&storage->rwlock) != 0) {
        return 0;
    }
    const kv_entry_t *entry = find_entry(storage, key);
    const int exists = entry != NULL;
    pthread_rwlock_unlock(&storage->rwlock);

    return exists;
}

int storage_expire(storage_t *storage, const char *key, const time_t ttl) {
    if (!storage || !key) {
        return 0;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return 0;
    }

    kv_entry_t *entry = find_entry(storage, key);
    if (!entry) {
        pthread_rwlock_unlock(&storage->rwlock);
        return 0;
    }

    if (ttl > 0) {
        entry->expires_at = time(NULL) + ttl;
    } else {
        entry->expires_at = 0;
    }

    pthread_rwlock_unlock(&storage->rwlock);
    return 1;
}

int64_t storage_ttl(storage_t *storage, const char *key) {
    if (!storage || !key) {
        return -1;
    }

    if (pthread_rwlock_rdlock(&storage->rwlock) != 0) {
        return -1;
    }

    kv_entry_t *entry = find_entry(storage, key);
    if (!entry) {
        pthread_rwlock_unlock(&storage->rwlock);
        return -1;
    }

    if (entry->expires_at == 0) {
        pthread_rwlock_unlock(&storage->rwlock);
        return -2;
    }

    const time_t now = time(NULL);
    const int64_t ttl = entry->expires_at - now;

    pthread_rwlock_unlock(&storage->rwlock);
    return ttl > 0 ? ttl : -1;
}

size_t storage_cleanup_expired(storage_t *storage) {
    if (!storage) {
        return 0;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return 0;
    }

    size_t removed = 0;
    const time_t now = time(NULL);

    for (size_t i = 0; i < storage->bucket_count; i++) {
        kv_entry_t *entry = storage->buckets[i];
        while (entry) {
            kv_entry_t *next = entry->next;

            if (entry->expires_at > 0 && now >= entry->expires_at) {
                lru_remove(storage, entry);

                if (entry->prev) {
                    entry->prev->next = entry->next;
                } else {
                    storage->buckets[i] = entry->next;
                }
                if (entry->next) {
                    entry->next->prev = entry->prev;
                }

                storage->memory_used -= strlen(entry->key) + entry->value_len;
                storage->entry_count--;

                kv_entry_free(entry);
                removed++;
            }

            entry = next;
        }
    }

    if (removed > 0 && storage->stats) {
        stats_set_memory(storage->stats, storage->memory_used);
    }

    pthread_rwlock_unlock(&storage->rwlock);
    return removed;
}

size_t storage_get_count(storage_t *storage) {
    if (!storage) {
        return 0;
    }

    if (pthread_rwlock_rdlock(&storage->rwlock) != 0) {
        return 0;
    }
    const size_t count = storage->entry_count;
    pthread_rwlock_unlock(&storage->rwlock);

    return count;
}

size_t storage_get_memory(storage_t *storage) {
    if (!storage) {
        return 0;
    }

    if (pthread_rwlock_rdlock(&storage->rwlock) != 0) {
        return 0;
    }
    const size_t memory = storage->memory_used;
    pthread_rwlock_unlock(&storage->rwlock);

    return memory;
}

void storage_set_max_memory(storage_t *storage, const size_t max_memory) {
    if (!storage) {
        return;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return;
    }
    storage->max_memory = max_memory;
    pthread_rwlock_unlock(&storage->rwlock);
}

void storage_set_default_ttl(storage_t *storage, const time_t default_ttl) {
    if (!storage) {
        return;
    }

    if (pthread_rwlock_wrlock(&storage->rwlock) != 0) {
        return;
    }
    storage->default_ttl = default_ttl;
    pthread_rwlock_unlock(&storage->rwlock);
}
