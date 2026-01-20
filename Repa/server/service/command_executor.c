#include "command_executor.h"
#include "auth.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static resp_value_t *handle_ping(const command_executor_t *executor) {
    stats_inc_command(executor->stats, "PING");
    return resp_create_simple_string("PONG");
}

static resp_value_t *handle_hello(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "HELLO");

    if (cmd->data.array.count < 2) {
        return resp_create_error("ERR", "wrong number of arguments for 'HELLO' command");
    }

    const resp_value_t *version = cmd->data.array.elements[1];
    if (version->type != RESP_BULK_STRING || strcmp(version->data.str, "2") != 0) {
        return resp_create_error("NOPROTO", "unsupported protocol version");
    }

    return resp_create_simple_string("OK");
}

static resp_value_t *handle_auth(const command_executor_t *executor, const resp_value_t *cmd, int *is_authenticated) {
    stats_inc_command(executor->stats, "AUTH");

    if (cmd->data.array.count < 2 || cmd->data.array.count > 3) {
        return resp_create_error("ERR", "wrong number of arguments for 'AUTH' command");
    }

    const char *username;
    const char *password;

    if (cmd->data.array.count == 2) {
        username = executor->auth->default_user;
        password = cmd->data.array.elements[1]->data.str;
    } else {
        username = cmd->data.array.elements[1]->data.str;
        password = cmd->data.array.elements[2]->data.str;
    }

    if (auth_service_authenticate(executor->auth, username, password)) {
        *is_authenticated = 1;
        return resp_create_simple_string("OK");
    }

    return resp_create_error("WRONGPASS", "invalid username-password pair");
}

static resp_value_t *handle_get(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "GET");

    if (cmd->data.array.count < 2) {
        return resp_create_error("ERR", "wrong number of arguments for 'GET' command");
    }

    const resp_value_t *key = cmd->data.array.elements[1];
    if (key->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid key type");
    }

    size_t value_len;
    char *value = storage_get(executor->storage, key->data.str, &value_len);

    if (!value) {
        return resp_create_null();
    }

    resp_value_t *response = resp_create_bulk_string(value, value_len);
    free(value);

    return response;
}

static resp_value_t *handle_set(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "SET");

    if (cmd->data.array.count < 3) {
        return resp_create_error("ERR", "wrong number of arguments for 'SET' command");
    }

    const resp_value_t *key = cmd->data.array.elements[1];
    const resp_value_t *value = cmd->data.array.elements[2];

    if (key->type != RESP_BULK_STRING || value->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid argument type");
    }

    const time_t ttl = 0;

    const int result = storage_set(executor->storage, key->data.str,
                                   value->data.str, value->value_len, ttl);

    if (result == 0) {
        return resp_create_simple_string("OK");
    }

    return resp_create_error("ERR", "out of memory");
}

static resp_value_t *handle_del(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "DEL");

    if (cmd->data.array.count < 2) {
        return resp_create_error("ERR", "wrong number of arguments for 'DEL' command");
    }

    int deleted = 0;
    for (size_t i = 1; i < cmd->data.array.count; i++) {
        const resp_value_t *key = cmd->data.array.elements[i];
        if (key->type == RESP_BULK_STRING) {
            deleted += storage_del(executor->storage, key->data.str);
        }
    }

    return resp_create_integer(deleted);
}

static resp_value_t *handle_expire(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "EXPIRE");

    if (cmd->data.array.count < 3) {
        return resp_create_error("ERR", "wrong number of arguments for 'EXPIRE' command");
    }

    const resp_value_t *key = cmd->data.array.elements[1];
    const resp_value_t *seconds = cmd->data.array.elements[2];

    if (key->type != RESP_BULK_STRING || seconds->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid argument type");
    }

    const time_t ttl = atoi(seconds->data.str);
    const int result = storage_expire(executor->storage, key->data.str, ttl);

    return resp_create_integer(result);
}

static resp_value_t *handle_ttl(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "TTL");

    if (cmd->data.array.count < 2) {
        return resp_create_error("ERR", "wrong number of arguments for 'TTL' command");
    }

    const resp_value_t *key = cmd->data.array.elements[1];
    if (key->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid key type");
    }

    const int64_t ttl = storage_ttl(executor->storage, key->data.str);
    return resp_create_integer(ttl);
}

static resp_value_t *handle_stats(const command_executor_t *executor) {
    stats_inc_command(executor->stats, "STATS");

    char *stats_str = stats_format(executor->stats);
    if (!stats_str) {
        return resp_create_error("ERR", "failed to format statistics");
    }

    resp_value_t *response = resp_create_bulk_string(stats_str, strlen(stats_str));
    free(stats_str);

    return response;
}

static resp_value_t *handle_quit(const command_executor_t *executor) {
    (void) executor;
    return resp_create_simple_string("OK");
}

