#pragma once

#include <pthread.h>

typedef enum {
    LOG_DEBUG = 1,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

typedef enum {
    LOG_STDOUT = 1,
    LOG_STDERR,
    LOG_FILE
} log_output_t;

int logger_init(const char *path, size_t file_size_limit);

void logger_fini(void);

int logger_write(log_output_t output, log_level_t level, const char *filename, int line, const char *format, ...);

void logger_set_level(log_level_t level);

log_level_t logger_get_level(void);

#define LOG_DEBUG_MSG(...) logger_write(LOG_FILE, LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_INFO_MSG(...) logger_write(LOG_FILE, LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_WARN_MSG(...) logger_write(LOG_FILE, LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_ERROR_MSG(...) logger_write(LOG_FILE, LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_FATAL_MSG(...) logger_write(LOG_FILE, LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
