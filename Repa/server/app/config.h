#pragma once

#include <stddef.h>
#include <time.h>

typedef struct {
    int port;
    char *config_path;
    int verbose;
    size_t max_memory_mb;
    int workers;
    time_t default_ttl;
    char *log_path;
    char *default_user;
    char *default_password;
    char *log_level;
    int shutdown_requested;
} app_config_t;

app_config_t* app_config_default(void);

int app_config_parse_args(app_config_t *config, int argc, char *argv[]);

int app_config_load_file(app_config_t *config, const char *path);

void app_config_free(app_config_t *config);

