#pragma once

typedef struct {
    char *addr;
    int port;
    char *user;
    char *password;
} client_config_t;

client_config_t* client_config_default(void);

int client_config_parse_args(client_config_t *config, int argc, char *argv[]);

void client_config_free(client_config_t *config);

