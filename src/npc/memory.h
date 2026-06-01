#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include "environment.h"

#define MAX_MEM_CONTENT     512
#define MAX_MEM_CHARS       128
#define MAX_SHORT_TERM       32
#define MAX_LONG_TERM        64
#define MAX_PERMANENT        32

/* Timeout constants for memory cleanup (in minutes) */
#define MEM_SHORT_TIMEOUT_MINUTES    480    /* 8 hours */
#define MEM_LONG_TIMEOUT_MINUTES    43200   /* 30 days */

/* 记忆类型 */
typedef enum {
    MEM_SHORT_TERM,   /* 8小时内的NPC对话 */
    MEM_LONG_TERM,    /* 一个月内的事件 */
    MEM_PERMANENT,    /* 永久重要事件 */
} MemoryType;

/* 单条记忆 */
typedef struct {
    char     content[MAX_MEM_CONTENT];          /* 内容 */
    char     related_chars[MAX_MEM_CHARS];      /* 相关人物，逗号分隔 */
    GameTime timestamp;                          /* 发生时间 */
    MemoryType type;
} MemoryEntry;

/* 记忆库 */
typedef struct {
    MemoryEntry short_term[MAX_SHORT_TERM];
    int         short_count;

    MemoryEntry long_term[MAX_LONG_TERM];
    int         long_count;

    MemoryEntry permanent[MAX_PERMANENT];
    int         perm_count;
} MemoryStore;

/* 初始化 */
void mem_init(MemoryStore *store);

/* 清理（重置为零，数组是内联的不需要释放堆内存） */
void mem_free(MemoryStore *store);

/* 添加记忆 */
bool mem_add(MemoryStore *store, const char *content,
             const char *related_chars, const GameTime *time, MemoryType type);

/* 清理过期记忆：短期超过8h(480min)、长期超过30天(43200min) */
void mem_cleanup(MemoryStore *store, const GameTime *now);

#endif