static resp_value_t *handle_config_get(const command_executor_t *executor, const resp_value_t *cmd) {
    if (cmd->data.array.count < 3) {
        return resp_create_error("ERR", "wrong number of arguments for 'CONFIG GET' command");
    }

    const resp_value_t *param = cmd->data.array.elements[2];
    if (param->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid parameter type");
    }

    if (pthread_rwlock_rdlock(&executor->runtime_config->rwlock) != 0) {
        return resp_create_error("ERR", "failed to acquire config lock");
    }

    char value[64];

    if (strcmp(param->data.str, "*") == 0) {
        resp_value_t *response = resp_create_array(10);

        resp_array_set(response, 0, resp_create_bulk_string("maxmemory", 9));
        snprintf(value, sizeof(value), "%zu", executor->runtime_config->max_memory_bytes);
        resp_array_set(response, 1, resp_create_bulk_string(value, strlen(value)));

        resp_array_set(response, 2, resp_create_bulk_string("maxclients", 10));
        resp_array_set(response, 3, resp_create_bulk_string("10000", 5));

        resp_array_set(response, 4, resp_create_bulk_string("timeout", 7));
        resp_array_set(response, 5, resp_create_bulk_string("0", 1));

        resp_array_set(response, 6, resp_create_bulk_string("tcp-keepalive", 13));
        resp_array_set(response, 7, resp_create_bulk_string("300", 3));

        resp_array_set(response, 8, resp_create_bulk_string("databases", 9));
        resp_array_set(response, 9, resp_create_bulk_string("16", 2));

        pthread_rwlock_unlock(&executor->runtime_config->rwlock);
        return response;
    }

    resp_value_t *response = resp_create_array(2);
    resp_array_set(response, 0, resp_create_bulk_string(param->data.str, strlen(param->data.str)));

    if (strcasecmp(param->data.str, "maxmemory") == 0) {
        snprintf(value, sizeof(value), "%zu", executor->runtime_config->max_memory_bytes);
        resp_array_set(response, 1, resp_create_bulk_string(value, strlen(value)));
    } else if (strcasecmp(param->data.str, "maxmemory-mb") == 0) {
        snprintf(value, sizeof(value), "%zu", executor->runtime_config->max_memory_mb);
        resp_array_set(response, 1, resp_create_bulk_string(value, strlen(value)));
    } else if (strcasecmp(param->data.str, "default-ttl") == 0) {
        snprintf(value, sizeof(value), "%ld", (long)executor->runtime_config->default_ttl);
        resp_array_set(response, 1, resp_create_bulk_string(value, strlen(value)));
    } else if (strcasecmp(param->data.str, "workers") == 0) {
        snprintf(value, sizeof(value), "%d", executor->runtime_config->workers);
        resp_array_set(response, 1, resp_create_bulk_string(value, strlen(value)));
    } else {
        pthread_rwlock_unlock(&executor->runtime_config->rwlock);
        resp_free(response);
        return resp_create_error("ERR", "unsupported CONFIG parameter");
    }

    pthread_rwlock_unlock(&executor->runtime_config->rwlock);
    return response;
}

static resp_value_t *handle_config_set(const command_executor_t *executor, const resp_value_t *cmd) {
    if (cmd->data.array.count < 4) {
        return resp_create_error("ERR", "wrong number of arguments for 'CONFIG SET' command");
    }

    const resp_value_t *param = cmd->data.array.elements[2];
    const resp_value_t *value = cmd->data.array.elements[3];

    if (param->type != RESP_BULK_STRING || value->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid argument type");
    }

    if (pthread_rwlock_wrlock(&executor->runtime_config->rwlock) != 0) {
        return resp_create_error("ERR", "failed to acquire config lock");
    }

    if (strcasecmp(param->data.str, "maxmemory") == 0) {
        const size_t new_value = atoll(value->data.str);
        if (new_value < 1024 * 1024) {
            pthread_rwlock_unlock(&executor->runtime_config->rwlock);
            return resp_create_error("ERR", "maxmemory must be at least 1MB");
        }
        executor->runtime_config->max_memory_bytes = new_value;
        executor->runtime_config->max_memory_mb = new_value / (1024 * 1024);
        executor->stats->max_memory_bytes = new_value;
        storage_set_max_memory(executor->storage, new_value);
    } else if (strcasecmp(param->data.str, "maxmemory-mb") == 0) {
        const size_t new_value = atoll(value->data.str);
        if (new_value < 1) {
            pthread_rwlock_unlock(&executor->runtime_config->rwlock);
            return resp_create_error("ERR", "maxmemory-mb must be at least 1");
        }
        executor->runtime_config->max_memory_mb = new_value;
        executor->runtime_config->max_memory_bytes = new_value * 1024 * 1024;
        executor->stats->max_memory_bytes = executor->runtime_config->max_memory_bytes;
        storage_set_max_memory(executor->storage, executor->runtime_config->max_memory_bytes);
    } else if (strcasecmp(param->data.str, "default-ttl") == 0) {
        const time_t new_value = atol(value->data.str);
        if (new_value < 0) {
            pthread_rwlock_unlock(&executor->runtime_config->rwlock);
            return resp_create_error("ERR", "default-ttl must be non-negative");
        }
        executor->runtime_config->default_ttl = new_value;
        storage_set_default_ttl(executor->storage, new_value);
    } else {
        pthread_rwlock_unlock(&executor->runtime_config->rwlock);
        return resp_create_error("ERR", "unsupported CONFIG parameter");
    }

    pthread_rwlock_unlock(&executor->runtime_config->rwlock);
    return resp_create_simple_string("OK");
}

