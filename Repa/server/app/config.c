#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

app_config_t* app_config_default(void) {
    app_config_t *config = malloc(sizeof(app_config_t));
    if (!config) return NULL;

    config->port = 6380;
    config->config_path = strdup("repa.conf");
    config->verbose = 0;
    config->max_memory_mb = 256;
    config->workers = 4;
    config->default_ttl = 0;
    config->log_path = strdup("repa.log");
    config->default_user = strdup("admin");
    config->default_password = strdup("admin");
    config->log_level = strdup("info");
    config->shutdown_requested = 0;

    return config;
}

static void trim(char *str) {
    if (!str) return;

    char *start = str;
    while (*start && (*start == ' ' || *start == '\t')) {
        start++;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

int app_config_load_file(app_config_t *config, const char *path) {
    if (!config || !path) return -1;

    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    char line[512];
    int line_num = 0;

    while (fgets(line, sizeof(line), file)) {
        line_num++;

        trim(line);

        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        char *equals = strchr(line, '=');
        if (!equals) {
            fprintf(stderr, "Warning: Invalid line %d in %s: %s\n", line_num, path, line);
            continue;
        }

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;

        trim(key);
        trim(value);

        if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        } else if (strcmp(key, "max_memory_mb") == 0) {
            config->max_memory_mb = atoi(value);
        } else if (strcmp(key, "workers") == 0) {
            config->workers = atoi(value);
        } else if (strcmp(key, "default_ttl") == 0) {
            config->default_ttl = atoi(value);
        } else if (strcmp(key, "log_level") == 0) {
            free(config->log_level);
            config->log_level = strdup(value);
        } else if (strcmp(key, "log_output") == 0) {
            free(config->log_path);
            config->log_path = strdup(value);
        } else if (strcmp(key, "default_user") == 0) {
            free(config->default_user);
            config->default_user = strdup(value);
        } else if (strcmp(key, "default_password") == 0) {
            free(config->default_password);
            config->default_password = strdup(value);
        } else {
            fprintf(stderr, "Warning: Unknown parameter '%s' in %s at line %d\n", key, path, line_num);
        }
    }

    fclose(file);
    return 0;
}

static void print_help(const char *prog_name) {
    printf("Repa - In-Memory Key-Value Store\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  --port <num>          Port to listen on (default: 6380)\n");
    printf("  --config <path>       Path to configuration file (default: repa.conf)\n");
    printf("  --verbose             Enable verbose logging\n");
    printf("  --max-memory-mb <num> Maximum memory in megabytes (default: 256)\n");
    printf("  --workers <num>       Number of worker threads (default: 4)\n");
    printf("  --default-ttl <sec>   Default TTL in seconds, 0 = no expiry (default: 0)\n");
    printf("  --help                Show this help message\n\n");
    printf("Configuration file format (repa.conf):\n");
    printf("  port = 6380\n");
    printf("  max_memory_mb = 256\n");
    printf("  workers = 4\n");
    printf("  default_ttl = 0\n");
    printf("  log_level = info\n");
    printf("  log_output = repa.log\n");
    printf("  default_user = admin\n");
    printf("  default_password = admin\n\n");
}

int app_config_parse_args(app_config_t *config, const int argc, char *argv[]) {
    if (!config) return -1;

    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"config", required_argument, 0, 'c'},
        {"verbose", no_argument, 0, 'v'},
        {"max-memory-mb", required_argument, 0, 'm'},
        {"workers", required_argument, 0, 'w'},
        {"default-ttl", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:c:vm:w:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                config->port = atoi(optarg);
                break;
            case 'c':
                free(config->config_path);
                config->config_path = strdup(optarg);
                break;
            case 'v':
                config->verbose = 1;
                break;
            case 'm':
                config->max_memory_mb = atoi(optarg);
                break;
            case 'w':
                config->workers = atoi(optarg);
                break;
            case 't':
                config->default_ttl = atoi(optarg);
                break;
            case 'h':
                print_help(argv[0]);
                return -1;
            default:
                print_help(argv[0]);
                return -1;
        }
    }

    return 0;
}

void app_config_free(app_config_t *config) {
    if (!config) return;

    free(config->config_path);
    free(config->log_path);
    free(config->default_user);
    free(config->default_password);
    free(config->log_level);
    free(config);
}
