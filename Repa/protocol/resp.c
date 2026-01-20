#include "../protocol/resp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *read_line(const char *buffer, size_t len, size_t *pos, size_t *line_len) {
    size_t start = *pos;
    while (*pos < len - 1) {
        if (buffer[*pos] == '\r' && buffer[*pos + 1] == '\n') {
            *line_len = *pos - start;
            *pos += 2;
            return &buffer[start];
        }
        (*pos)++;
    }
    return NULL;
}

static resp_value_t *parse_simple_string(const char *buffer, size_t len, size_t *pos);

static resp_value_t *parse_error(const char *buffer, size_t len, size_t *pos);

static resp_value_t *parse_integer(const char *buffer, size_t len, size_t *pos);

static resp_value_t *parse_bulk_string(const char *buffer, size_t len, size_t *pos);

static resp_value_t *parse_array(const char *buffer, size_t len, size_t *pos);

resp_value_t *resp_parse(const char *buffer, size_t len, size_t *bytes_consumed) {
    if (!buffer || len == 0) {
        return NULL;
    }

    size_t pos = 0;
    char type = buffer[pos++];
    resp_value_t *value = NULL;

    switch (type) {
        case '+':
            value = parse_simple_string(buffer, len, &pos);
            break;
        case '-':
            value = parse_error(buffer, len, &pos);
            break;
        case ':':
            value = parse_integer(buffer, len, &pos);
            break;
        case '$':
            value = parse_bulk_string(buffer, len, &pos);
            break;
        case '*':
            value = parse_array(buffer, len, &pos);
            break;
        default:
            return NULL;
    }

    if (value && bytes_consumed) {
        *bytes_consumed = pos;
    }

    return value;
}

static resp_value_t *parse_simple_string(const char *buffer, size_t len, size_t *pos) {
    size_t line_len;
    const char *line = read_line(buffer, len, pos, &line_len);
    if (!line) return NULL;

    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_SIMPLE_STRING;
    value->data.str = strndup(line, line_len);
    return value;
}

static resp_value_t *parse_error(const char *buffer, size_t len, size_t *pos) {
    size_t line_len;
    const char *line = read_line(buffer, len, pos, &line_len);
    if (!line) return NULL;

    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_ERROR;
    value->data.str = strndup(line, line_len);
    return value;
}

static resp_value_t *parse_integer(const char *buffer, size_t len, size_t *pos) {
    size_t line_len;
    const char *line = read_line(buffer, len, pos, &line_len);
    if (!line) return NULL;

    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_INTEGER;
    value->data.integer = atoll(line);
    return value;
}

static resp_value_t *parse_bulk_string(const char *buffer, size_t len, size_t *pos) {
    size_t line_len;
    const char *line = read_line(buffer, len, pos, &line_len);
    if (!line) return NULL;

    int bulk_len = atoi(line);

    if (bulk_len < 0) {
        resp_value_t *value = malloc(sizeof(resp_value_t));
        if (!value) return NULL;
        value->type = RESP_NULL;
        return value;
    }

    if (*pos + bulk_len + 2 > len) {
        return NULL;
    }

    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_BULK_STRING;
    value->data.str = strndup(&buffer[*pos], bulk_len);
    value->value_len = bulk_len;
    *pos += bulk_len + 2;

    return value;
}

static resp_value_t *parse_array(const char *buffer, size_t len, size_t *pos) {
    size_t line_len;
    const char *line = read_line(buffer, len, pos, &line_len);
    if (!line) return NULL;

    int array_len = atoi(line);

    if (array_len < 0) {
        resp_value_t *value = malloc(sizeof(resp_value_t));
        if (!value) return NULL;
        value->type = RESP_NULL;
        return value;
    }

    resp_value_t *value = resp_create_array(array_len);
    if (!value) return NULL;

    for (int i = 0; i < array_len; i++) {
        size_t consumed = 0;
        resp_value_t *element = resp_parse(&buffer[*pos], len - *pos, &consumed);
        if (!element) {
            resp_free(value);
            return NULL;
        }
        resp_array_set(value, i, element);
        *pos += consumed;
    }

    return value;
}

resp_value_t *resp_create_simple_string(const char *str) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_SIMPLE_STRING;
    value->data.str = strdup(str);
    value->value_len = strlen(str);
    return value;
}

resp_value_t *resp_create_error(const char *prefix, const char *message) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_ERROR;
    size_t len = strlen(prefix) + strlen(message) + 2;
    value->data.str = malloc(len);
    snprintf(value->data.str, len, "%s %s", prefix, message);
    value->value_len = strlen(value->data.str);
    return value;
}

resp_value_t *resp_create_integer(int64_t num) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_INTEGER;
    value->data.integer = num;
    value->value_len = 0;
    return value;
}

resp_value_t *resp_create_bulk_string(const char *str, size_t len) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    if (!str) {
        value->type = RESP_NULL;
        value->value_len = 0;
    } else {
        value->type = RESP_BULK_STRING;
        value->data.str = strndup(str, len);
        value->value_len = len;
    }
    return value;
}

resp_value_t *resp_create_null(void) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_NULL;
    value->value_len = 0;
    return value;
}

resp_value_t *resp_create_array(size_t count) {
    resp_value_t *value = malloc(sizeof(resp_value_t));
    if (!value) return NULL;

    value->type = RESP_ARRAY;
    value->data.array.count = count;
    value->data.array.elements = calloc(count, sizeof(resp_value_t *));
    value->value_len = 0;
    return value;
}

void resp_array_set(resp_value_t *array, size_t index, resp_value_t *element) {
    if (!array || array->type != RESP_ARRAY || index >= array->data.array.count) {
        return;
    }
    array->data.array.elements[index] = element;
}

int resp_serialize(const resp_value_t *value, char **output, size_t *output_len) {
    if (!value || !output || !output_len) {
        return -1;
    }

    char *buffer = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&buffer, &size);
    if (!stream) return -1;

    switch (value->type) {
        case RESP_SIMPLE_STRING:
            fprintf(stream, "+%s\r\n", value->data.str);
            break;
        case RESP_ERROR:
            fprintf(stream, "-%s\r\n", value->data.str);
            break;
        case RESP_INTEGER:
            fprintf(stream, ":%lld\r\n", (long long) value->data.integer);
            break;
        case RESP_BULK_STRING:
            fprintf(stream, "$%zu\r\n", value->value_len);
            fwrite(value->data.str, 1, value->value_len, stream);
            fprintf(stream, "\r\n");
            break;
        case RESP_NULL:
            fprintf(stream, "$-1\r\n");
            break;
        case RESP_ARRAY:
            fprintf(stream, "*%zu\r\n", value->data.array.count);
            for (size_t i = 0; i < value->data.array.count; i++) {
                char *elem_buf;
                size_t elem_len;
                if (resp_serialize(value->data.array.elements[i], &elem_buf, &elem_len) == 0) {
                    fwrite(elem_buf, 1, elem_len, stream);
                    free(elem_buf);
                }
            }
            break;
    }

    fclose(stream);
    *output = buffer;
    *output_len = size;
    return 0;
}

void resp_free(resp_value_t *value) {
    if (!value) return;

    switch (value->type) {
        case RESP_SIMPLE_STRING:
        case RESP_ERROR:
        case RESP_BULK_STRING:
            free(value->data.str);
            break;
        case RESP_ARRAY:
            for (size_t i = 0; i < value->data.array.count; i++) {
                resp_free(value->data.array.elements[i]);
            }
            free(value->data.array.elements);
            break;
        default:
            break;
    }

    free(value);
}
