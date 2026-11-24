
#ifndef LOGGER_H
#define LOGGER_H

int logger_init(const char *filepath);
void logger_log(const char *level, const char *format, ...);
void logger_close(void);

#endif

