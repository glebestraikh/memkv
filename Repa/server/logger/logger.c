#include "../logger/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>

typedef struct {
    FILE *file;
    char *filepath;
    size_t file_size_limit;
    log_level_t min_level;
    pthread_mutex_t mutex;
    int initialized;
} logger_t;

static logger_t g_logger = {
    .file = NULL,
    .filepath = NULL,
    .file_size_limit = 10 * 1024 * 1024,
    .min_level = LOG_INFO,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = 0
};

static const char* level_to_string(const log_level_t level) {
    switch (level) {
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "ERROR";
        case LOG_FATAL:   return "FATAL";
        default:          return "UNKNOWN";
    }
}

static void get_timestamp(char *buffer) {
    const time_t now = time(NULL);
    const struct tm *tm_info = localtime(&now);
    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void rotate_log_if_needed(void) {
    if (!g_logger.file || !g_logger.filepath) {
        return;
    }

    const long size = ftell(g_logger.file);
    if (size >= (long)g_logger.file_size_limit) {
        fclose(g_logger.file);

        char backup_path[512];
        snprintf(backup_path, sizeof(backup_path), "%s.old", g_logger.filepath);
        rename(g_logger.filepath, backup_path);

        g_logger.file = fopen(g_logger.filepath, "a");
    }
}

int logger_init(const char *path, const size_t file_size_limit) {
    pthread_mutex_lock(&g_logger.mutex);

    if (g_logger.initialized) {
        pthread_mutex_unlock(&g_logger.mutex);
        return 0;
    }

    if (path) {
        g_logger.filepath = strdup(path);
        g_logger.file = fopen(path, "a");
        if (!g_logger.file) {
            free(g_logger.filepath);
            g_logger.filepath = NULL;
            pthread_mutex_unlock(&g_logger.mutex);
            return -1;
        }
    } else {
        g_logger.file = stdout;
    }

    g_logger.file_size_limit = file_size_limit;
    g_logger.initialized = 1;

    pthread_mutex_unlock(&g_logger.mutex);
    return 0;
}

void logger_fini(void) {
    pthread_mutex_lock(&g_logger.mutex);

    if (!g_logger.initialized) {
        pthread_mutex_unlock(&g_logger.mutex);
    }

    if (g_logger.file && g_logger.file != stdout && g_logger.file != stderr) {
        fclose(g_logger.file);
    }

    if (g_logger.filepath) {
        free(g_logger.filepath);
        g_logger.filepath = NULL;
    }

    g_logger.file = NULL;
    g_logger.initialized = 0;

    pthread_mutex_unlock(&g_logger.mutex);
}

int logger_write(const log_output_t output, const log_level_t level,
                 const char *filename, const int line, const char *format, ...) {

    if (level < g_logger.min_level) {
        return 0;
    }

    pthread_mutex_lock(&g_logger.mutex);

    if (!g_logger.initialized) {
        pthread_mutex_unlock(&g_logger.mutex);
        return -1;
    }

    FILE *out = g_logger.file;
    if (output == LOG_STDOUT) {
        out = stdout;
    } else if (output == LOG_STDERR) {
        out = stderr;
    }

    char timestamp[64];
    get_timestamp(timestamp);

    const pthread_t tid = pthread_self();

    fprintf(out, "[%s] [%s] [tid:%lu] [%s:%d] ",
            timestamp, level_to_string(level),
            (unsigned long)tid, filename, line);

    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);

    if (out == g_logger.file) {
        rotate_log_if_needed();
    }

    pthread_mutex_unlock(&g_logger.mutex);
    return 0;
}

void logger_set_level(const log_level_t level) {
    pthread_mutex_lock(&g_logger.mutex);
    g_logger.min_level = level;
    pthread_mutex_unlock(&g_logger.mutex);
}

log_level_t logger_get_level(void) {
    pthread_mutex_lock(&g_logger.mutex);
    const log_level_t level = g_logger.min_level;
    pthread_mutex_unlock(&g_logger.mutex);
    return level;
}

