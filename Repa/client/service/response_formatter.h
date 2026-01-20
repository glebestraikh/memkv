#pragma once

#include "../../protocol/resp.h"

void response_display(const resp_value_t *response);

void response_display_array(const resp_value_t *array);

void response_display_error(const char *error_msg);

