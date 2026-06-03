#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_log_lock;
#else
#include <pthread.h>
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static FILE        *g_log_file   = NULL;
static LogLevel     g_log_level  = LOG_INFO;
static bool         g_log_initialized = false;

/* ── Internal lock/unlock ── */
static void log_lock(void)
{
#ifdef _WIN32
    EnterCriticalSection(&g_log_lock);
#else
    pthread_mutex_lock(&g_log_lock);
#endif
}

static void log_unlock(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&g_log_lock);
#else
    pthread_mutex_unlock(&g_log_lock);
#endif
}

/* ── Level → string ── */
static const char *level_name(LogLevel level)
{
    switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    case LOG_FATAL: return "FATAL";
    default:        return "?";
    }
}

/* ── Thread-safe timestamp: "2026-06-03 14:31:05" ── */
static void build_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &t);
}

/* ── Ensure parent directory exists (best-effort) ── */
static void ensure_log_dir(const char *filepath)
{
#ifdef _WIN32
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", filepath);
    char *slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        /* Recursively create — simple approach: try CreateDirectoryW */
        wchar_t wdir[512];
        MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 512);
        /* Try creating intermediate dirs too (best-effort, may already exist) */
        for (wchar_t *p = wdir; *p; p++) {
            if (*p == L'/' || *p == L'\\') {
                wchar_t saved = *p; *p = L'\0';
                CreateDirectoryW(wdir, NULL);
                *p = saved;
            }
        }
        CreateDirectoryW(wdir, NULL);
    }
#else
    /* POSIX: mkdir -p equivalent */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", filepath);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" 2>/dev/null", dir);
        system(cmd);
    }
#endif
}

/* ═══════════════════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════════════════ */

int log_init(const char *filename, LogLevel level)
{
    if (g_log_initialized) return 1;

#ifdef _WIN32
    InitializeCriticalSection(&g_log_lock);
#endif
    g_log_level = level;

    if (filename && filename[0]) {
        ensure_log_dir(filename);
        g_log_file = fopen(filename, "a");
        if (!g_log_file) {
            /* Non-fatal: console-only logging still works */
            fprintf(stderr, "[LOG] Cannot open log file: %s\n", filename);
        }
    }

    g_log_initialized = true;
    return 1;
}

void log_flush(void)
{
    if (g_log_file) fflush(g_log_file);
    fflush(stdout);
}

void log_shutdown(void)
{
    if (!g_log_initialized) return;

    log_lock();

    if (!g_log_initialized) {
        log_unlock();
        return;
    }
    g_log_initialized = false;

    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }

    log_unlock();
#ifdef _WIN32
    DeleteCriticalSection(&g_log_lock);
#else
    pthread_mutex_destroy(&g_log_lock);
#endif
}

void log_write(LogLevel level,
               const char *file, int line, const char *func,
               const char *fmt, ...)
{
    /* Level filter (fast path — no lock needed for read) */
    if (level < g_log_level) return;
    if (!g_log_initialized) {
        /* Fallback: write to stderr if log system not initialized */
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "[FALLBACK] ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        return;
    }

    /* ── Format message ── */
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* ── Build timestamp ── */
    char timestamp[64];
    build_timestamp(timestamp, sizeof(timestamp));

    /* ── Extract filename leaf (strip path for readability) ── */
    const char *leaf = file;
    if (leaf) {
        const char *s = strrchr(file, '/');
        if (!s) s = strrchr(file, '\\');
        if (s) leaf = s + 1;
    }

    /* ── Write to stdout ── */
    fprintf(stdout, "[%s] [%s] [%s:%d] [%s] %s\n",
            timestamp, level_name(level), leaf, line, func, msg);
    fflush(stdout);

    /* ── Write to log file ── */
    log_lock();
    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%s] [%s:%d] [%s] %s\n",
                timestamp, level_name(level), leaf, line, func, msg);
        fflush(g_log_file);
    }
    log_unlock();

    /* ── FATAL: flush everything and abort ── */
    if (level == LOG_FATAL) {
        log_flush();
        abort();
    }
}
