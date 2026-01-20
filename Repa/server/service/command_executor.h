#pragma once

#include "../../protocol/resp.h"
#include "storage.h"
#include "../model/stats.h"
#include "auth.h"
#include <pthread.h>

typedef struct {
    size_t max_memory_bytes;
    size_t max_memory_mb;

    time_t default_ttl;

    int workers;

    pthread_rwlock_t rwlock;
} runtime_config_t;

typedef struct {
    storage_t *storage;
    stats_t *stats;
    auth_service_t *auth;
    runtime_config_t *runtime_config;
} command_executor_t;

runtime_config_t *runtime_config_create(size_t max_memory_mb, int workers, time_t default_ttl);

void runtime_config_destroy(runtime_config_t *config);

command_executor_t *command_executor_create(storage_t *storage, stats_t *stats, auth_service_t *auth,
                                            runtime_config_t *runtime_config);

void command_executor_destroy(command_executor_t *executor);

resp_value_t *command_executor_execute(command_executor_t *executor, resp_value_t *cmd, int *is_authenticated);
