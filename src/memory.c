#include "memory.h"
#include <stdio.h>
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
    strncpy(e->content, content, MAX_MEM_CONTENT - 1);
    e->content[MAX_MEM_CONTENT - 1] = '\0';
    strncpy(e->related_chars, related_chars, MAX_MEM_CHARS - 1);
    e->related_chars[MAX_MEM_CHARS - 1] = '\0';
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

const char *mem_type_str(MemoryType t)
{
    switch (t) {
        case MEM_SHORT_TERM: return "短期";
        case MEM_LONG_TERM:  return "长期";
        case MEM_PERMANENT:  return "永久";
        default:             return "未知";
    }
}

void mem_print(const MemoryStore *store)
{
    printf("═══ 短期记忆 (%d) ═══\n", store->short_count);
    for (int i = 0; i < store->short_count; i++) {
        const MemoryEntry *e = &store->short_term[i];
        printf("  [%02d:%02d] %s\n", e->timestamp.hour, e->timestamp.minute, e->content);
    }

    printf("═══ 长期记忆 (%d) ═══\n", store->long_count);
    for (int i = 0; i < store->long_count; i++) {
        const MemoryEntry *e = &store->long_term[i];
        printf("  [%d-%02d-%02d] %s\n",
            e->timestamp.year, e->timestamp.month, e->timestamp.day, e->content);
    }

    printf("═══ 永久记忆 (%d) ═══\n", store->perm_count);
    for (int i = 0; i < store->perm_count; i++) {
        const MemoryEntry *e = &store->permanent[i];
        printf("  ★ %s\n", e->content);
    }
}
