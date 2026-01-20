#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_INTEGER,
    RESP_BULK_STRING,
    RESP_ARRAY,
    RESP_NULL
} resp_type_t;

typedef struct resp_value {
    resp_type_t type;

    union {
        char *str;
        int64_t integer;

        struct {
            struct resp_value **elements;
            size_t count;
        } array;
    } data;

    size_t value_len;
} resp_value_t;

resp_value_t *resp_parse(const char *buffer, size_t len, size_t *bytes_consumed);

void resp_free(resp_value_t *value);

int resp_serialize(const resp_value_t *value, char **output, size_t *output_len);

resp_value_t *resp_create_simple_string(const char *str);

resp_value_t *resp_create_error(const char *prefix, const char *message);

resp_value_t *resp_create_integer(int64_t value);

resp_value_t *resp_create_bulk_string(const char *str, size_t len);

resp_value_t *resp_create_null(void);

resp_value_t *resp_create_array(size_t count);

void resp_array_set(resp_value_t *array, size_t index, resp_value_t *element);
