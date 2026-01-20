#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../service/terminal_service.h"
#include "../adapter/out/connection_adapter.h"
#include "../service/response_formatter.h"
#include "../../protocol/resp.h"

int client_app_run(client_config_t *config) {
    if (!config) return EXIT_FAILURE;

    printf("Connecting to %s:%d...\n", config->addr, config->port);

    connection_t *conn = connection_create(config->addr, config->port);
    if (!conn) {
        return EXIT_FAILURE;
    }

    printf("Connected to %s:%d\n", config->addr, config->port);
    printf("Use 'AUTH <username> <password>' to authenticate\n");
    printf("Use 'QUIT' to exit\n\n");

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "%s:%d> ", config->addr, config->port);

    while (1) {
        char *line = terminal_read_command(prompt);

        if (!line) {
            printf("\n");
            break;
        }

        if (strlen(line) == 0) {
            free(line);
            continue;
        }

        if (strcasecmp(line, "QUIT") == 0 || strcasecmp(line, "EXIT") == 0) {
            printf("Bye!\n");
            free(line);
            break;
        }

        resp_value_t *response = connection_execute_command(conn, line);
        if (response) {
            response_display(response);
            resp_free(response);
        }

        free(line);
    }

    connection_close(conn);

    return EXIT_SUCCESS;
}

