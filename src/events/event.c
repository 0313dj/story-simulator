#include "event.h"
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Initialisation
   ═══════════════════════════════════════════════════════════════ */

void event_init(EventLog *log)
{
    memset(log, 0, sizeof(*log));
}

/* ═══════════════════════════════════════════════════════════════
   Push event
   ═══════════════════════════════════════════════════════════════ */

int event_push(EventLog *log, long long tick, int source_id, int target_id,
               EventType type, const char *payload_fmt, ...)
{
    if (log->count >= MAX_EVENTS) {
        /* FIFO evict: shift all events left by 1, dropping the oldest */
        memmove(&log->events[0], &log->events[1],
                sizeof(Event) * (MAX_EVENTS - 1));
        log->count = MAX_EVENTS - 1;
    }

    Event *e = &log->events[log->count];
    memset(e, 0, sizeof(*e));
    e->id = log->next_id++;
    e->tick = tick;
    e->source_id = source_id;
    e->target_id = target_id;
    e->type = type;

    if (payload_fmt) {
        va_list ap;
        va_start(ap, payload_fmt);
        vsnprintf(e->payload, sizeof(e->payload), payload_fmt, ap);
        va_end(ap);
        e->payload[sizeof(e->payload) - 1] = '\0';
    }

    log->count++;
    LOG_D("Event #%d: type=%d tick=%lld source=%d target=%d",
          e->id, (int)type, tick, source_id, target_id);
    return e->id;
}

/* ═══════════════════════════════════════════════════════════════
   JSON export
   ═══════════════════════════════════════════════════════════════ */

/* Simple JSON string escaper */
static void json_escape(const char *src, char *dst, int dst_size)
{
    int j = 0;
    for (const char *s = src; *s && j < dst_size - 1; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\'; dst[j++] = 'n';
        } else if (c == '\r') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\'; dst[j++] = 'r';
        } else if (c == '\t') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\'; dst[j++] = 't';
        } else if (c < 0x20) {
            if (j + 6 >= dst_size) break;
            char hex[7];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            for (int k = 0; hex[k]; k++) dst[j++] = hex[k];
        } else {
            dst[j++] = *s;
        }
    }
    dst[j] = '\0';
}

static char *event_export_one_json(const Event *e)
{
    char payload_esc[MAX_EVENT_PAYLOAD * 2];
    json_escape(e->payload, payload_esc, sizeof(payload_esc));

    char *json = (char *)malloc(2048);
    if (!json) return NULL;

    snprintf(json, 2048,
        "{\"id\":%d,\"tick\":%lld,\"source\":%d,\"target\":%d,"
        "\"type\":\"%s\",\"payload\":\"%s\"}",
        e->id, e->tick, e->source_id, e->target_id,
        event_type_str(e->type), payload_esc);

    return json;
}

char *event_export_json(const EventLog *log)
{
    if (log->count == 0) {
        char *empty = (char *)malloc(4);
        if (empty) safe_strcpy(empty, "[]", 4);
        return empty;
    }

    /* Estimate size: ~256 bytes per event */
    int est_size = log->count * 300 + 4;
    char *json = (char *)malloc(est_size);
    if (!json) return NULL;

    int pos = 0;
    pos += snprintf(json + pos, est_size - pos, "[");

    for (int i = 0; i < log->count; i++) {
        if (i > 0) pos += snprintf(json + pos, est_size - pos, ",");

        char *one = event_export_one_json(&log->events[i]);
        if (one) {
            pos += snprintf(json + pos, est_size - pos, "%s", one);
            free(one);

            /* Re-allocate if needed */
            if (pos + 512 >= est_size) {
                est_size *= 2;
                char *new_json = (char *)realloc(json, est_size);
                if (!new_json) { free(json); return NULL; }
                json = new_json;
            }
        }
    }

    pos += snprintf(json + pos, est_size - pos, "]");
    return json;
}

/* ═══════════════════════════════════════════════════════════════
   Utility
   ═══════════════════════════════════════════════════════════════ */

const char *event_type_str(EventType t)
{
    switch (t) {
    case EVENT_SYSTEM:        return "SYSTEM";
    case EVENT_ACTION:        return "ACTION";
    case EVENT_RULE_HIT:      return "RULE_HIT";
    case EVENT_STATE_CHANGE:  return "STATE_CHANGE";
    case EVENT_NARRATIVE:     return "NARRATIVE";
    case EVENT_WORLD_TICK:    return "WORLD_TICK";
    default:                  return "UNKNOWN";
    }
}

