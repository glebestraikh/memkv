#pragma once

#include "../../protocol/resp.h"

typedef struct connection connection_t;

connection_t* connection_create(const char *addr, int port);

resp_value_t* connection_execute_command(connection_t *conn, const char *command);

void connection_close(connection_t *conn);
