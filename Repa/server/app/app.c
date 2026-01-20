#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include "../logger/logger.h"
#include "../service/storage.h"
#include "../service/auth.h"
#include "../service/command_executor.h"
#include "../model/stats.h"
#include "../adapter/in/network_listener.h"

static int ensure_parent_dir(const char *filepath) {
    if (!filepath || !strchr(filepath, '/')) {
        return 0;
    }

    char *path_copy = strdup(filepath);
    if (!path_copy) return -1;

    char *dir = dirname(path_copy);
    if (strcmp(dir, ".") == 0) {
        free(path_copy);
        return 0;
    }

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(path_copy);
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }

    free(path_copy);
    return 0;
}

typedef struct {
    storage_t *storage;
    volatile int *shutdown_flag;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} maintenance_ctx_t;

static void *maintenance_thread(void *arg) {
    maintenance_ctx_t *ctx = arg;

    while (!*ctx->shutdown_flag) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        pthread_mutex_lock(&ctx->mutex);
        pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &ts);
        pthread_mutex_unlock(&ctx->mutex);

        if (*ctx->shutdown_flag) break;

        const size_t cleaned = storage_cleanup_expired(ctx->storage);
        if (cleaned > 0) {
            LOG_DEBUG_MSG("Cleaned up %zu expired keys", cleaned);
        }
    }

    return NULL;
}

static app_config_t *g_app_config = NULL;

static volatile sig_atomic_t g_shutdown_signal = 0;

static void signal_handler(const int signo) {
    (void)signo;
    g_shutdown_signal = 1;
    if (g_app_config) {
        g_app_config->shutdown_requested = 1;
    }
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        fprintf(stderr, "Warning: failed to setup SIGINT handler\n");
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        fprintf(stderr, "Warning: failed to setup SIGTERM handler\n");
    }
    if (sigaction(SIGQUIT, &sa, NULL) != 0) {
        fprintf(stderr, "Warning: failed to setup SIGQUIT handler\n");
    }

    signal(SIGPIPE, SIG_IGN);
}

void app_request_shutdown(void) {
    if (g_app_config) {
        g_app_config->shutdown_requested = 1;
    }
}

int app_run(app_config_t *config) {
    if (!config) return EXIT_FAILURE;
    g_app_config = config;

    if (ensure_parent_dir(config->log_path) != 0) {
        fprintf(stderr, "Warning: failed to create log directory for %s\n", config->log_path);
    }

    if (logger_init(config->log_path, 10 * 1024 * 1024) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }
    if (config->verbose) logger_set_level(LOG_DEBUG);

    LOG_INFO_MSG("Repa server starting");
    LOG_INFO_MSG("Configuration loaded from: %s", config->config_path);
    LOG_INFO_MSG("Port: %d", config->port);
    LOG_INFO_MSG("Max memory: %zu MB", config->max_memory_mb);
    LOG_INFO_MSG("Workers: %d", config->workers);
    LOG_INFO_MSG("Default TTL: %ld seconds", (long)config->default_ttl);
    LOG_INFO_MSG("Log level: %s", config->log_level);
    LOG_INFO_MSG("Default user: %s", config->default_user);

    setup_signal_handlers();

    stats_t stats;
    if (stats_init(&stats, config->max_memory_mb * 1024 * 1024) != 0) {
        LOG_ERROR_MSG("Failed to initialize statistics");
        logger_fini();
        return EXIT_FAILURE;
    }

    storage_t *storage = storage_create(config->max_memory_mb * 1024 * 1024, config->default_ttl, &stats);
    if (!storage) {
        LOG_ERROR_MSG("Failed to initialize storage");
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Storage initialized");

    auth_service_t *auth = auth_service_create(config->default_user, config->default_password);
    if (!auth) {
        LOG_ERROR_MSG("Failed to initialize authentication service");
        storage_destroy(storage);
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Authentication service initialized");

    runtime_config_t *runtime_config = runtime_config_create(config->max_memory_mb, config->workers, config->default_ttl);
    if (!runtime_config) {
        LOG_ERROR_MSG("Failed to initialize runtime configuration");
        auth_service_destroy(auth);
        storage_destroy(storage);
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Runtime configuration initialized");

    command_executor_t *executor = command_executor_create(storage, &stats, auth, runtime_config);
    if (!executor) {
        LOG_ERROR_MSG("Failed to initialize command executor");
        runtime_config_destroy(runtime_config);
        auth_service_destroy(auth);
        storage_destroy(storage);
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Command executor initialized");

    network_listener_t *listener = network_listener_create(config->port, config->workers, executor);
    if (!listener) {
        LOG_ERROR_MSG("Failed to create network listener");
        command_executor_destroy(executor);
        runtime_config_destroy(runtime_config);
        auth_service_destroy(auth);
        storage_destroy(storage);
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Network listener created");

    if (network_listener_start(listener) != 0) {
        LOG_ERROR_MSG("Failed to start network listener");
        network_listener_destroy(listener);
        command_executor_destroy(executor);
        runtime_config_destroy(runtime_config);
        auth_service_destroy(auth);
        storage_destroy(storage);
        stats_destroy(&stats);
        logger_fini();
        return EXIT_FAILURE;
    }
    LOG_INFO_MSG("Repa server ready to accept connections on port %d", config->port);

    maintenance_ctx_t maint_ctx;
    maint_ctx.storage = storage;
    maint_ctx.shutdown_flag = &config->shutdown_requested;
    pthread_mutex_init(&maint_ctx.mutex, NULL);
    pthread_cond_init(&maint_ctx.cond, NULL);

    pthread_t maint_thread;
    if (pthread_create(&maint_thread, NULL, maintenance_thread, &maint_ctx) != 0) {
        LOG_ERROR_MSG("Failed to create maintenance thread");
        network_listener_stop(listener, 5);
        network_listener_destroy(listener);
        command_executor_destroy(executor);
        runtime_config_destroy(runtime_config);
        auth_service_destroy(auth);
        storage_destroy(storage);
        stats_destroy(&stats);
        pthread_cond_destroy(&maint_ctx.cond);
        pthread_mutex_destroy(&maint_ctx.mutex);
        logger_fini();
        return EXIT_FAILURE;
    }

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGQUIT);

    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    int sig;
    sigwait(&sigset, &sig);

    LOG_INFO_MSG("Received shutdown signal");

    pthread_mutex_lock(&maint_ctx.mutex);
    pthread_cond_signal(&maint_ctx.cond);
    pthread_mutex_unlock(&maint_ctx.mutex);

    pthread_join(maint_thread, NULL);
    pthread_cond_destroy(&maint_ctx.cond);
    pthread_mutex_destroy(&maint_ctx.mutex);

    network_listener_stop(listener, 5);

    network_listener_destroy(listener);
    command_executor_destroy(executor);
    runtime_config_destroy(runtime_config);
    auth_service_destroy(auth);
    storage_destroy(storage);
    stats_destroy(&stats);

    LOG_INFO_MSG("All threads have finished");
    LOG_INFO_MSG("Repa finished");
    logger_fini();

    g_app_config = NULL;
    return EXIT_SUCCESS;
}

