#include "auth.h"
#include <stdlib.h>
#include <string.h>

auth_service_t *auth_service_create(const char *default_user, const char *default_password) {
    if (!default_user || !default_password) {
        return NULL;
    }

    auth_service_t *auth = malloc(sizeof(auth_service_t));
    if (!auth) {
        return NULL;
    }

    auth->default_user = strdup(default_user);
    auth->default_password = strdup(default_password);

    if (!auth->default_user || !auth->default_password) {
        free(auth->default_user);
        free(auth->default_password);
        free(auth);
        return NULL;
    }

    if (pthread_mutex_init(&auth->mutex, NULL) != 0) {
        free(auth->default_user);
        free(auth->default_password);
        free(auth);
        return NULL;
    }

    return auth;
}

void auth_service_destroy(auth_service_t *auth) {
    if (!auth) {
        return;
    }

    pthread_mutex_destroy(&auth->mutex);
    free(auth->default_user);
    free(auth->default_password);
    free(auth);
}

int auth_service_authenticate(auth_service_t *auth, const char *username, const char *password) {
    if (!auth || !username || !password) {
        return 0;
    }

    if (pthread_mutex_lock(&auth->mutex) != 0) {
        return 0;
    }

    int authenticated = 0;
    if (strcmp(username, auth->default_user) == 0 &&
        strcmp(password, auth->default_password) == 0) {
        authenticated = 1;
    }

    pthread_mutex_unlock(&auth->mutex);
    return authenticated;
}
