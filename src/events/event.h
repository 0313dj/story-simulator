#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>

#define MAX_EVENTS       1024
#define MAX_EVENT_PAYLOAD 1024

/* ═══════════════════════════════════════════════════════════════
   EventType — per USE Spec Chapter 7
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    EVENT_SYSTEM,         /* 系统事件（启动/关闭/存档） */
    EVENT_ACTION,         /* 行为事件（AI ActionProposal 执行） */
    EVENT_RULE_HIT,       /* 规则命中 */
    EVENT_STATE_CHANGE,   /* 状态变更（ChangeSet 应用后） */
    EVENT_NARRATIVE,      /* 叙事生成 */
    EVENT_WORLD_TICK,     /* World Director tick（后台模拟） */
} EventType;

/* ═══════════════════════════════════════════════════════════════
   Event — per USE Spec Chapter 7
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int         id;                    /* 自增ID */
    long long   tick;                  /* 发生时 WorldState tick */
    int         source_id;             /* 来源实体ID，-1=系统 */
    int         target_id;             /* 目标实体ID，-1=无 */
    EventType   type;                  /* 事件类型 */
    char        payload[MAX_EVENT_PAYLOAD];  /* JSON payload */
} Event;

/* ═══════════════════════════════════════════════════════════════
   EventLog — append-only event queue
   ═══════════════════════════════════════════════════════════════ */

typedef struct EventLog {
    Event events[MAX_EVENTS];
    int   count;
    int   next_id;
} EventLog;

/* ── API ── */

/* Initialise event log */
void event_init(EventLog *log);

/* Add an event. Returns event id, or -1 if full. */
int event_push(EventLog *log, long long tick, int source_id, int target_id,
               EventType type, const char *payload_fmt, ...);

/* Export all events as a JSON array string (caller must free). */
char *event_export_json(const EventLog *log);

/* Get event type name string */
const char *event_type_str(EventType t);

#endif
