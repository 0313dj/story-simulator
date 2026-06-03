#include "memory.h"
#include "log.h"
#include <string.h>
#include <stdbool.h>

void mem_init(MemoryStore *store)
{
    memset(store, 0, sizeof(*store));
}

void mem_free(MemoryStore *store)
{
    /* Arrays are inline (not heap pointers), just zero the store */
    memset(store, 0, sizeof(*store));
}

bool mem_add(MemoryStore *store, const char *content,
             const char *related_chars, const GameTime *time, MemoryType type)
{
    MemoryEntry *arr;
    int *count;
    int max;

    switch (type) {
        case MEM_SHORT_TERM:
            arr = store->short_term; count = &store->short_count; max = MAX_SHORT_TERM; break;
        case MEM_LONG_TERM:
            arr = store->long_term;  count = &store->long_count;  max = MAX_LONG_TERM;  break;
        case MEM_PERMANENT:
            arr = store->permanent;  count = &store->perm_count;  max = MAX_PERMANENT;  break;
        default: return false;
    }

    if (*count >= max) return false;

    MemoryEntry *e = &arr[*count];
    safe_strcpy(e->content, content, MAX_MEM_CONTENT);
    safe_strcpy(e->related_chars, related_chars, MAX_MEM_CHARS);
    e->timestamp = *time;
    e->type = type;
    (*count)++;
    return true;
}

/* 每月天数 */
static int month_days(int year, int month)
{
    static const int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return mdays[month];
}

/* 从元年1月1日起的天数 */
static long days_since_epoch(int year, int month, int day)
{
    long d = 0;
    for (int y = 1; y < year; y++)
        d += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    for (int m = 1; m < month; m++)
        d += month_days(year, m);
    return d + day - 1;
}

/* 计算两个 GameTime 之间的分钟差 */
static long long time_diff_minutes(const GameTime *a, const GameTime *b)
{
    long long day_diff = days_since_epoch(b->year, b->month, b->day)
                       - days_since_epoch(a->year, a->month, a->day);
    long long min_diff = (b->hour * 60LL + b->minute) - (a->hour * 60LL + a->minute);
    /* Bug #39: use long long to avoid overflow for large date ranges */
    return day_diff * 24LL * 60LL + min_diff;
}

void mem_cleanup(MemoryStore *store, const GameTime *now)
{
    /* Short term: expire after MEM_SHORT_TIMEOUT_MINUTES (8 hours) */
    for (int i = store->short_count - 1; i >= 0; i--) {
        long long diff = time_diff_minutes(&store->short_term[i].timestamp, now);
        if (diff > MEM_SHORT_TIMEOUT_MINUTES) {
            for (int j = i; j < store->short_count - 1; j++) {
                store->short_term[j] = store->short_term[j + 1];
            }
            store->short_count--;
        }
    }

    /* Long term: expire after MEM_LONG_TIMEOUT_MINUTES (30 days) */
    for (int i = store->long_count - 1; i >= 0; i--) {
        long long diff = time_diff_minutes(&store->long_term[i].timestamp, now);
        if (diff > MEM_LONG_TIMEOUT_MINUTES) {
            for (int j = i; j < store->long_count - 1; j++) {
                store->long_term[j] = store->long_term[j + 1];
            }
            store->long_count--;
        }
    }
    /* 永久记忆不清理 */
}

