
#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// 로그 파일 초기화
int logger_init(const char *filepath)
{
    pthread_mutex_lock(&log_mutex);

    if (log_file != NULL) {
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    log_file = fopen(filepath, "a");

    pthread_mutex_unlock(&log_mutex);

    if (log_file == NULL)
        return -1;

    return 0;
}

// 로그 기록
void logger_log(const char *level, const char *format, ...)
{
    pthread_mutex_lock(&log_mutex);

    if (log_file == NULL) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm time_info;
    char timestamp[32];

    if (localtime_r(&now, &time_info) == NULL) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", &time_info);

    fprintf(log_file, "[%s] [%s] ",
            timestamp, level);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");

    fflush(log_file);

    pthread_mutex_unlock(&log_mutex);
}

// 로그 파일 종료
void logger_close(void)
{
    pthread_mutex_lock(&log_mutex);

    if (log_file != NULL) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }

    pthread_mutex_unlock(&log_mutex);
}

