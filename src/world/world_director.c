#include "world_director.h"
#include "weather_sim.h"
#include "economy_sim.h"
#include "faction_sim.h"
#include "population_sim.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ═══════════════════════════════════════════════════════════════
   Internal helpers — last-tick tracking via world variables
   ═══════════════════════════════════════════════════════════════ */

static long long get_last_tick(const WorldState *ws, const char *name)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            /* Store long long tick as int for world vars; since ticks
               are minutes from year 1, they can fit in 32-bit (~4085 years) */
            return (long long)ws->variables[i].int_val;
        }
    }
    return 0;  /* never run before */
}

static void set_last_tick(WorldState *ws, const char *name, long long tick)
{
    /* Ticks are minutes from year 1 — INT_MAX (~2.1B) covers ~4085 years.
       If tick exceeds INT_MAX, clamp to INT_MAX rather than silently wrapping. */
    int stored = (tick > (long long)INT_MAX) ? INT_MAX : (int)tick;

    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            ws->variables[i].int_val = stored;
            ws->variables[i].type = VAR_INT;
            return;
        }
    }
    /* Create if missing */
    if (ws->variable_count >= WS_MAX_VARIABLES) return;
    WorldVariable *v = &ws->variables[ws->variable_count++];
    memset(v, 0, sizeof(*v));
    v->id = ws->variable_count - 1;
    safe_strcpy(v->name, name, sizeof(v->name));
    v->type = VAR_INT;
    v->int_val = stored;
}

/* ═══════════════════════════════════════════════════════════════
   Initialization
   ═══════════════════════════════════════════════════════════════ */

void wd_init(WorldState *ws, long long current_tick)
{
    /* Initialize last-tick markers so subsystems don't all fire
       immediately on the first tick after world creation */
    set_last_tick(ws, WD_VAR_LAST_WEATHER, current_tick);
    set_last_tick(ws, WD_VAR_LAST_ECONOMY, current_tick);
    set_last_tick(ws, WD_VAR_LAST_FACTION, current_tick);
    set_last_tick(ws, WD_VAR_LAST_POP,     current_tick);

    /* Initialize each subsystem's world variables */
    econ_sim_init(ws);
    faction_sim_init(ws);
    pop_sim_init(ws);
    /* weather_sim doesn't need init — it uses env->weather directly */
}

/* ═══════════════════════════════════════════════════════════════
   Tick
   ═══════════════════════════════════════════════════════════════ */

int wd_tick(WorldState *ws, EventLog *events, long long current_tick)
{
    int ran = 0;

    /* ── Weather: every 60 ticks (1 hour) ── */
    {
        long long last = get_last_tick(ws, WD_VAR_LAST_WEATHER);
        if (current_tick - last >= WEATHER_TICK_INTERVAL) {
            log_info("WD: weather tick (last=%lld, now=%lld)", last, current_tick);
            weather_sim_tick(ws, events, current_tick);
            set_last_tick(ws, WD_VAR_LAST_WEATHER, current_tick);
            ran++;
        }
    }

    /* ── Economy: every 1440 ticks (1 day) ── */
    {
        long long last = get_last_tick(ws, WD_VAR_LAST_ECONOMY);
        if (current_tick - last >= ECON_TICK_INTERVAL) {
            log_info("WD: economy tick (last=%lld, now=%lld)", last, current_tick);
            econ_sim_tick(ws, events, current_tick);
            set_last_tick(ws, WD_VAR_LAST_ECONOMY, current_tick);
            ran++;
        }
    }

    /* ── Faction: every 10080 ticks (1 week) ── */
    {
        long long last = get_last_tick(ws, WD_VAR_LAST_FACTION);
        if (current_tick - last >= FACTION_TICK_INTERVAL) {
            log_info("WD: faction tick (last=%lld, now=%lld)", last, current_tick);
            faction_sim_tick(ws, events, current_tick);
            set_last_tick(ws, WD_VAR_LAST_FACTION, current_tick);
            ran++;
        }
    }

    /* ── Population: every 43200 ticks (1 month) ── */
    {
        long long last = get_last_tick(ws, WD_VAR_LAST_POP);
        if (current_tick - last >= POP_TICK_INTERVAL) {
            log_info("WD: population tick (last=%lld, now=%lld)", last, current_tick);
            pop_sim_tick(ws, events, current_tick);
            set_last_tick(ws, WD_VAR_LAST_POP, current_tick);
            ran++;
        }
    }

    if (ran > 0) log_info("WD: tick complete — %d subsystem(s) ran", ran);
    return ran;
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int wd_export(const WorldState *ws, char *out, int out_size)
{
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos, "═══ 世界模拟状态 ═══\n");

    /* Weather */
    char wbuf[128];
    weather_sim_export(ws, wbuf, sizeof(wbuf));
    pos += snprintf(out + pos, out_size - pos, "%s\n", wbuf);

    /* Economy */
    char ebuf[256];
    econ_sim_export(ws, ebuf, sizeof(ebuf));
    pos += snprintf(out + pos, out_size - pos, "%s\n", ebuf);

    /* Population */
    char pbuf[256];
    pop_sim_export(ws, pbuf, sizeof(pbuf));
    pos += snprintf(out + pos, out_size - pos, "%s\n", pbuf);

    /* Factions (compact) */
    char fbuf[1024];
    faction_sim_export(ws, fbuf, sizeof(fbuf));
    pos += snprintf(out + pos, out_size - pos, "%s", fbuf);

    return pos;
}
