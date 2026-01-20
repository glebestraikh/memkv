#include <stdio.h>
#include <stdlib.h>
#include "app/app.h"
#include "app/config.h"

int main(const int argc, char *argv[]) {
    printf("repactl - Repa Command Line Interface\n");
    printf("Type 'QUIT' to exit\n\n");

    client_config_t *config = client_config_default();
    if (!config) {
        fprintf(stderr, "Failed to create client configuration\n");
        return EXIT_FAILURE;
    }

    if (client_config_parse_args(config, argc, argv) != 0) {
        client_config_free(config);
        return EXIT_SUCCESS;
    }

    const int result = client_app_run(config);

    client_config_free(config);

    return result;
}

