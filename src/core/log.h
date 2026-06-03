#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ── Log levels ── */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

/* ── Lifecycle ── */
int  log_init(const char *filename, LogLevel level);
void log_shutdown(void);
void log_flush(void);

/* ── Core write (called via macros below) ── */
void log_write(LogLevel level,
               const char *file, int line, const char *func,
               const char *fmt, ...);

/* ── Logging macros (file + line + func automatically captured) ── */
#define LOG_D(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_I(...) log_write(LOG_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_W(...) log_write(LOG_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_E(...) log_write(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_F(...) log_write(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* ═══════════════════════════════════════════════════════════════
   UTF-8 safe string utilities (unrelated to logging — kept here
   because many files include log.h for these).
   ═══════════════════════════════════════════════════════════════ */

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

        while (copy_len > 0 && utf8_is_cont((unsigned char)src[copy_len]))
            copy_len--;

        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
}

static inline void safe_strcpy_bounded(char *dest, const char *src,
                                        size_t max_src_len, size_t dest_size)
{
    if (dest_size > 0 && max_src_len > 0) {
        size_t srclen = 0;
        while (srclen < max_src_len && src[srclen] != '\0')
            srclen++;

        size_t max_dest = dest_size - 1;
        size_t copy_len = (srclen < max_dest) ? srclen : max_dest;

        while (copy_len > 0 && utf8_is_cont((unsigned char)src[copy_len]))
            copy_len--;

        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
}

static inline void safe_strcat(char *dest, const char *src, size_t size)
{
    if (size > 0) {
        size_t dest_len = 0;
        while (dest_len < size && dest[dest_len] != '\0')
            dest_len++;
        if (dest_len < size) {
            safe_strcpy(dest + dest_len, src, size - dest_len);
        }
    }
}

#endif
