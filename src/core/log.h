#ifndef LOG_H
#define LOG_H

#include <string.h>
#include <stdint.h>

typedef enum { LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void log_init(const char *dir);
void log_write(LogLevel lv, const char *fmt, ...);
void log_close(void);

#define log_info(...)  log_write(LOG_INFO,  __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __VA_ARGS__)

/* Check if a byte is a UTF-8 continuation byte (10xxxxxx) */
static inline int utf8_is_cont(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/* Safe string copy: always null-terminates dest, truncates at valid
   UTF-8 boundary to avoid breaking multi-byte characters.
   size is the FULL buffer size (not size-1). If size == 0, does nothing. */
__attribute__((no_sanitize("undefined")))
static inline void safe_strcpy(char *dest, const char *src, size_t size)
{
    if (size > 0) {
        size_t max_len = size - 1;
        size_t src_len = strlen(src);
        size_t copy_len = (src_len < max_len) ? src_len : max_len;

        /* Walk back to the last valid UTF-8 start byte.
           A UTF-8 start byte is either 0xxxxxxx (ASCII) or 11xxxxxx (multi-byte lead).
           Continuation bytes are 10xxxxxx — we must not truncate on one of those. */
        while (copy_len > 0 && utf8_is_cont((unsigned char)src[copy_len])) {
            copy_len--;
        }

        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
}

#endif
