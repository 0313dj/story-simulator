#ifndef LOG_H
#define LOG_H

typedef enum { LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void log_init(const char *dir);
void log_write(LogLevel lv, const char *fmt, ...);
void log_close(void);

#define log_info(...)  log_write(LOG_INFO,  __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __VA_ARGS__)

#endif
