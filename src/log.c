#include "log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <windows.h>

static FILE *g_log_fp = NULL;
static CRITICAL_SECTION g_log_lock;

void log_init(const char *dir)
{
    InitializeCriticalSection(&g_log_lock);
    char path[512];
    snprintf(path, sizeof(path), "%s\\sim.log", dir);
    g_log_fp = fopen(path, "a");
}

void log_write(LogLevel lv, const char *fmt, ...)
{
    if (!g_log_fp) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    const char *tag = "?";
    switch (lv) { case LOG_INFO: tag="INFO"; break;
                  case LOG_WARN: tag="WARN"; break;
                  case LOG_ERROR:tag="ERROR";break; }

    va_list ap;
    va_start(ap, fmt);

    EnterCriticalSection(&g_log_lock);

    fprintf(g_log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, tag);
    vfprintf(g_log_fp, fmt, ap);
    fprintf(g_log_fp, "\n");
    fflush(g_log_fp);

    LeaveCriticalSection(&g_log_lock);
    va_end(ap);

    /* Also dump errors to debug output */
    if (lv == LOG_ERROR) {
        char buf[1024];
        va_list ap2;
        va_start(ap2, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);
        OutputDebugStringA("[sim] ");
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}

void log_close(void)
{
    /* Bug #32: protect closed flag with critical section */
    EnterCriticalSection(&g_log_lock);
    static bool closed = false;
    if (closed) { LeaveCriticalSection(&g_log_lock); return; }
    closed = true;
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    LeaveCriticalSection(&g_log_lock);
    DeleteCriticalSection(&g_log_lock);
}
