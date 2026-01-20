#pragma once

#include "../../service/command_executor.h"

typedef struct network_listener network_listener_t;

network_listener_t* network_listener_create(int port, int workers, command_executor_t *executor);

int network_listener_start(network_listener_t *listener);

void network_listener_stop(network_listener_t *listener, int timeout_sec);

void network_listener_destroy(network_listener_t *listener);

