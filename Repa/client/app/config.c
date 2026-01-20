#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

client_config_t* client_config_default(void) {
    client_config_t *config = malloc(sizeof(client_config_t));
    if (!config) return NULL;

    config->addr = strdup("127.0.0.1");
    config->port = 6380;
    config->user = NULL;
    config->password = NULL;

    return config;
}

static void print_help(const char *prog_name) {
    printf("repactl - Repa Command Line Interface\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  --addr <address>  Server address (default: 127.0.0.1)\n");
    printf("  --port <num>      Server port (default: 6380)\n");
    printf("  --user <name>     Username for authentication\n");
    printf("  --help            Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --addr localhost --port 6380\n", prog_name);
    printf("  %s --user admin\n", prog_name);
    printf("\n");
}

int client_config_parse_args(client_config_t *config, const int argc, char *argv[]) {
    if (!config) return -1;

    static struct option long_options[] = {
        {"addr", required_argument, 0, 'a'},
        {"port", required_argument, 0, 'p'},
        {"user", required_argument, 0, 'u'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, option_index = 0;
    while ((opt = getopt_long(argc, argv, "a:p:u:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a':
                free(config->addr);
                config->addr = strdup(optarg);
                break;
            case 'p':
                config->port = atoi(optarg);
                break;
            case 'u':
                config->user = strdup(optarg);
                break;
            case 'h':
            default:
                print_help(argv[0]);
                return -1;
        }
    }
    return 0;
}

void client_config_free(client_config_t *config) {
    if (!config) return;

    free(config->addr);
    free(config->user);
    free(config->password);
    free(config);
}

