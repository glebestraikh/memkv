#include "../service/response_formatter.h"
#include <stdio.h>

void response_display(const resp_value_t *response) {
    if (!response) {
        return;
    }

    switch (response->type) {
        case RESP_SIMPLE_STRING:
            printf("%s\n", response->data.str);
            break;

        case RESP_ERROR:
            response_display_error(response->data.str);
            break;

        case RESP_INTEGER:
            printf("(integer) %lld\n", (long long) response->data.integer);
            break;

        case RESP_BULK_STRING:
            printf("%s\n", response->data.str);
            break;

        case RESP_NULL:
            printf("(nil)\n");
            break;

        case RESP_ARRAY:
            response_display_array(response);
            break;
    }
}

void response_display_array(const resp_value_t *array) {
    if (!array || array->type != RESP_ARRAY) {
        return;
    }

    for (size_t i = 0; i < array->data.array.count; i++) {
        printf("%zu) ", i + 1);
        const resp_value_t *elem = array->data.array.elements[i];

        if (elem->type == RESP_BULK_STRING) {
            printf("\"%s\"\n", elem->data.str);
        } else if (elem->type == RESP_NULL) {
            printf("(nil)\n");
        } else if (elem->type == RESP_INTEGER) {
            printf("%lld\n", (long long) elem->data.integer);
        } else if (elem->type == RESP_SIMPLE_STRING) {
            printf("%s\n", elem->data.str);
        } else {
            printf("(unknown type)\n");
        }
    }
}

void response_display_error(const char *error_msg) {
    if (!error_msg) {
        printf("(error) Unknown error\n");
        return;
    }

    printf("(error) %s\n", error_msg);
}
