#ifndef SCHEDULER_H
#define SCHEDULER_H

/* ═══════════════════════════════════════════════════════════════
   Tick Scheduler — centralized simulation tick management.

   Replaces the ad-hoc per-subsystem last-tick tracking in
   world_director.c with a unified registration-based system.
   Each subsystem registers its tick callback with an interval;
   the scheduler fires due tasks in priority order.

   Part of USE Architecture extension (P2-2).
   ═══════════════════════════════════════════════════════════════ */

#include "worldstate.h"
#include "event.h"

/* Maximum number of registered scheduled tasks */
#define SCHED_MAX_TASKS 32

/* Callback signature: receives world state, event log, and current tick */
typedef void (*TickFn)(WorldState *ws, EventLog *events, long long tick);

typedef struct {
    TickFn     fn;              /* callback to invoke */
    int        interval_ticks;  /* how often to run (in ticks) */
    int        phase;           /* execution order within same tick (lower = earlier) */
    long long  next_fire_tick;  /* next tick at which this task should fire */
    const char *name;           /* human-readable name for logging */
    bool       enabled;         /* can be disabled at runtime */
} ScheduledTask;

typedef struct {
    ScheduledTask tasks[SCHED_MAX_TASKS];
    int           task_count;
} TickScheduler;

/* ── API ── */

/* Initialize an empty scheduler */
void sched_init(TickScheduler *s);

/* Register a task. Returns true on success, false if table is full. */
bool sched_register(TickScheduler *s, TickFn fn, int interval_ticks,
                    int phase, const char *name, long long current_tick);

/* Enable or disable a registered task by name */
void sched_set_enabled(TickScheduler *s, const char *name, bool enabled);

/* Run all tasks that are due at current_tick.
   Tasks are sorted by phase before execution.
   Returns number of tasks that ran. */
int sched_run_tick(TickScheduler *s, WorldState *ws, EventLog *events,
                   long long current_tick);

/* Export scheduler state for debugging */
int sched_export(const TickScheduler *s, char *out, int out_size);

#endif /* SCHEDULER_H */