static resp_value_t *handle_config(const command_executor_t *executor, const resp_value_t *cmd) {
    stats_inc_command(executor->stats, "CONFIG");

    if (cmd->data.array.count < 2) {
        return resp_create_error("ERR", "wrong number of arguments for 'CONFIG' command");
    }

    const resp_value_t *subcommand = cmd->data.array.elements[1];
    if (subcommand->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid subcommand type");
    }

    if (strcasecmp(subcommand->data.str, "GET") == 0) {
        return handle_config_get(executor, cmd);
    }

    if (strcasecmp(subcommand->data.str, "SET") == 0) {
        return handle_config_set(executor, cmd);
    }

    return resp_create_error("ERR", "unknown CONFIG subcommand");
}

runtime_config_t *runtime_config_create(const size_t max_memory_mb, const int workers, const time_t default_ttl) {
    runtime_config_t *config = malloc(sizeof(runtime_config_t));
    if (!config) {
        return NULL;
    }

    config->max_memory_mb = max_memory_mb;
    config->max_memory_bytes = max_memory_mb * 1024 * 1024;
    config->default_ttl = default_ttl;
    config->workers = workers;

    if (pthread_rwlock_init(&config->rwlock, NULL) != 0) {
        free(config);
        return NULL;
    }

    return config;
}

void runtime_config_destroy(runtime_config_t *config) {
    if (!config) return;
    pthread_rwlock_destroy(&config->rwlock);
    free(config);
}

command_executor_t *command_executor_create(storage_t *storage, stats_t *stats, auth_service_t *auth, runtime_config_t *runtime_config) {
    if (!storage || !stats || !auth || !runtime_config) {
        return NULL;
    }

    command_executor_t *executor = malloc(sizeof(command_executor_t));
    if (!executor) {
        return NULL;
    }

    executor->storage = storage;
    executor->stats = stats;
    executor->auth = auth;
    executor->runtime_config = runtime_config;

    return executor;
}

void command_executor_destroy(command_executor_t *executor) {
    free(executor);
}

resp_value_t *command_executor_execute(command_executor_t *executor,
                                       resp_value_t *cmd,
                                       int *is_authenticated) {
    if (!executor || !cmd || !is_authenticated) {
        return resp_create_error("ERR", "internal error");
    }

    if (cmd->type != RESP_ARRAY || cmd->data.array.count == 0) {
        return resp_create_error("ERR", "invalid command format");
    }

    const resp_value_t *cmd_name = cmd->data.array.elements[0];
    if (cmd_name->type != RESP_BULK_STRING) {
        return resp_create_error("ERR", "invalid command name");
    }

    const char *name = cmd_name->data.str;

    if (strcasecmp(name, "HELLO") == 0) {
        return handle_hello(executor, cmd);
    }
    if (strcasecmp(name, "AUTH") == 0) {
        return handle_auth(executor, cmd, is_authenticated);
    }
    if (strcasecmp(name, "CONFIG") == 0) {
        return handle_config(executor, cmd);
    }
    if (strcasecmp(name, "PING") == 0) {
        return handle_ping(executor);
    }
    if (strcasecmp(name, "QUIT") == 0) {
        return handle_quit(executor);
    }

    if (!*is_authenticated) {
        return resp_create_error("NOAUTH", "Authentication required");
    }

    if (strcasecmp(name, "GET") == 0) {
        return handle_get(executor, cmd);
    }
    if (strcasecmp(name, "SET") == 0) {
        return handle_set(executor, cmd);
    }
    if (strcasecmp(name, "DEL") == 0) {
        return handle_del(executor, cmd);
    }
    if (strcasecmp(name, "EXPIRE") == 0) {
        return handle_expire(executor, cmd);
    }
    if (strcasecmp(name, "TTL") == 0) {
        return handle_ttl(executor, cmd);
    }
    if (strcasecmp(name, "STATS") == 0) {
        return handle_stats(executor);
    }

    stats_inc_command(executor->stats, "OTHER");
    return resp_create_error("ERR", "unknown command");
}
