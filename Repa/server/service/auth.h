#pragma once

#include <pthread.h>

typedef struct {
    char *default_user;
    char *default_password;
    pthread_mutex_t mutex;
} auth_service_t;

auth_service_t *auth_service_create(const char *default_user, const char *default_password);

void auth_service_destroy(auth_service_t *auth);

int auth_service_authenticate(auth_service_t *auth, const char *username, const char *password);
