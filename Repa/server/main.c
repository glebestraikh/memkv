#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/app.h"
#include "app/config.h"

int main(const int argc, char *argv[]) {
    printf("Repa Server Starting...\n");

    app_config_t *config = app_config_default();
    if (!config) {
        fprintf(stderr, "Failed to create application configuration\n");
        return EXIT_FAILURE;
    }

    char *config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
            config_path = argv[i + 1];
            break;
        }
    }

    if (config_path) {
        if (app_config_load_file(config, config_path) != 0) {
            fprintf(stderr, "Warning: Failed to load configuration file: %s\n", config_path);
        }
    } else {
        if (app_config_load_file(config, config->config_path) != 0) {
            fprintf(stderr, "Warning: Failed to load default configuration file: %s\n", config->config_path);
        }
    }

    if (app_config_parse_args(config, argc, argv) != 0) {
        app_config_free(config);
        return EXIT_SUCCESS;
    }

    const int result = app_run(config);

    app_config_free(config);

    printf("Repa Server Stopped.\n");
    return result;
}

